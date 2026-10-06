#include "ftd2xx_emu.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <deque>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr char kSerial[] = "RNET0200";
constexpr char kDescription[] = "RNet Dongle";
constexpr DWORD kDeviceId = 0x0403E128u; // VID 0403 / PID E128
constexpr DWORD kLibraryVersion = 0x00030208u;
constexpr DWORD kDriverVersion = 0x00021236u;
constexpr size_t kFrameSize = 24;
constexpr size_t kUserAreaSize = 64;

struct ReplayRx {
    std::array<unsigned char, kFrameSize> frame{};
    std::uint64_t delayUs = 0;
};

struct ReplayStep {
    std::array<unsigned char, kFrameSize> tx{};
    std::vector<ReplayRx> rx;
};

struct ScheduledFrame {
    std::uint64_t dueUs = 0;
    std::array<unsigned char, kFrameSize> frame{};
};

struct PeriodicSource {
    std::uint32_t canId = 0;
    std::uint64_t intervalUs = 0;
    std::uint64_t nextDueUs = 0;
    std::array<unsigned char, kFrameSize> frame{};
    bool active = false;
};

struct StoredBlock {
    std::array<unsigned char, 4> selector{};
    std::vector<unsigned char> data;

    // Repository metadata for a block that has been committed by the
    // Programmer. ODI 0x89 is needed to reproduce the file header/checksum
    // during later reads. Legacy RNB1 state files do not contain this field.
    std::uint32_t value89 = 0;
    bool value89Valid = false;
};

struct BlockWriteState {
    bool armed = false;
    bool receiving = false;
    bool awaitingFinalAck = false;
    std::array<unsigned char, 4> selector{};
    std::uint32_t expectedSize = 0;
    std::vector<unsigned char> data;
    std::uint32_t lastSegment = 0;
    unsigned window = 64;
    unsigned segmentsSinceAck = 0;
};

struct BlockReadState {
    bool active = false;
    bool awaitingFinalAck = false;
    const StoredBlock* block = nullptr;
    std::uint32_t nextSegment = 1;
    std::size_t offset = 0;
    unsigned window = 64;
};

struct PostWriteState {
    bool active = false;
    std::array<unsigned char, 4> selector{};
    std::uint32_t size = 0;
    std::uint32_t value89 = 0;
    bool value89Valid = false;
    std::array<unsigned char, 4> value88{};
    bool value88Valid = false;
    bool checksumServed = false;
};

struct Device {
    CRITICAL_SECTION cs{};
    CRITICAL_SECTION logCs{};
    bool opened = false;
    bool inputStopped = false;
    HANDLE eventHandle = nullptr;
    DWORD eventMask = 0;
    ULONG readTimeout = 300;
    ULONG writeTimeout = 300;
    UCHAR latency = 16;
    ULONG usbInSize = 4096;
    ULONG usbOutSize = 4096;
    ULONG baudRate = 115200;
    UCHAR wordLength = 8;
    UCHAR stopBits = 0;
    UCHAR parity = 0;
    USHORT flowControl = 0;
    bool dtr = false;
    bool rts = false;
    bool breakOn = false;
    DWORD waitMask = 0;
    USHORT divisor = 0;
    UCHAR bitMask = 0;
    UCHAR bitMode = 0;
    std::deque<unsigned char> rx;
    std::vector<unsigned char> txStream;
    std::array<unsigned char, kUserAreaSize> userArea{};
    std::array<unsigned short, 256> eeprom{};
    unsigned char filterMode = 0;
    bool flashOverCan = false;
    bool replayMode = false;
    bool emulateReenumeration = true;
    bool reenumerationPending = false;
    unsigned reopenFailuresRemaining = 0;
    bool everOpened = false;
    std::vector<ReplayStep> replay;
    size_t replayPos = 0;
    bool replayHasTiming = false;

    std::deque<ScheduledFrame> scheduled;
    std::vector<PeriodicSource> periodic;
    PeriodicSource statusPeriodic{};

    // Stateful POP block overlay. Writes made by the Programmer are kept by
    // this virtual dongle and override replay data on later reads.
    std::array<unsigned char, 4> currentSelector{};
    bool currentSelectorValid = false;
    std::vector<StoredBlock> storedBlocks;
    BlockWriteState blockWrite{};
    BlockReadState blockRead{};
    PostWriteState postWrite{};
    std::array<unsigned char, 4> popReplyHeader{{0xF2, 0x00, 0x06, 0x00}};
    std::string statePath;

    HANDLE schedulerWakeEvent = nullptr;
    HANDLE schedulerThread = nullptr;
    LARGE_INTEGER qpcFrequency{};

    std::string baseDir;
    std::string logPath;
};

Device g;
HMODULE gModule = nullptr;
INIT_ONCE gInitOnce = INIT_ONCE_STATIC_INIT;

std::string module_dir() {
    char path[MAX_PATH] = {};
    DWORD n = GetModuleFileNameA(gModule, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return ".";
    std::string s(path, n);
    const auto pos = s.find_last_of("\\/");
    return (pos == std::string::npos) ? "." : s.substr(0, pos);
}

std::string join_path(const std::string& a, const std::string& b) {
    if (b.empty()) return a;
    if (b.size() > 2 && (b[1] == ':' || b[0] == '\\' || b[0] == '/')) return b;
    if (a.empty() || a == ".") return b;
    return a + "\\" + b;
}

std::uint64_t now_us() {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    if (g.qpcFrequency.QuadPart <= 0) {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        g.qpcFrequency = frequency;
    }
    const std::uint64_t ticks = static_cast<std::uint64_t>(counter.QuadPart);
    const std::uint64_t freq = static_cast<std::uint64_t>(g.qpcFrequency.QuadPart);
    if (!freq) return static_cast<std::uint64_t>(GetTickCount64()) * 1000u;
    return (ticks / freq) * 1000000u + ((ticks % freq) * 1000000u) / freq;
}

void logf(const char* fmt, ...) {
    EnterCriticalSection(&g.logCs);
    FILE* f = std::fopen(g.logPath.c_str(), "a");
    if (f) {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        std::fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u.%03u ",
                     st.wYear, st.wMonth, st.wDay,
                     st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        va_list ap;
        va_start(ap, fmt);
        std::vfprintf(f, fmt, ap);
        va_end(ap);
        std::fputc('\n', f);
        std::fclose(f);
    }
    LeaveCriticalSection(&g.logCs);
}

bool parse_hex24(const std::string& src, std::array<unsigned char, kFrameSize>& out) {
    std::string h;
    h.reserve(src.size());
    for (unsigned char c : src) {
        if (std::isxdigit(c)) h.push_back(static_cast<char>(c));
    }
    if (h.size() != kFrameSize * 2) return false;
    auto hv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return 10 + (c - 'A');
    };
    for (size_t i = 0; i < kFrameSize; ++i) {
        const int hi = hv(h[i * 2]);
        const int lo = hv(h[i * 2 + 1]);
        if (hi < 0 || hi > 15 || lo < 0 || lo > 15) return false;
        out[i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return true;
}

std::string hex24(const unsigned char* p) {
    static const char* hx = "0123456789ABCDEF";
    std::string s;
    s.reserve(kFrameSize * 2);
    for (size_t i = 0; i < kFrameSize; ++i) {
        s.push_back(hx[p[i] >> 4]);
        s.push_back(hx[p[i] & 15]);
    }
    return s;
}

std::uint16_t crc16_ccitt_false_update(std::uint16_t crc,
                                         const unsigned char* data,
                                         std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= static_cast<std::uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000u)
                ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021u)
                : static_cast<std::uint16_t>(crc << 1);
    }
    return crc;
}

std::uint16_t crc16_ccitt_false(const unsigned char* data, std::size_t size) {
    return crc16_ccitt_false_update(0xFFFFu, data, size);
}

std::uint16_t pop_file_checksum(const std::array<unsigned char, 4>& selector,
                                std::uint32_t value89,
                                const std::vector<unsigned char>& data) {
    // ODI 0x8B is the repository-file checksum. Across the captured files it is
    // CRC-16/CCITT-FALSE over: selector (4 bytes), ODI 0x89 value (u32 LE),
    // file size (u32 LE), then the complete file payload.
    const unsigned char meta89[4] = {
        static_cast<unsigned char>(value89),
        static_cast<unsigned char>(value89 >> 8),
        static_cast<unsigned char>(value89 >> 16),
        static_cast<unsigned char>(value89 >> 24)
    };
    const std::uint32_t size = static_cast<std::uint32_t>(data.size());
    const unsigned char sizeLe[4] = {
        static_cast<unsigned char>(size),
        static_cast<unsigned char>(size >> 8),
        static_cast<unsigned char>(size >> 16),
        static_cast<unsigned char>(size >> 24)
    };

    std::uint16_t crc = 0xFFFFu;
    crc = crc16_ccitt_false_update(crc, selector.data(), selector.size());
    crc = crc16_ccitt_false_update(crc, meta89, sizeof(meta89));
    crc = crc16_ccitt_false_update(crc, sizeLe, sizeof(sizeLe));
    if (!data.empty())
        crc = crc16_ccitt_false_update(crc, data.data(), data.size());
    return crc;
}

bool same_selector(const std::array<unsigned char, 4>& a,
                   const std::array<unsigned char, 4>& b) {
    return std::equal(a.begin(), a.end(), b.begin());
}

std::string selector_text(const std::array<unsigned char, 4>& s) {
    char buf[16] = {};
    std::snprintf(buf, sizeof(buf), "%02X%02X%02X%02X",
                  s[0], s[1], s[2], s[3]);
    return buf;
}

void write_u32_le(std::ostream& out, std::uint32_t v) {
    const unsigned char b[4] = {
        static_cast<unsigned char>(v),
        static_cast<unsigned char>(v >> 8),
        static_cast<unsigned char>(v >> 16),
        static_cast<unsigned char>(v >> 24)
    };
    out.write(reinterpret_cast<const char*>(b), 4);
}

bool read_u32_le(std::istream& in, std::uint32_t& v) {
    unsigned char b[4] = {};
    if (!in.read(reinterpret_cast<char*>(b), 4)) return false;
    v = static_cast<std::uint32_t>(b[0]) |
        (static_cast<std::uint32_t>(b[1]) << 8) |
        (static_cast<std::uint32_t>(b[2]) << 16) |
        (static_cast<std::uint32_t>(b[3]) << 24);
    return true;
}

void save_stored_blocks_locked() {
    if (g.statePath.empty()) return;
    std::ofstream out(g.statePath, std::ios::binary | std::ios::trunc);
    if (!out) {
        logf("Block state: cannot write %s", g.statePath.c_str());
        return;
    }

    // RNB2 adds the repository metadata needed for stateful reads:
    //   selector[4], flags(u32), ODI89(u32), size(u32), data[size]
    // RNB1 is still accepted by the loader below.
    out.write("RNB2", 4);
    write_u32_le(out, static_cast<std::uint32_t>(g.storedBlocks.size()));
    for (const auto& block : g.storedBlocks) {
        out.write(reinterpret_cast<const char*>(block.selector.data()), 4);
        write_u32_le(out, block.value89Valid ? 1u : 0u);
        write_u32_le(out, block.value89);
        write_u32_le(out, static_cast<std::uint32_t>(block.data.size()));
        if (!block.data.empty())
            out.write(reinterpret_cast<const char*>(block.data.data()),
                      static_cast<std::streamsize>(block.data.size()));
    }
    logf("Block state saved: %u block(s), format=RNB2",
         static_cast<unsigned>(g.storedBlocks.size()));
}

void load_stored_blocks() {
    if (g.statePath.empty()) return;
    std::ifstream in(g.statePath, std::ios::binary);
    if (!in) return;

    char magic[4] = {};
    if (!in.read(magic, 4)) return;
    const bool rnb1 = std::memcmp(magic, "RNB1", 4) == 0;
    const bool rnb2 = std::memcmp(magic, "RNB2", 4) == 0;
    if (!rnb1 && !rnb2) {
        logf("Block state ignored: invalid header in %s", g.statePath.c_str());
        return;
    }

    std::uint32_t count = 0;
    if (!read_u32_le(in, count) || count > 256) return;
    std::vector<StoredBlock> loaded;
    for (std::uint32_t i = 0; i < count; ++i) {
        StoredBlock block;
        if (!in.read(reinterpret_cast<char*>(block.selector.data()), 4)) return;

        if (rnb2) {
            std::uint32_t flags = 0;
            if (!read_u32_le(in, flags)) return;
            if (!read_u32_le(in, block.value89)) return;
            block.value89Valid = (flags & 1u) != 0;
        }

        std::uint32_t size = 0;
        if (!read_u32_le(in, size) || size > 1024u * 1024u) return;
        block.data.resize(size);
        if (size && !in.read(reinterpret_cast<char*>(block.data.data()), size))
            return;
        loaded.push_back(std::move(block));
    }
    g.storedBlocks = std::move(loaded);
    logf("Block state loaded: %u block(s), format=%s",
         static_cast<unsigned>(g.storedBlocks.size()),
         rnb2 ? "RNB2" : "RNB1");
}

StoredBlock* find_stored_block_locked(
    const std::array<unsigned char, 4>& selector,
    std::uint32_t size) {
    for (auto& block : g.storedBlocks) {
        if (block.data.size() == size && same_selector(block.selector, selector))
            return &block;
    }
    return nullptr;
}

StoredBlock* find_stored_block_by_selector_locked(
    const std::array<unsigned char, 4>& selector) {
    for (auto& block : g.storedBlocks) {
        if (same_selector(block.selector, selector))
            return &block;
    }
    return nullptr;
}

bool commit_post_write_repository_block_locked() {
    if (!g.postWrite.active ||
        !g.postWrite.value88Valid ||
        !g.postWrite.value89Valid)
        return false;

    std::size_t stagingIndex = g.storedBlocks.size();
    for (std::size_t i = 0; i < g.storedBlocks.size(); ++i) {
        const auto& block = g.storedBlocks[i];
        if (block.data.size() == g.postWrite.size &&
            same_selector(block.selector, g.postWrite.selector)) {
            stagingIndex = i;
            break;
        }
    }
    if (stagingIndex == g.storedBlocks.size())
        return false;

    StoredBlock committed = std::move(g.storedBlocks[stagingIndex]);
    g.storedBlocks.erase(g.storedBlocks.begin() + stagingIndex);

    // The write itself is performed through the temporary/staging selector
    // (0x16 in the captured Programmer session). ODI 0x88 identifies the
    // repository file that the block actually replaces. Keep one current
    // block per repository selector, even when the new size differs.
    g.storedBlocks.erase(
        std::remove_if(g.storedBlocks.begin(), g.storedBlocks.end(),
                       [&](const StoredBlock& block) {
                           return same_selector(block.selector,
                                                g.postWrite.value88);
                       }),
        g.storedBlocks.end());

    committed.selector = g.postWrite.value88;
    committed.value89 = g.postWrite.value89;
    committed.value89Valid = true;

    const std::uint16_t checksum =
        pop_file_checksum(committed.selector,
                          committed.value89,
                          committed.data);
    const auto repositoryKey = committed.selector;
    const auto repositorySize =
        static_cast<std::uint32_t>(committed.data.size());
    const auto repositoryValue89 = committed.value89;

    g.storedBlocks.push_back(std::move(committed));
    save_stored_blocks_locked();

    logf("POP repository block committed: staging=%s key=%s value89=%u size=%u checksum=%04X",
         selector_text(g.postWrite.selector).c_str(),
         selector_text(repositoryKey).c_str(),
         static_cast<unsigned>(repositoryValue89),
         static_cast<unsigned>(repositorySize),
         static_cast<unsigned>(checksum));
    return true;
}

void store_block_locked(const std::array<unsigned char, 4>& selector,
                        const std::vector<unsigned char>& data) {
    StoredBlock* existing = find_stored_block_locked(
        selector, static_cast<std::uint32_t>(data.size()));
    if (existing) {
        existing->data = data;
    } else {
        StoredBlock block;
        block.selector = selector;
        block.data = data;
        g.storedBlocks.push_back(std::move(block));
    }
    const std::uint16_t crc = crc16_ccitt_false(data.data(), data.size());
    logf("POP block stored: selector=%s size=%u crc=%04X",
         selector_text(selector).c_str(),
         static_cast<unsigned>(data.size()),
         static_cast<unsigned>(crc));
    save_stored_blocks_locked();
}

void load_replay(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        logf("Replay file not found: %s", path.c_str());
        return;
    }

    ReplayStep* current = nullptr;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;

        if (line.rfind("TX ", 0) == 0) {
            ReplayStep step;
            if (!parse_hex24(line.substr(3), step.tx)) continue;
            g.replay.push_back(std::move(step));
            current = &g.replay.back();
            continue;
        }

        if (line.rfind("RX ", 0) == 0 && current) {
            std::istringstream iss(line.substr(3));
            std::string first;
            std::string second;
            if (!(iss >> first)) continue;

            ReplayRx rr{};
            std::string hexText;

            if (iss >> second) {
                char* endp = nullptr;
                const unsigned long long delay = std::strtoull(first.c_str(), &endp, 10);
                if (!endp || *endp != '\0') continue;
                rr.delayUs = static_cast<std::uint64_t>(delay);
                hexText = second;
                g.replayHasTiming = true;
            } else {
                // Legacy v1 transcript: RX <hex>
                rr.delayUs = 0;
                hexText = first;
            }

            if (parse_hex24(hexText, rr.frame))
                current->rx.push_back(rr);
        }
    }

    logf("Replay loaded: %u TX steps, timing=%s",
         static_cast<unsigned>(g.replay.size()),
         g.replayHasTiming ? "captured" : "legacy-fallback");
}

void start_scheduler();

BOOL CALLBACK init_once(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&g.cs);
    InitializeCriticalSection(&g.logCs);
    QueryPerformanceFrequency(&g.qpcFrequency);
    g.baseDir = module_dir();

    char logName[MAX_PATH] = "ftd2xx-emu.log";
    char mode[64] = "replay";
    char replayName[MAX_PATH] = "rnet-replay.txt";
    const std::string ini = join_path(g.baseDir, "ftd2xx-emu.ini");
    GetPrivateProfileStringA("emulator", "LogFile", "ftd2xx-emu.log",
                             logName, MAX_PATH, ini.c_str());
    GetPrivateProfileStringA("emulator", "Mode", "replay",
                             mode, static_cast<DWORD>(sizeof(mode)), ini.c_str());
    GetPrivateProfileStringA("emulator", "ReplayFile", "rnet-replay.txt",
                             replayName, MAX_PATH, ini.c_str());

    g.logPath = join_path(g.baseDir, logName);
    g.statePath = join_path(g.baseDir, "rnet-block-state.bin");
    g.replayMode = (_stricmp(mode, "replay") == 0);
    g.emulateReenumeration = GetPrivateProfileIntA(
        "emulator", "EmulateReenumeration", 1, ini.c_str()) != 0;

    // Values observed in the supplied capture.
    g.eeprom[0x80] = 0x000C;
    g.eeprom[0x81] = 0x0101;
    g.eeprom[0x82] = 0x05B0;
    g.eeprom[0x83] = 0xB014;
    g.eeprom[0x84] = 0x0000;
    g.eeprom[0x85] = 0x0000;
    g.eeprom[0x86] = 0x0001;
    g.eeprom[0x87] = 0x0000;
    g.eeprom[0x88] = 0x0000;
    g.eeprom[0x89] = 0x0000;
    g.eeprom[0x8A] = 0x0000;
    g.eeprom[0x8B] = 0x979D;

    load_stored_blocks();
    if (g.replayMode) load_replay(join_path(g.baseDir, replayName));
    logf("FTD2XX RNet emulator initialized: mode=%s, no USB backend",
         g.replayMode ? "replay" : "synthetic");
    start_scheduler();
    return TRUE;
}

void ensure_init() {
    InitOnceExecuteOnce(&gInitOnce, init_once, nullptr, nullptr);
}

bool valid_handle(FT_HANDLE h) {
    return h == reinterpret_cast<FT_HANDLE>(&g) && g.opened;
}

void signal_rx_locked() {
    if (!g.inputStopped && g.eventHandle && (g.eventMask & FT_EVENT_RXCHAR) && !g.rx.empty())
        SetEvent(g.eventHandle);
}

void enqueue_bytes_locked(const unsigned char* p, size_t n) {
    for (size_t i = 0; i < n; ++i) g.rx.push_back(p[i]);
    signal_rx_locked();
}

std::array<unsigned char, kFrameSize> make_rx_frame(const std::array<unsigned char, 19>& pdu) {
    std::array<unsigned char, kFrameSize> f{};
    f[0] = 0x10; f[1] = 0x02;
    std::copy(pdu.begin(), pdu.end(), f.begin() + 2);
    unsigned char x = 0x12; // 0x10 XOR 0x02, as observed for dongle->host
    for (unsigned char b : pdu) x ^= b;
    f[21] = x;
    f[22] = 0x10; f[23] = 0xFE;
    return f;
}

bool valid_tx_frame(const unsigned char* f) {
    if (f[0] != 0x10 || f[1] != 0x02 || f[22] != 0x10 || f[23] != 0xFE)
        return false;
    unsigned char x = 0;
    for (size_t i = 2; i < 21; ++i) x ^= f[i];
    return x == f[21];
}

void enqueue_pdu_locked(const std::array<unsigned char, 19>& pdu) {
    const auto f = make_rx_frame(pdu);
    enqueue_bytes_locked(f.data(), f.size());
    logf("RX %s", hex24(f.data()).c_str());
}

void queue_attached_status_locked() {
    std::array<unsigned char, 19> p{};
    p[0] = 0x00;
    p[1] = 0xFF;
    p[2] = 0x00;
    const unsigned char data[16] = {
        0x01,0x02,0x00,0x01,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x8A,0x00,0x00,0x00,0x02
    };
    std::copy(data, data + 16, p.begin() + 3);
    enqueue_pdu_locked(p);
}


bool decode_can_id(const std::array<unsigned char, kFrameSize>& frame,
                   std::uint32_t& canId) {
    const unsigned char* p = frame.data() + 2;
    if (p[0] != 0x01) return false;

    const std::uint32_t b1 = p[1];
    const std::uint32_t b2 = p[2];
    const std::uint32_t b3 = p[3];
    const std::uint32_t b4 = p[4];

    if ((b2 & 0x08u) != 0) {
        const std::uint32_t temp = (b1 << 24) | (b2 << 16);
        canId = (((temp & 0xFFE00000u) >> 2) |
                 ((b4 | temp) & 0x0007FFFEu) |
                 (b3 << 8)) >> 1;
    } else {
        canId = (b2 >> 5) | (b1 << 3);
    }
    return true;
}

std::uint64_t legacy_periodic_interval_us(
    const std::array<unsigned char, kFrameSize>& frame,
    std::uint32_t* decodedId = nullptr) {

    std::uint32_t id = 0;
    if (!decode_can_id(frame, id)) return 0;
    if (decodedId) *decodedId = id;

    // Periods documented in the supplied R-Net dictionaries.
    if (id == 0x0000000Eu) return 50000u;                    // 50 ms
    if ((id & 0xFFFFF0FFu) == 0x02000000u) return 10000u;   // joystick, 10 ms
    if (id == 0x03C30F0Fu) return 100000u;                   // 100 ms
    if ((id & 0xFFFF00FFu) == 0x0C140000u) return 1000000u; // 1 s
    if (id == 0x0C180400u) return 125000u;                   // about 125 ms
    if ((id & 0xFFFF00FFu) == 0x14300000u) return 200000u;  // 200 ms
    if ((id & 0xFFFF00FFu) == 0x1C0C0000u) return 1000000u; // 1 s
    if ((id & 0xFFFF00FFu) == 0x1C300004u) return 1000000u; // 1 s

    // This frame is strongly periodic in the supplied capture but its exact
    // function/rate is not documented in the dictionaries. 500 ms matches the
    // relative occurrence count in the legacy transcript well enough to keep
    // the fallback bus alive until a timestamped replay is generated.
    if ((id & 0xFFFFF0FFu) == 0x140C0001u) return 500000u;

    return 0;
}

bool is_status_periodic(const std::array<unsigned char, kFrameSize>& frame) {
    const unsigned char* p = frame.data() + 2;
    return p[0] == 0x00 && p[1] == 0xFF && p[2] == 0x00;
}

void wake_scheduler_locked() {
    if (g.schedulerWakeEvent) SetEvent(g.schedulerWakeEvent);
}

void schedule_frame_locked(const std::array<unsigned char, kFrameSize>& frame,
                           std::uint64_t delayUs) {
    if (delayUs == 0) {
        enqueue_bytes_locked(frame.data(), frame.size());
        logf("RX(timed) %s", hex24(frame.data()).c_str());
        return;
    }

    ScheduledFrame item{};
    item.dueUs = now_us() + delayUs;
    item.frame = frame;

    const auto it = std::upper_bound(
        g.scheduled.begin(), g.scheduled.end(), item.dueUs,
        [](std::uint64_t due, const ScheduledFrame& rhs) {
            return due < rhs.dueUs;
        });
    g.scheduled.insert(it, item);
    wake_scheduler_locked();
}

void update_periodic_locked(const std::array<unsigned char, kFrameSize>& frame,
                            std::uint32_t canId,
                            std::uint64_t intervalUs) {
    const std::uint64_t now = now_us();

    for (auto& source : g.periodic) {
        if (source.canId == canId) {
            source.frame = frame;
            source.intervalUs = intervalUs;
            if (!source.active) {
                source.active = true;
                source.nextDueUs = now;
            }
            wake_scheduler_locked();
            return;
        }
    }

    PeriodicSource source{};
    source.canId = canId;
    source.intervalUs = intervalUs;
    source.nextDueUs = now;
    source.frame = frame;
    source.active = true;
    g.periodic.push_back(source);
    logf("Legacy periodic CAN source: id=0x%08lX interval=%lu ms",
         static_cast<unsigned long>(canId),
         static_cast<unsigned long>(intervalUs / 1000u));
    wake_scheduler_locked();
}

void update_status_periodic_locked(
    const std::array<unsigned char, kFrameSize>& frame) {
    const std::uint64_t now = now_us();
    g.statusPeriodic.canId = 0xFFFFFFFFu;
    g.statusPeriodic.intervalUs = 300000u;
    g.statusPeriodic.frame = frame;
    if (!g.statusPeriodic.active) {
        g.statusPeriodic.active = true;
        g.statusPeriodic.nextDueUs = now;
        logf("Legacy periodic dongle-status source: interval=300 ms");
    }
    wake_scheduler_locked();
}

void emit_periodic_locked(PeriodicSource& source,
                          std::uint64_t now,
                          const char* tag) {
    if (!source.active || source.intervalUs == 0) return;
    if (source.nextDueUs > now) return;

    if (g.opened && !g.inputStopped) {
        enqueue_bytes_locked(source.frame.data(), source.frame.size());
    }
    (void)tag;

    // Never "catch up" by dumping a burst after the process was paused.
    source.nextDueUs += source.intervalUs;
    if (source.nextDueUs <= now)
        source.nextDueUs = now + source.intervalUs;
}

std::uint64_t next_due_locked() {
    std::uint64_t due = std::numeric_limits<std::uint64_t>::max();

    if (!g.scheduled.empty())
        due = std::min(due, g.scheduled.front().dueUs);

    if (g.statusPeriodic.active)
        due = std::min(due, g.statusPeriodic.nextDueUs);

    for (const auto& source : g.periodic) {
        if (source.active)
            due = std::min(due, source.nextDueUs);
    }

    return due;
}

DWORD WINAPI scheduler_thread_proc(LPVOID) {
    for (;;) {
        DWORD waitMs = INFINITE;

        EnterCriticalSection(&g.cs);
        const std::uint64_t now = now_us();

        while (!g.scheduled.empty() && g.scheduled.front().dueUs <= now) {
            const auto item = g.scheduled.front();
            g.scheduled.pop_front();

            if (g.opened && !g.inputStopped) {
                enqueue_bytes_locked(item.frame.data(), item.frame.size());
                logf("RX(timed) %s", hex24(item.frame.data()).c_str());
            }
        }

        emit_periodic_locked(g.statusPeriodic, now, "RX(status-periodic)");

        for (auto& source : g.periodic)
            emit_periodic_locked(source, now, "RX(CAN-periodic)");

        const std::uint64_t next = next_due_locked();
        const std::uint64_t after = now_us();

        if (next != std::numeric_limits<std::uint64_t>::max()) {
            if (next <= after) {
                waitMs = 0;
            } else {
                const std::uint64_t deltaUs = next - after;
                const std::uint64_t roundedMs = (deltaUs + 999u) / 1000u;
                waitMs = static_cast<DWORD>(
                    std::min<std::uint64_t>(roundedMs, 0xFFFFFFFEu));
            }
        }

        LeaveCriticalSection(&g.cs);

        if (!g.schedulerWakeEvent) {
            Sleep(waitMs == INFINITE ? 100u : waitMs);
        } else {
            WaitForSingleObject(g.schedulerWakeEvent, waitMs);
        }
    }
}

void start_scheduler() {
    g.schedulerWakeEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (!g.schedulerWakeEvent) {
        logf("Timed replay scheduler: CreateEvent failed (%lu)",
             static_cast<unsigned long>(GetLastError()));
        return;
    }

    g.schedulerThread = CreateThread(
        nullptr, 0, scheduler_thread_proc, nullptr, 0, nullptr);

    if (!g.schedulerThread) {
        logf("Timed replay scheduler: CreateThread failed (%lu)",
             static_cast<unsigned long>(GetLastError()));
        CloseHandle(g.schedulerWakeEvent);
        g.schedulerWakeEvent = nullptr;
        return;
    }

    logf("Timed replay scheduler started");
}

void remember_pop_reply_header_locked(
    const std::array<unsigned char, kFrameSize>& frame) {
    const unsigned char* p = frame.data() + 2;
    if (p[0] != 0x01 || (p[2] & 0x08u) != 0) return;
    const unsigned char tc = p[5];
    if (tc != 0x0F && tc != 0x2F && tc != 0x4F &&
        tc != 0x8F && tc != 0xCF) return;
    std::copy(p + 1, p + 5, g.popReplyHeader.begin());
}

void enqueue_pop_response_locked(const std::array<unsigned char, 8>& payload) {
    std::array<unsigned char, 19> p{};
    p[0] = 0x01;
    std::copy(g.popReplyHeader.begin(), g.popReplyHeader.end(), p.begin() + 1);
    std::copy(payload.begin(), payload.end(), p.begin() + 5);
    p[13] = 0x38;
    enqueue_pdu_locked(p);
}

void enqueue_pop_ack_locked(unsigned char odi) {
    std::array<unsigned char, 8> d{{0x4F, odi, 0, 0, 0, 0, 0, 0}};
    enqueue_pop_response_locked(d);
}

void enqueue_pop_u32_locked(unsigned char odi, std::uint32_t value) {
    std::array<unsigned char, 8> d{{
        0x2F, odi, 0, 0,
        static_cast<unsigned char>(value),
        static_cast<unsigned char>(value >> 8),
        static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 24)
    }};
    enqueue_pop_response_locked(d);
}

void enqueue_block_open_ack_locked(std::uint32_t size,
                                   unsigned window = 0) {
    // Response used by the observed upload/read path.
    std::array<unsigned char, 8> d{{
        0x0F, 0x8C, 0, 0,
        static_cast<unsigned char>(size),
        static_cast<unsigned char>(size >> 8),
        static_cast<unsigned char>(size >> 16),
        static_cast<unsigned char>(window & 0xFFu)
    }};
    enqueue_pop_response_locked(d);
}

void enqueue_block_download_ready_locked(std::uint32_t size,
                                         unsigned blockSize) {
    // DongleInterface.dll CRebusInterface::SegmentedDownload creates a
    // TC_DL_REQ with CRC flag (wire byte 0x10). Its transaction handler
    // accepts a non-segmented transfer-code 1 response, then reads Size and
    // Block from that response before it starts sending segmented data.
    // DL_RES is a server-to-client response. For server 0 / client F the
    // low node nibble is F, so TC=1 is wire byte 0x4F. Do not copy the
    // request CRC flag into the response: 0x50 is UL_REQ (wrong direction).
    std::array<unsigned char, 8> d{{
        0x4F, 0x8C, 0, 0,
        static_cast<unsigned char>(size),
        static_cast<unsigned char>(size >> 8),
        static_cast<unsigned char>(size >> 16),
        static_cast<unsigned char>(blockSize & 0xFFu)
    }};
    enqueue_pop_response_locked(d);
}

void enqueue_block_download_segment_response_locked() {
    // Segmented TC=1 server response. For server node 0 the 29-bit CAN ID is
    // 0x1E410000. DongleInterface.dll waits for this after every advertised
    // block and again after the final data segment. DLC is zero.
    std::array<unsigned char, 19> p{};
    p[0] = 0x01;
    p[1] = 0xF2;
    p[2] = 0x0A;
    p[3] = 0x00;
    p[4] = 0x00;
    p[13] = 0x00;
    enqueue_pdu_locked(p);
    logf("POP block write segmented response: id=0x1E410000");
}

void enqueue_pop_end_locked(unsigned char odi) {
    // Final non-segmented TC_END response. The transaction matcher requires
    // transfer code 2 and the same ODI; no payload data is required.
    // TC_END response is also server-to-client: server 0 / client F => 0x8F.
    std::array<unsigned char, 8> d{{0x8F, odi, 0, 0, 0, 0, 0, 0}};
    enqueue_pop_response_locked(d);
}

void enqueue_block_crc_locked(std::uint16_t crc) {
    std::array<unsigned char, 8> d{{
        0x8F, 0x8C, 0, 0,
        static_cast<unsigned char>(crc),
        static_cast<unsigned char>(crc >> 8),
        0, 0
    }};
    enqueue_pop_response_locked(d);
}

void enqueue_block_segment_locked(std::uint32_t segment,
                                  const unsigned char* data,
                                  unsigned dlc,
                                  bool last) {
    std::array<unsigned char, 19> p{};
    p[0] = 0x01;
    p[1] = 0xF2;
    p[2] = last ? 0x1E : 0x1C; // 0x1E43xxxx / 0x1E42xxxx
    p[3] = static_cast<unsigned char>((segment >> 7) & 0xFFu);
    p[4] = static_cast<unsigned char>((segment & 0x7Fu) << 1);
    if (dlc) std::copy(data, data + dlc, p.begin() + 5);
    p[13] = static_cast<unsigned char>(0x30u | (dlc & 0x0Fu));
    enqueue_pdu_locked(p);
}

void emit_block_read_window_locked() {
    if (!g.blockRead.active || !g.blockRead.block) return;
    const auto& data = g.blockRead.block->data;
    unsigned sent = 0;
    while (g.blockRead.offset < data.size() && sent < g.blockRead.window) {
        const std::size_t remain = data.size() - g.blockRead.offset;
        const unsigned dlc = static_cast<unsigned>(std::min<std::size_t>(8, remain));
        const bool last = (g.blockRead.offset + dlc == data.size());
        enqueue_block_segment_locked(g.blockRead.nextSegment,
                                     data.data() + g.blockRead.offset,
                                     dlc, last);
        g.blockRead.offset += dlc;
        ++g.blockRead.nextSegment;
        ++sent;
        if (last) break;
    }

    if (g.blockRead.offset == data.size()) {
        g.blockRead.awaitingFinalAck = true;
        logf("POP stored block read: sent final segment=%u, waiting segment ACK",
             static_cast<unsigned>(g.blockRead.nextSegment - 1));
    } else {
        logf("POP stored block read: sent window through segment=%u",
             static_cast<unsigned>(g.blockRead.nextSegment - 1));
    }
}

bool handle_stored_block_metadata_read_locked(const unsigned char* frame) {
    const unsigned char* p = frame + 2;
    if (p[0] != 0x01 || p[5] != 0x40 || !g.currentSelectorValid)
        return false;

    StoredBlock* block =
        find_stored_block_by_selector_locked(g.currentSelector);
    if (!block || !block->value89Valid)
        return false;

    const unsigned char odi = p[6];
    switch (odi) {
    case 0x86:
        enqueue_pop_u32_locked(
            0x86, static_cast<std::uint32_t>(block->data.size()));
        logf("POP stored metadata: selector=%s ODI86 size=%u",
             selector_text(block->selector).c_str(),
             static_cast<unsigned>(block->data.size()));
        return true;

    case 0x89:
        enqueue_pop_u32_locked(0x89, block->value89);
        logf("POP stored metadata: selector=%s ODI89 value=%u",
             selector_text(block->selector).c_str(),
             static_cast<unsigned>(block->value89));
        return true;

    case 0x8A:
        // All captured repository-file reads return zero for ODI 0x8A.
        enqueue_pop_u32_locked(0x8A, 0);
        logf("POP stored metadata: selector=%s ODI8A value=0",
             selector_text(block->selector).c_str());
        return true;

    case 0x8B: {
        const std::uint16_t checksum =
            pop_file_checksum(block->selector,
                              block->value89,
                              block->data);
        enqueue_pop_u32_locked(0x8B, checksum);
        logf("POP stored metadata: selector=%s ODI8B checksum=%04X",
             selector_text(block->selector).c_str(),
             static_cast<unsigned>(checksum));
        return true;
    }

    default:
        return false;
    }
}

bool start_stored_block_read_locked(const unsigned char* frame) {
    const unsigned char* p = frame + 2;
    if (p[0] != 0x01 || p[5] != 0x50 || p[6] != 0x8C ||
        !g.currentSelectorValid) return false;

    const std::uint32_t size = static_cast<std::uint32_t>(p[9]) |
        (static_cast<std::uint32_t>(p[10]) << 8) |
        (static_cast<std::uint32_t>(p[11]) << 16);
    StoredBlock* block = find_stored_block_locked(g.currentSelector, size);
    if (!block) return false;

    g.blockRead = BlockReadState{};
    g.blockRead.active = true;
    g.blockRead.block = block;
    g.blockRead.window = p[12] ? p[12] : 64;
    enqueue_block_open_ack_locked(size, 0);
    logf("POP stored block read: selector=%s size=%u window=%u",
         selector_text(g.currentSelector).c_str(),
         static_cast<unsigned>(size), g.blockRead.window);
    emit_block_read_window_locked();
    return true;
}

bool handle_block_read_ack_locked(const unsigned char* frame) {
    if (!g.blockRead.active || !g.blockRead.block) return false;
    std::array<unsigned char, kFrameSize> f{};
    std::copy(frame, frame + kFrameSize, f.begin());
    std::uint32_t id = 0;
    if (!decode_can_id(f, id)) return false;
    if ((id & 0x1FFF0000u) != 0x1E3F0000u) return false;

    const std::uint32_t ackSegment = id & 0xFFFFu;
    logf("POP stored block read: segment ACK=%u",
         static_cast<unsigned>(ackSegment));
    if (g.blockRead.awaitingFinalAck) {
        const auto& data = g.blockRead.block->data;
        enqueue_block_crc_locked(crc16_ccitt_false(data.data(), data.size()));
        g.blockRead.awaitingFinalAck = false;
    } else {
        emit_block_read_window_locked();
    }
    return true;
}

void observe_pop_control_locked(const unsigned char* frame) {
    const unsigned char* p = frame + 2;
    if (p[0] != 0x01 || p[5] != 0x20) return;
    const unsigned char odi = p[6];
    if (odi == 0x81) {
        std::copy(p + 9, p + 13, g.currentSelector.begin());
        g.currentSelectorValid = true;
    } else if (odi == 0x82 && g.currentSelectorValid) {
        g.blockWrite = BlockWriteState{};
        g.blockWrite.armed = true;
        g.blockWrite.selector = g.currentSelector;
        g.blockWrite.expectedSize = static_cast<std::uint32_t>(p[9]) |
            (static_cast<std::uint32_t>(p[10]) << 8) |
            (static_cast<std::uint32_t>(p[11]) << 16) |
            (static_cast<std::uint32_t>(p[12]) << 24);
        logf("POP block write armed: selector=%s size=%u",
             selector_text(g.blockWrite.selector).c_str(),
             static_cast<unsigned>(g.blockWrite.expectedSize));
    }
}

bool handle_dynamic_pop_control_locked(const unsigned char* frame) {
    const unsigned char* p = frame + 2;
    if (p[0] != 0x01) return false;
    const unsigned char tc = p[5];
    const unsigned char odi = p[6];

    // After a block download the Programmer updates repository metadata and
    // immediately reads ODI 0x8B to verify the complete file. Do not let the
    // static replay answer this with the checksum of the old file.
    if (g.postWrite.active && tc == 0x40 && odi == 0x8B &&
        g.currentSelectorValid &&
        same_selector(g.currentSelector, g.postWrite.selector) &&
        g.postWrite.value89Valid) {
        StoredBlock* block = find_stored_block_locked(g.postWrite.selector,
                                                       g.postWrite.size);
        if (block) {
            // ODI 0x88 is the repository key whose header participates in
            // the ODI 0x8B file checksum. During an ordinary read this key is
            // the selected file itself. After a copy/edit write the Programmer
            // explicitly supplies the source repository key with 20/88.
            // Example from the captured write: destination 16000100, 20/88 =
            // 06000100. Using the destination here yields 071D and the
            // Programmer rejects it; using 06000100 continues the same checksum
            // relation that produced the captured 6699 for the original file.
            const auto& checksumKey = g.postWrite.value88Valid
                ? g.postWrite.value88
                : g.postWrite.selector;
            const std::uint16_t checksum =
                pop_file_checksum(checksumKey,
                                  g.postWrite.value89,
                                  block->data);
            enqueue_pop_u32_locked(0x8B, checksum);
            logf("POP dynamic file checksum: selector=%s key88=%s value89=%u size=%u checksum=%04X",
                 selector_text(g.postWrite.selector).c_str(),
                 selector_text(checksumKey).c_str(),
                 static_cast<unsigned>(g.postWrite.value89),
                 static_cast<unsigned>(block->data.size()),
                 static_cast<unsigned>(checksum));
            g.postWrite.checksumServed = true;
            return true;
        }
    }

    // Remember post-download repository metadata. ODI 0x89 is part of the
    // checksum input; ODI 0x88 names the related/source repository file.
    if (g.postWrite.active && tc == 0x20 && odi == 0x89) {
        g.postWrite.value89 = static_cast<std::uint32_t>(p[9]) |
            (static_cast<std::uint32_t>(p[10]) << 8) |
            (static_cast<std::uint32_t>(p[11]) << 16) |
            (static_cast<std::uint32_t>(p[12]) << 24);
        g.postWrite.value89Valid = true;
        logf("POP post-write metadata: 20/89 value=%u",
             static_cast<unsigned>(g.postWrite.value89));
    } else if (g.postWrite.active && tc == 0x20 && odi == 0x88) {
        std::copy(p + 9, p + 13, g.postWrite.value88.begin());
        g.postWrite.value88Valid = true;
        logf("POP post-write metadata: 20/88 value=%s",
             selector_text(g.postWrite.value88).c_str());
    }

    // Ordinary POP control writes use a non-segmented TC=1 response.
    // The Programmer emits additional post-download control writes after a
    // modified block has been committed. These are not present at this point
    // in the read-only replay, so acknowledge the observed ODI values in the
    // stateful overlay just like the normal 0x80..0x85 control writes. ODI
    // 0x8F is the checksum commit sent after a successful 0x8B verification.
    if (tc == 0x20 &&
        ((odi >= 0x80 && odi <= 0x85) ||
         odi == 0x88 || odi == 0x89 || odi == 0x8A || odi == 0x8F)) {
        enqueue_pop_ack_locked(odi);
        logf("POP dynamic ACK: 20/%02X", odi);

        // 20/8F is the file-checksum commit. At this point ODI 0x88 tells us
        // the real repository key and ODI 0x89 supplies its metadata value.
        // Promote the just-written staging block so later reads can override
        // the stale replay contents for that repository file.
        if (g.postWrite.active && g.postWrite.checksumServed && odi == 0x8F)
            commit_post_write_repository_block_locked();

        if (g.postWrite.active && g.postWrite.checksumServed && odi == 0x80) {
            logf("POP post-write checksum transaction complete");
            g.postWrite = PostWriteState{};
        }
        return true;
    }

    // SegmentedDownload in DongleInterface.dll starts with a non-segmented
    // TC_DL_REQ carrying the CRC flag: wire byte 0x10, ODI 0x8C. The real
    // transaction does not start sending data until it receives a TC=1
    // response containing Size and Block.
    if (g.blockWrite.armed && odi == 0x8C && tc == 0x10) {
        g.blockWrite.receiving = true;
        g.blockWrite.awaitingFinalAck = false;
        g.blockWrite.data.clear();
        g.blockWrite.lastSegment = 0;
        g.blockWrite.window = 64;
        g.blockWrite.segmentsSinceAck = 0;

        enqueue_block_download_ready_locked(g.blockWrite.expectedSize,
                                            g.blockWrite.window);

        logf("POP block write transfer started: TC_DL_REQ selector=%s size=%u block=%u",
             selector_text(g.blockWrite.selector).c_str(),
             static_cast<unsigned>(g.blockWrite.expectedSize),
             g.blockWrite.window);
        return true;
    }

    // After the final segmented server response, DongleInterface.dll sends a
    // non-segmented TC_END (wire byte 0x80) containing its CRC. Commit the
    // buffered bytes only here and answer with TC_END so the transaction can
    // enter its completed state.
    if (tc == 0x80 && odi == 0x8C && g.blockWrite.awaitingFinalAck) {
        g.blockWrite.data.resize(g.blockWrite.expectedSize);
        store_block_locked(g.blockWrite.selector, g.blockWrite.data);
        const std::uint16_t crc =
            crc16_ccitt_false(g.blockWrite.data.data(), g.blockWrite.data.size());
        enqueue_pop_end_locked(0x8C);
        logf("POP block write committed: selector=%s size=%u crc=%04X",
             selector_text(g.blockWrite.selector).c_str(),
             static_cast<unsigned>(g.blockWrite.data.size()),
             static_cast<unsigned>(crc));

        g.postWrite = PostWriteState{};
        g.postWrite.active = true;
        g.postWrite.selector = g.blockWrite.selector;
        g.postWrite.size = static_cast<std::uint32_t>(g.blockWrite.data.size());

        g.blockWrite = BlockWriteState{};
        return true;
    }

    if (tc == 0x80 && odi == 0x8C && g.blockRead.active) {
        logf("POP stored block read complete");
        g.blockRead = BlockReadState{};
        return true;
    }

    return false;
}

bool capture_block_write_segment_locked(const unsigned char* frame) {
    if (!g.blockWrite.receiving || g.blockWrite.expectedSize == 0) return false;
    std::array<unsigned char, kFrameSize> f{};
    std::copy(frame, frame + kFrameSize, f.begin());
    std::uint32_t id = 0;
    if (!decode_can_id(f, id)) return false;
    if ((id & 0x1F000000u) != 0x1E000000u) return false;
    if ((id & 0x1FFF0000u) == 0x1E3F0000u) return false;

    const unsigned char* p = frame + 2;
    const unsigned dlc = p[13] & 0x0Fu;
    if (dlc == 0 || dlc > 8) return false;
    const std::uint32_t segment = id & 0xFFFFu;
    if (segment == 0) return false;

    const std::size_t need = g.blockWrite.expectedSize -
        std::min<std::size_t>(g.blockWrite.data.size(), g.blockWrite.expectedSize);
    const unsigned take = static_cast<unsigned>(std::min<std::size_t>(dlc, need));
    g.blockWrite.data.insert(g.blockWrite.data.end(), p + 5, p + 5 + take);
    g.blockWrite.lastSegment = segment;
++g.blockWrite.segmentsSinceAck;

    logf("POP block write segment: id=0x%08lX seg=%u dlc=%u total=%u/%u",
         static_cast<unsigned long>(id), static_cast<unsigned>(segment), dlc,
         static_cast<unsigned>(g.blockWrite.data.size()),
         static_cast<unsigned>(g.blockWrite.expectedSize));

    const bool complete = g.blockWrite.data.size() >= g.blockWrite.expectedSize;

    // DongleInterface.dll sends at most Block segmented frames, then waits for
    // a segmented TC=1 response before continuing. It also waits for the same
    // response after the final data segment.
    if (g.blockWrite.segmentsSinceAck >= g.blockWrite.window || complete) {
        enqueue_block_download_segment_response_locked();
        g.blockWrite.segmentsSinceAck = 0;
    }

    if (complete) {
        g.blockWrite.data.resize(g.blockWrite.expectedSize);
        g.blockWrite.receiving = false;
        g.blockWrite.awaitingFinalAck = true;
        const std::uint16_t crc =
            crc16_ccitt_false(g.blockWrite.data.data(), g.blockWrite.data.size());
        logf("POP block write data complete: selector=%s size=%u crc=%04X; waiting TC_END",
             selector_text(g.blockWrite.selector).c_str(),
             static_cast<unsigned>(g.blockWrite.data.size()),
             static_cast<unsigned>(crc));
    }
    return true;
}

bool replay_locked(const unsigned char* frame) {
    if (!g.replayMode || g.replay.empty()) return false;

    const size_t end = std::min(g.replay.size(), g.replayPos + 17);
    size_t hit = g.replay.size();
    for (size_t i = g.replayPos; i < end; ++i) {
        if (std::equal(g.replay[i].tx.begin(), g.replay[i].tx.end(), frame)) {
            hit = i;
            break;
        }
    }

    if (hit == g.replay.size()) {
        logf("REPLAY mismatch at step %u", static_cast<unsigned>(g.replayPos));
        return false;
    }

    if (hit != g.replayPos) {
        logf("REPLAY resync: skipped %u step(s)",
             static_cast<unsigned>(hit - g.replayPos));
    }

    for (const auto& rr : g.replay[hit].rx) {
        remember_pop_reply_header_locked(rr.frame);
        if (g.replayHasTiming) {
            schedule_frame_locked(rr.frame, rr.delayUs);
            continue;
        }

        // Legacy replay files did not contain time. Do not dump captured
        // cyclic traffic as one giant RX burst. Keep the latest cyclic frame
        // and let the scheduler send it at the documented bus period.
        std::uint32_t canId = 0;
        const std::uint64_t period =
            legacy_periodic_interval_us(rr.frame, &canId);

        if (period != 0) {
            update_periodic_locked(rr.frame, canId, period);
            continue;
        }

        if (is_status_periodic(rr.frame)) {
            update_status_periodic_locked(rr.frame);
            continue;
        }

        enqueue_bytes_locked(rr.frame.data(), rr.frame.size());
        logf("RX(replay) %s", hex24(rr.frame.data()).c_str());
    }

    g.replayPos = hit + 1;
    return true;
}

void synthetic_usb_command_locked(const unsigned char* f) {
    const unsigned char* p = f + 2; // 19-byte PDU
    if (p[0] != 0x00) return;
    const unsigned char cmd = p[1];
    const unsigned char sub = p[2];
    const unsigned char* d = p + 3;

    std::array<unsigned char, 19> r{};
    r[0] = 0x00;
    r[1] = cmd;
    r[2] = sub;

    if (cmd == 0x07 && sub == 0x00) { // ReadEEPROM
        const unsigned short v = g.eeprom[d[0]];
        r[3] = static_cast<unsigned char>(v >> 8);
        r[4] = static_cast<unsigned char>(v & 0xFF);
        enqueue_pdu_locked(r);
        return;
    }
    if (cmd == 0x07 && sub == 0x01) { // WriteEEPROM
        g.eeprom[d[0]] = static_cast<unsigned short>((d[1] << 8) | d[2]);
        return;
    }
    if (cmd == 0x05 && sub == 0x03) { // ReadFW
        r[3] = 0x04;
        r[4] = 0x4B;
        enqueue_pdu_locked(r);
        return;
    }
    if (cmd == 0x04 && sub == 0x03) { // WriteFilterMode
        g.filterMode = d[0];
        return;
    }
    if (cmd == 0x04 && sub == 0x02) { // ReadFilterMode
        r[3] = g.filterMode;
        enqueue_pdu_locked(r);
        return;
    }
    if (cmd == 0x05 && sub == 0x0A) { // WriteFlashOverCAN
        g.flashOverCan = d[0] != 0;
        if (g.emulateReenumeration) g.reenumerationPending = true;
        return;
    }
    if (cmd == 0x05 && sub == 0x09) { // ReadFlashOverCAN
        r[3] = g.flashOverCan ? 1 : 0;
        enqueue_pdu_locked(r);
        return;
    }
    if (cmd == 0x02 && sub == 0x04) { // Ping
        enqueue_pdu_locked(r);
        return;
    }

    // Common read commands: return a zero-filled response with echoed cmd/sub.
    const bool likelyRead =
        (cmd == 0x01 && (sub == 0x00 || sub == 0x02 || sub == 0x04 || sub == 0x05 || sub == 0x07)) ||
        (cmd == 0x02 && (sub == 0x00 || sub == 0x02)) ||
        (cmd == 0x03 && sub == 0x00) ||
        (cmd == 0x04 && sub == 0x00) ||
        (cmd == 0x05 && (sub == 0x01 || sub == 0x04 || sub == 0x06 || sub == 0x08)) ||
        (cmd == 0x08 && sub == 0x00);
    if (likelyRead) enqueue_pdu_locked(r);
}

void process_frame_locked(const unsigned char* frame) {
    logf("TX %s", hex24(frame).c_str());
    const unsigned char* pdu = frame + 2;

    // Track POP selectors and block-size writes even when the ordinary replay
    // supplies the response. This is the key used for the stateful block overlay.
    observe_pop_control_locked(frame);

    // Stateful repository data must override the read-only replay. Metadata
    // (ODI 86/89/8A/8B) and the segmented file payload are served from blocks
    // previously committed by a Programmer write.
    if (handle_block_read_ack_locked(frame)) return;
    if (handle_stored_block_metadata_read_locked(frame)) return;
    if (start_stored_block_read_locked(frame)) return;

    // Capture block data sent by the Programmer. This never reaches physical CAN;
    // it only updates the virtual R-Net image maintained by this DLL.
    if (capture_block_write_segment_locked(frame)) return;

    // Consume the final 80/8C acknowledgement of a dynamically served block
    // before the static replay gets a chance to resynchronise on it.
    if (g.blockRead.active && handle_dynamic_pop_control_locked(frame)) return;

    // Once a modified configuration starts a write transaction, do not let the
    // read-only replay resynchronise onto a superficially similar later command.
    // Keep the write transaction entirely in the stateful virtual target.
    if ((g.blockWrite.armed || g.postWrite.active) &&
        handle_dynamic_pop_control_locked(frame)) return;

    // The real trace shows command 05/0A followed by FT_IO_ERROR, Close,
    // one FT_DEVICE_NOT_FOUND from FT_OpenEx, then a successful reopen.
    // Remember that transition even when the response itself comes from replay.
    if (pdu[0] == 0x00 && pdu[1] == 0x05 && pdu[2] == 0x0A &&
        g.emulateReenumeration) {
        g.flashOverCan = pdu[3] != 0;
        g.reenumerationPending = true;
        logf("RNet dongle re-enumeration armed by command 05/0A");
    }

    if (replay_locked(frame)) return;

    // A modified configuration contains POP writes that are intentionally absent
    // from the read-only capture. Acknowledge those control writes dynamically.
    if (handle_dynamic_pop_control_locked(frame)) return;

    if (pdu[0] == 0x00) {
        synthetic_usb_command_locked(frame);
    } else if (pdu[0] == 0x01) {
        // CAN frame from the PC application. In synthetic mode there is deliberately
        // no physical CAN/USB backend. Replay mode can answer it from rnet-replay.txt.
        logf("CAN TX observed; no physical backend");
    }
}

void consume_tx_locked() {
    // DongleInterface.dll has two PC->dongle wire formats:
    //
    //   10 02 + 19-byte raw message + XOR + 10 FE
    //       24 bytes, used for one CAN/USB message.
    //
    //   10 01 + 3 * 14-byte compact CAN messages + XOR + 10 FE
    //       47 bytes, used by CFTDIInterface::TxCANMsg(CCANMsgList) when
    //       transmitting three CAN messages at once.
    //
    // The compact form is important for segmented POP downloads. The Programmer
    // sends groups of three data segments in this format; ignoring 10/01 means
    // that only the final remainder segment is visible and the download stalls.
    constexpr size_t kBurstCount = 3;
    constexpr size_t kCompactCanSize = 14;
    constexpr size_t kBurstPayloadSize = kBurstCount * kCompactCanSize; // 42
    constexpr size_t kBurstSize = 2 + kBurstPayloadSize + 1 + 2;        // 47

    for (;;) {
        // Resynchronise to either supported start marker. Preserve a lone 0x10
        // at the end because the second marker byte can arrive in a later
        // FT_Write call.
        size_t start = 0;
        while (start + 1 < g.txStream.size()) {
            if (g.txStream[start] == 0x10 &&
                (g.txStream[start + 1] == 0x01 ||
                 g.txStream[start + 1] == 0x02)) {
                break;
            }
            ++start;
        }

        if (start + 1 >= g.txStream.size()) {
            if (!g.txStream.empty() && g.txStream.back() == 0x10) {
                const unsigned char last = g.txStream.back();
                g.txStream.clear();
                g.txStream.push_back(last);
            } else {
                g.txStream.clear();
            }
            return;
        }

        if (start != 0)
            g.txStream.erase(g.txStream.begin(), g.txStream.begin() + start);

        if (g.txStream.size() < 2) return;

        const unsigned char wireType = g.txStream[1];

        if (wireType == 0x02) {
            if (g.txStream.size() < kFrameSize) return;

            if (!valid_tx_frame(g.txStream.data())) {
                logf("TX 10/02 framing/checksum error; resync by one byte");
                g.txStream.erase(g.txStream.begin());
                continue;
            }

            std::array<unsigned char, kFrameSize> f{};
            std::copy_n(g.txStream.begin(), kFrameSize, f.begin());
            g.txStream.erase(g.txStream.begin(),
                             g.txStream.begin() + kFrameSize);
            process_frame_locked(f.data());
            continue;
        }

        // 10/01: three compact CAN messages in one FT_Write packet.
        if (g.txStream.size() < kBurstSize) return;

        bool valid = g.txStream[0] == 0x10 &&
                     g.txStream[1] == 0x01 &&
                     g.txStream[kBurstSize - 2] == 0x10 &&
                     g.txStream[kBurstSize - 1] == 0xFE;

        unsigned char burstXor = 0;
        for (size_t i = 2; i < 2 + kBurstPayloadSize; ++i)
            burstXor ^= g.txStream[i];

        if (burstXor != g.txStream[2 + kBurstPayloadSize])
            valid = false;

        if (!valid) {
            logf("TX 10/01 burst framing/checksum error; resync by one byte");
            g.txStream.erase(g.txStream.begin());
            continue;
        }

        std::array<unsigned char, kBurstPayloadSize> compact{};
        std::copy_n(g.txStream.begin() + 2, kBurstPayloadSize,
                    compact.begin());
        g.txStream.erase(g.txStream.begin(),
                         g.txStream.begin() + kBurstSize);

        logf("TX 10/01 burst: 3 compact CAN messages");

        // Expand each compact CAN record back into the ordinary 19-byte PDU
        // representation already understood by process_frame_locked().
        // Compact record layout:
        //   type(1) + encoded CAN id(4) + data(8) + DLC(1) = 14 bytes.
        // The five trailing raw-message bytes omitted by 10/01 are zero for
        // these Programmer-generated CAN frames.
        for (size_t n = 0; n < kBurstCount; ++n) {
            std::array<unsigned char, kFrameSize> f{};
            f[0] = 0x10;
            f[1] = 0x02;

            const auto first = compact.begin() + n * kCompactCanSize;
            std::copy_n(first, kCompactCanSize, f.begin() + 2);

            unsigned char x = 0;
            for (size_t i = 2; i < 21; ++i)
                x ^= f[i];
            f[21] = x;
            f[22] = 0x10;
            f[23] = 0xFE;

            process_frame_locked(f.data());
        }
    }
}

void reset_io_locked(bool resetReplay) {
    g.rx.clear();
    g.txStream.clear();
    g.scheduled.clear();
    g.periodic.clear();
    g.statusPeriodic = PeriodicSource{};
    g.blockRead = BlockReadState{};
    if (resetReplay) g.replayPos = 0;
    g.inputStopped = false;
    queue_attached_status_locked();
    wake_scheduler_locked();
}

} // namespace

extern "C" {

FT_STATUS WINAPI FT_Open(int deviceNumber, FT_HANDLE* out) {
    ensure_init();
    if (!out) return FT_INVALID_PARAMETER;
    if (deviceNumber != 0) {
        *out = nullptr;
        return FT_DEVICE_NOT_FOUND;
    }
    return FT_OpenEx((PVOID)kSerial, FT_OPEN_BY_SERIAL_NUMBER, out);
}

FT_STATUS WINAPI FT_ListDevices(PVOID pArg1, PVOID pArg2, DWORD flags) {
    ensure_init();
    if (flags & FT_LIST_NUMBER_ONLY) {
        if (!pArg1) return FT_INVALID_PARAMETER;
        *static_cast<DWORD*>(pArg1) = 1;
        return FT_OK;
    }
    if ((flags & FT_LIST_BY_INDEX) && pArg2) {
        const uintptr_t index = reinterpret_cast<uintptr_t>(pArg1);
        if (index != 0) return FT_DEVICE_NOT_FOUND;
        char* dst = static_cast<char*>(pArg2);
        const char* src = (flags & FT_OPEN_BY_DESCRIPTION) ? kDescription : kSerial;
        std::strcpy(dst, src);
        return FT_OK;
    }
    return FT_OK;
}

FT_STATUS WINAPI FT_CreateDeviceInfoList(LPDWORD n) {
    ensure_init();
    if (!n) return FT_INVALID_PARAMETER;
    *n = 1;
    return FT_OK;
}

FT_STATUS WINAPI FT_GetDeviceInfoList(FT_DEVICE_LIST_INFO_NODE* p, LPDWORD n) {
    ensure_init();
    if (!n) return FT_INVALID_PARAMETER;
    if (p && *n >= 1) {
        std::memset(p, 0, sizeof(*p));
        p->Flags = g.opened ? FT_FLAGS_OPENED : 0;
        p->Type = FT_DEVICE_232R;
        p->ID = kDeviceId;
        p->LocId = 0;
        std::strncpy(p->SerialNumber, kSerial, sizeof(p->SerialNumber) - 1);
        std::strncpy(p->Description, kDescription, sizeof(p->Description) - 1);
        p->ftHandle = g.opened ? reinterpret_cast<FT_HANDLE>(&g) : nullptr;
    }
    *n = 1;
    return FT_OK;
}

FT_STATUS WINAPI FT_OpenEx(PVOID arg, DWORD flags, FT_HANDLE* out) {
    ensure_init();
    if (!out) return FT_INVALID_PARAMETER;
    bool match = true;
    if (flags == FT_OPEN_BY_SERIAL_NUMBER && arg)
        match = _stricmp(static_cast<const char*>(arg), kSerial) == 0;
    else if (flags == FT_OPEN_BY_DESCRIPTION && arg)
        match = _stricmp(static_cast<const char*>(arg), kDescription) == 0;
    if (!match) {
        *out = nullptr;
        return FT_DEVICE_NOT_FOUND;
    }
    EnterCriticalSection(&g.cs);
    if (g.reopenFailuresRemaining) {
        --g.reopenFailuresRemaining;
        *out = nullptr;
        LeaveCriticalSection(&g.cs);
        logf("FT_OpenEx: simulated transient FT_DEVICE_NOT_FOUND");
        return FT_DEVICE_NOT_FOUND;
    }
    const bool firstOpen = !g.everOpened;
    g.opened = true;
    g.everOpened = true;
    g.eventHandle = nullptr;
    g.eventMask = 0;
    // On USB re-enumeration the protocol trace continues at the next replay step.
    // Only a process's first open starts replay from step zero.
    reset_io_locked(firstOpen);
    *out = reinterpret_cast<FT_HANDLE>(&g);
    const size_t rp = g.replayPos;
    LeaveCriticalSection(&g.cs);
    logf("FT_OpenEx: virtual %s/%s opened (replay step %u)",
         kDescription, kSerial, static_cast<unsigned>(rp));
    return FT_OK;
}

FT_STATUS WINAPI FT_Close(FT_HANDLE h) {
    ensure_init();
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) {
        LeaveCriticalSection(&g.cs);
        return FT_INVALID_HANDLE;
    }
    g.opened = false;
    g.eventHandle = nullptr;
    g.eventMask = 0;
    g.rx.clear();
    g.txStream.clear();
    g.scheduled.clear();
    g.periodic.clear();
    g.statusPeriodic = PeriodicSource{};
    wake_scheduler_locked();
    LeaveCriticalSection(&g.cs);
    return FT_OK;
}

FT_STATUS WINAPI FT_Read(FT_HANDLE h, LPVOID buf, DWORD want, LPDWORD got) {
    ensure_init();
    if (!got || (want && !buf)) return FT_INVALID_PARAMETER;
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) {
        LeaveCriticalSection(&g.cs);
        return FT_INVALID_HANDLE;
    }
    DWORD n = 0;
    if (!g.inputStopped) {
        n = static_cast<DWORD>(std::min<size_t>(want, g.rx.size()));
        auto* d = static_cast<unsigned char*>(buf);
        for (DWORD i = 0; i < n; ++i) {
            d[i] = g.rx.front();
            g.rx.pop_front();
        }
    }
    *got = n;
    if (!g.rx.empty()) signal_rx_locked();
    LeaveCriticalSection(&g.cs);
    return FT_OK;
}

FT_STATUS WINAPI FT_Write(FT_HANDLE h, LPVOID buf, DWORD count, LPDWORD written) {
    ensure_init();
    if (!written || (count && !buf)) return FT_INVALID_PARAMETER;
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) {
        LeaveCriticalSection(&g.cs);
        return FT_INVALID_HANDLE;
    }
    const auto* p = static_cast<const unsigned char*>(buf);
    g.txStream.insert(g.txStream.end(), p, p + count);
    *written = count;
    consume_tx_locked();
    LeaveCriticalSection(&g.cs);
    return FT_OK;
}

FT_STATUS WINAPI FT_GetQueueStatus(FT_HANDLE h, DWORD* n) {
    ensure_init();
    if (!n) return FT_INVALID_PARAMETER;
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) {
        LeaveCriticalSection(&g.cs);
        return FT_INVALID_HANDLE;
    }
    if (g.reenumerationPending && g.rx.empty()) {
        g.reenumerationPending = false;
        g.reopenFailuresRemaining = 1;
        *n = 0;
        LeaveCriticalSection(&g.cs);
        logf("FT_GetQueueStatus: simulated FT_IO_ERROR for dongle re-enumeration");
        return FT_IO_ERROR;
    }
    *n = static_cast<DWORD>(g.rx.size());
    LeaveCriticalSection(&g.cs);
    return FT_OK;
}

FT_STATUS WINAPI FT_GetStatus(FT_HANDLE h, DWORD* rx, DWORD* tx, DWORD* ev) {
    ensure_init();
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) {
        LeaveCriticalSection(&g.cs);
        return FT_INVALID_HANDLE;
    }
    if (rx) *rx = static_cast<DWORD>(g.rx.size());
    if (tx) *tx = 0;
    if (ev) *ev = g.rx.empty() ? 0 : FT_EVENT_RXCHAR;
    LeaveCriticalSection(&g.cs);
    return FT_OK;
}

FT_STATUS WINAPI FT_GetEventStatus(FT_HANDLE h, DWORD* ev) {
    ensure_init();
    if (!ev) return FT_INVALID_PARAMETER;
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) { LeaveCriticalSection(&g.cs); return FT_INVALID_HANDLE; }
    *ev = g.rx.empty() ? 0 : FT_EVENT_RXCHAR;
    LeaveCriticalSection(&g.cs);
    return FT_OK;
}

FT_STATUS WINAPI FT_SetBreakOn(FT_HANDLE h) {
    ensure_init();
    if (!valid_handle(h)) return FT_INVALID_HANDLE;
    g.breakOn = true;
    logf("FT_SetBreakOn");
    return FT_OK;
}

FT_STATUS WINAPI FT_SetBreakOff(FT_HANDLE h) {
    ensure_init();
    if (!valid_handle(h)) return FT_INVALID_HANDLE;
    g.breakOn = false;
    logf("FT_SetBreakOff");
    return FT_OK;
}

FT_STATUS WINAPI FT_SetWaitMask(FT_HANDLE h, DWORD mask) {
    ensure_init();
    if (!valid_handle(h)) return FT_INVALID_HANDLE;
    g.waitMask = mask;
    logf("FT_SetWaitMask: 0x%08lX", static_cast<unsigned long>(mask));
    return FT_OK;
}

FT_STATUS WINAPI FT_WaitOnMask(FT_HANDLE h, DWORD* mask) {
    ensure_init();
    if (!mask) return FT_INVALID_PARAMETER;
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) { LeaveCriticalSection(&g.cs); return FT_INVALID_HANDLE; }
    *mask = (!g.rx.empty() && (g.waitMask & FT_EVENT_RXCHAR)) ? FT_EVENT_RXCHAR : 0;
    LeaveCriticalSection(&g.cs);
    return FT_OK;
}

FT_STATUS WINAPI FT_SetDivisor(FT_HANDLE h, USHORT divisor) {
    ensure_init();
    if (!valid_handle(h)) return FT_INVALID_HANDLE;
    g.divisor = divisor;
    logf("FT_SetDivisor: %u", divisor);
    return FT_OK;
}

FT_STATUS WINAPI FT_SetEventNotification(FT_HANDLE h, DWORD mask, PVOID param) {
    ensure_init();
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) {
        LeaveCriticalSection(&g.cs);
        return FT_INVALID_HANDLE;
    }
    g.eventMask = mask;
    g.eventHandle = static_cast<HANDLE>(param);
    signal_rx_locked();
    LeaveCriticalSection(&g.cs);
    return FT_OK;
}

FT_STATUS WINAPI FT_IoCtl(FT_HANDLE h, DWORD code, LPVOID, DWORD, LPVOID, DWORD,
                            LPDWORD returned, LPOVERLAPPED) {
    ensure_init();
    if (!valid_handle(h)) return FT_INVALID_HANDLE;
    if (returned) *returned = 0;
    logf("FT_IoCtl: code=0x%08lX (virtual no-op)", static_cast<unsigned long>(code));
    return FT_OK;
}

FT_STATUS WINAPI FT_SetBaudRate(FT_HANDLE h, ULONG baud) {
    ensure_init();
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) { LeaveCriticalSection(&g.cs); return FT_INVALID_HANDLE; }
    g.baudRate = baud;
    LeaveCriticalSection(&g.cs);
    logf("FT_SetBaudRate: %lu", static_cast<unsigned long>(baud));
    return FT_OK;
}

FT_STATUS WINAPI FT_SetDataCharacteristics(FT_HANDLE h, UCHAR word, UCHAR stop, UCHAR parity) {
    ensure_init();
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) { LeaveCriticalSection(&g.cs); return FT_INVALID_HANDLE; }
    g.wordLength = word;
    g.stopBits = stop;
    g.parity = parity;
    LeaveCriticalSection(&g.cs);
    logf("FT_SetDataCharacteristics: word=%u stop=%u parity=%u", word, stop, parity);
    return FT_OK;
}

FT_STATUS WINAPI FT_SetFlowControl(FT_HANDLE h, USHORT flow, UCHAR xon, UCHAR xoff) {
    ensure_init();
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) { LeaveCriticalSection(&g.cs); return FT_INVALID_HANDLE; }
    g.flowControl = flow;
    LeaveCriticalSection(&g.cs);
    logf("FT_SetFlowControl: flow=0x%04X xon=0x%02X xoff=0x%02X", flow, xon, xoff);
    return FT_OK;
}

FT_STATUS WINAPI FT_ResetDevice(FT_HANDLE h) {
    ensure_init();
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) {
        LeaveCriticalSection(&g.cs);
        return FT_INVALID_HANDLE;
    }
    reset_io_locked(false);
    LeaveCriticalSection(&g.cs);
    return FT_OK;
}

FT_STATUS WINAPI FT_ResetPort(FT_HANDLE h) { return FT_ResetDevice(h); }
FT_STATUS WINAPI FT_CyclePort(FT_HANDLE h) { return FT_ResetDevice(h); }

FT_STATUS WINAPI FT_StopInTask(FT_HANDLE h) {
    ensure_init();
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) {
        LeaveCriticalSection(&g.cs);
        return FT_INVALID_HANDLE;
    }
    g.inputStopped = true;
    LeaveCriticalSection(&g.cs);
    return FT_OK;
}

FT_STATUS WINAPI FT_RestartInTask(FT_HANDLE h) {
    ensure_init();
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) {
        LeaveCriticalSection(&g.cs);
        return FT_INVALID_HANDLE;
    }
    g.inputStopped = false;
    signal_rx_locked();
    LeaveCriticalSection(&g.cs);
    return FT_OK;
}

FT_STATUS WINAPI FT_SetDtr(FT_HANDLE h) {
    ensure_init();
    if (!valid_handle(h)) return FT_INVALID_HANDLE;
    g.dtr = true;
    logf("FT_SetDtr");
    return FT_OK;
}

FT_STATUS WINAPI FT_ClrDtr(FT_HANDLE h) {
    ensure_init();
    if (!valid_handle(h)) return FT_INVALID_HANDLE;
    g.dtr = false;
    logf("FT_ClrDtr");
    return FT_OK;
}

FT_STATUS WINAPI FT_SetRts(FT_HANDLE h) {
    ensure_init();
    if (!valid_handle(h)) return FT_INVALID_HANDLE;
    g.rts = true;
    logf("FT_SetRts");
    return FT_OK;
}

FT_STATUS WINAPI FT_ClrRts(FT_HANDLE h) {
    ensure_init();
    if (!valid_handle(h)) return FT_INVALID_HANDLE;
    g.rts = false;
    logf("FT_ClrRts");
    return FT_OK;
}

FT_STATUS WINAPI FT_GetModemStatus(FT_HANDLE h, ULONG* status) {
    ensure_init();
    if (!status) return FT_INVALID_PARAMETER;
    if (!valid_handle(h)) return FT_INVALID_HANDLE;
    *status = 0;
    return FT_OK;
}

FT_STATUS WINAPI FT_SetChars(FT_HANDLE h, UCHAR, UCHAR, UCHAR, UCHAR) {
    ensure_init();
    return valid_handle(h) ? FT_OK : FT_INVALID_HANDLE;
}

FT_STATUS WINAPI FT_Purge(FT_HANDLE h, ULONG mask) {
    ensure_init();
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) { LeaveCriticalSection(&g.cs); return FT_INVALID_HANDLE; }
    if (mask & FT_PURGE_TX) g.txStream.clear();
    // A real dongle answers asynchronously. The emulator currently creates the
    // replay/synthetic reply during FT_Write(), so deleting RX immediately here
    // would create an emulator-only Write -> Purge -> Read race.
    const size_t rx = g.rx.size();
    LeaveCriticalSection(&g.cs);
    logf("FT_Purge: mask=0x%lX rx=%u (RX preserved for async emulation)",
         static_cast<unsigned long>(mask), static_cast<unsigned>(rx));
    return FT_OK;
}

FT_STATUS WINAPI FT_SetTimeouts(FT_HANDLE h, ULONG r, ULONG w) {
    ensure_init();
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) {
        LeaveCriticalSection(&g.cs);
        return FT_INVALID_HANDLE;
    }
    g.readTimeout = r;
    g.writeTimeout = w;
    LeaveCriticalSection(&g.cs);
    return FT_OK;
}

FT_STATUS WINAPI FT_SetLatencyTimer(FT_HANDLE h, UCHAR v) {
    ensure_init();
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) {
        LeaveCriticalSection(&g.cs);
        return FT_INVALID_HANDLE;
    }
    g.latency = v;
    LeaveCriticalSection(&g.cs);
    return FT_OK;
}

FT_STATUS WINAPI FT_GetLatencyTimer(FT_HANDLE h, PUCHAR v) {
    ensure_init();
    if (!v) return FT_INVALID_PARAMETER;
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) {
        LeaveCriticalSection(&g.cs);
        return FT_INVALID_HANDLE;
    }
    *v = g.latency;
    LeaveCriticalSection(&g.cs);
    return FT_OK;
}

FT_STATUS WINAPI FT_SetBitMode(FT_HANDLE h, UCHAR mask, UCHAR mode) {
    ensure_init();
    if (!valid_handle(h)) return FT_INVALID_HANDLE;
    g.bitMask = mask;
    g.bitMode = mode;
    logf("FT_SetBitMode: mask=0x%02X mode=0x%02X", mask, mode);
    return FT_OK;
}

FT_STATUS WINAPI FT_GetBitMode(FT_HANDLE h, PUCHAR mode) {
    ensure_init();
    if (!mode) return FT_INVALID_PARAMETER;
    if (!valid_handle(h)) return FT_INVALID_HANDLE;
    *mode = g.bitMode;
    return FT_OK;
}

FT_STATUS WINAPI FT_SetUSBParameters(FT_HANDLE h, ULONG inSize, ULONG outSize) {
    ensure_init();
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) {
        LeaveCriticalSection(&g.cs);
        return FT_INVALID_HANDLE;
    }
    g.usbInSize = inSize;
    g.usbOutSize = outSize;
    LeaveCriticalSection(&g.cs);
    return FT_OK;
}

FT_STATUS WINAPI FT_EE_UASize(FT_HANDLE h, LPDWORD size) {
    ensure_init();
    if (!size) return FT_INVALID_PARAMETER;
    if (!valid_handle(h)) return FT_INVALID_HANDLE;
    *size = static_cast<DWORD>(g.userArea.size());
    return FT_OK;
}

FT_STATUS WINAPI FT_EE_UARead(FT_HANDLE h, PUCHAR dst, DWORD len, LPDWORD got) {
    ensure_init();
    if (!got || (len && !dst)) return FT_INVALID_PARAMETER;
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) {
        LeaveCriticalSection(&g.cs);
        return FT_INVALID_HANDLE;
    }
    const DWORD n = static_cast<DWORD>(std::min<size_t>(len, g.userArea.size()));
    std::copy_n(g.userArea.begin(), n, dst);
    *got = n;
    LeaveCriticalSection(&g.cs);
    return FT_OK;
}

FT_STATUS WINAPI FT_EE_UAWrite(FT_HANDLE h, PUCHAR src, DWORD len) {
    ensure_init();
    if (len && !src) return FT_INVALID_PARAMETER;
    EnterCriticalSection(&g.cs);
    if (!valid_handle(h)) {
        LeaveCriticalSection(&g.cs);
        return FT_INVALID_HANDLE;
    }
    const size_t n = std::min<size_t>(len, g.userArea.size());
    std::copy_n(src, n, g.userArea.begin());
    LeaveCriticalSection(&g.cs);
    return FT_OK;
}

FT_STATUS WINAPI FT_GetDriverVersion(FT_HANDLE h, LPDWORD v) {
    ensure_init();
    if (!v) return FT_INVALID_PARAMETER;
    if (!valid_handle(h)) return FT_INVALID_HANDLE;
    *v = kDriverVersion;
    return FT_OK;
}

FT_STATUS WINAPI FT_GetLibraryVersion(LPDWORD v) {
    ensure_init();
    if (!v) return FT_INVALID_PARAMETER;
    *v = kLibraryVersion;
    return FT_OK;
}

} // extern "C"

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        gModule = h;
        DisableThreadLibraryCalls(h);
    }
    return TRUE;
}