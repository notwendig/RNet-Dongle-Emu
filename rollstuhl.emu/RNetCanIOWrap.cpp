// RNET-CPOP-CLOG-V7-WRITE100-CONNECT1-HARDOFF
//
// Keep the existing CAN/CPOP object logging, but add one rule:
// after the completed sleep sequence (STD 0x000 RTR), the emulator TX side
// is latched OFF for the rest of this process lifetime.
//
// The initial session is intentionally unchanged so the currently working
// connect/read path is not disturbed. A deliberate new session starts by
// restarting rollstuhl.emu.

#include "../common/DongleMessages.hpp"
#include "../common/RNetStream.hpp"
#include "../common/DongleMessages.cpp"

#include <linux/can.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <vector>

extern "C" ssize_t __real_write(int, const void*, size_t);

namespace {

std::mutex g_clog_mutex;
std::atomic<bool> g_poweroff_latched{false};

bool StartupFallbackEnabledFromEnv() {
    const char* value = std::getenv("RNET_STARTOFF_FALLBACK");
    return value != nullptr && value[0] == '1' && value[1] == '\0';
}

// Fallback only: when current GUI source cannot be patched safely, suppress the
// automatically generated first wake sequence on CAN. A later explicit GUI ON
// starts a fresh 00C sequence and releases this startup latch.
std::atomic<bool> g_startup_off_latched{StartupFallbackEnabledFromEnv()};
std::atomic<bool> g_startup_auto_00c_seen{false};
std::atomic<unsigned> g_startup_suppressed_count{0};

struct BlockWriteState {
    bool selectorValid{false};
    std::array<std::uint8_t, 4> selector{};
    bool armed{false};
    bool receiving{false};
    bool awaitingFinalAck{false};
    std::uint32_t expectedSize{0};
    std::uint32_t window{64};
    std::uint32_t segmentsSinceAck{0};
    std::uint32_t lastSegment{0};
    std::vector<std::uint8_t> data;
};

std::mutex g_block_write_mutex;
BlockWriteState g_block_write;

// Complete write-finalization state recovered from the previously working
// emulator path (20/89 -> 20/88 -> 20/8A -> 40/8B -> 20/8F).
struct PostWriteState {
    bool active{false};
    bool haveValue89{false};
    bool haveKey88{false};
    std::uint32_t value89{0};
    std::array<std::uint8_t, 4> key88{};
    std::vector<std::uint8_t> data;
};

std::mutex g_post_write_mutex;
PostWriteState g_post_write;

std::atomic<bool> g_suppress_base_88{false};
std::atomic<bool> g_suppress_base_89{false};
std::atomic<bool> g_suppress_base_8a{false};
std::atomic<bool> g_suppress_base_8b{false};
std::atomic<bool> g_suppress_base_8f{false};

// First-connect race: the Programmer can send 20/80=0x1f while the JSM wake
// sequence has not yet reached 0x7B0. Remember that request and acknowledge it
// exactly when the observed startup sequence reaches 0x7B0.
std::atomic<bool> g_connect_ready{false};
std::atomic<bool> g_connect_pending{false};
std::atomic<int> g_connect_pending_fd{-1};

std::atomic<bool> g_hardoff_rewake_rejected_logged{false};

bool IsCanFd(int fd) {
    sockaddr_storage ss{};
    socklen_t len = sizeof(ss);
    return ::getsockname(fd, reinterpret_cast<sockaddr*>(&ss), &len) == 0
        && ss.ss_family == AF_CAN;
}

dongle_recovered::CCANMsg MakeCAN(const can_frame& frame) {
    dongle_recovered::CCANMsg msg;
    const bool ext = (frame.can_id & CAN_EFF_FLAG) != 0;
    msg.SetExtended(ext);
    msg.SetRemote((frame.can_id & CAN_RTR_FLAG) != 0);
    msg.SetID(frame.can_id & (ext ? CAN_EFF_MASK : CAN_SFF_MASK));

    const std::uint32_t dlc = std::min<std::uint32_t>(frame.can_dlc, 8U);
    msg.SetDLC(dlc);
    if (!msg.IsRemote()) {
        for (std::uint32_t i = 0; i < dlc; ++i) {
            msg[static_cast<int>(i)] = frame.data[i];
        }
    }
    return msg;
}

dongle_recovered::CCANMsg MakeCANFD(const canfd_frame& frame) {
    dongle_recovered::CCANMsg msg;
    const bool ext = (frame.can_id & CAN_EFF_FLAG) != 0;
    msg.SetExtended(ext);
    msg.SetRemote(false);
    msg.SetID(frame.can_id & (ext ? CAN_EFF_MASK : CAN_SFF_MASK));

    const std::uint32_t dlc = std::min<std::uint32_t>(frame.len, 8U);
    msg.SetDLC(dlc);
    for (std::uint32_t i = 0; i < dlc; ++i) {
        msg[static_cast<int>(i)] = frame.data[i];
    }
    return msg;
}

bool DecodeCAN(const void* data, std::size_t size, dongle_recovered::CCANMsg& msg) {
    if (!data || size < CAN_MTU) {
        return false;
    }

    msg = (size >= CANFD_MTU)
        ? MakeCANFD(*static_cast<const canfd_frame*>(data))
        : MakeCAN(*static_cast<const can_frame*>(data));
    return true;
}

void LogObject(const char* direction, const dongle_recovered::CCANMsg& can) {
    std::lock_guard<std::mutex> lock(g_clog_mutex);
    std::clog << "[rollstuhl.emu][" << direction << "] ";

    if (can.IsPOPMsg()) {
        const dongle_recovered::CPOPMsg pop(can);
        std::clog << pop; // requested: clog << CPOPMsg
    } else {
        std::clog << can;
    }

    std::clog << '\n';
    std::clog.flush();
}

enum class TxDecision {
    Pass,
    PassAndLatchOff,
    Suppress,
    SuppressError
};

TxDecision InspectTX(const void* data, std::size_t size) {
    dongle_recovered::CCANMsg can;
    if (!DecodeCAN(data, size, can)) {
        return TxDecision::Pass;
    }

    if (g_poweroff_latched.load(std::memory_order_acquire)) {
        const bool is00c =
            !can.IsExtended() && !can.IsRemote() && can.GetID() == 0x00CU;

        if (is00c) {
            LogObject("TX-REJECTED-HARDOFF-AUTO00C", can);
            if (!g_hardoff_rewake_rejected_logged.exchange(
                    true, std::memory_order_acq_rel)) {
                std::lock_guard<std::mutex> lock(g_clog_mutex);
                std::clog
                    << "[rollstuhl.emu][POWER] automatic re-wake rejected with "
                       "ENETDOWN; emulator remains Power OFF\n";
                std::clog.flush();
            }
            return TxDecision::SuppressError;
        }

        LogObject("TX-SUPPRESSED-OFF-LATCH", can);
        return TxDecision::Suppress;
    }

    if (g_startup_off_latched.load(std::memory_order_acquire)) {
        const bool is00c =
            !can.IsExtended() && !can.IsRemote() && can.GetID() == 0x00CU;

        if (is00c) {
            bool expected = false;
            if (g_startup_auto_00c_seen.compare_exchange_strong(
                    expected, true, std::memory_order_acq_rel)) {
                LogObject("TX-SUPPRESSED-STARTOFF-AUTO00C", can);
                std::lock_guard<std::mutex> lock(g_clog_mutex);
                std::clog
                    << "[rollstuhl.emu][POWER] automatic startup wake suppressed; "
                       "waiting for explicit GUI ON\n";
                std::clog.flush();
                return TxDecision::Suppress;
            }

            // The automatic startup sequence only contains one 00C. A later
            // 00C is therefore the explicit GUI ON sequence.
            g_startup_off_latched.store(false, std::memory_order_release);
            LogObject("TX-STARTOFF-RELEASE-GUI-ON", can);
            std::lock_guard<std::mutex> lock(g_clog_mutex);
            std::clog
                << "[rollstuhl.emu][POWER] explicit GUI ON detected; "
                   "START-OFF fallback released\n";
            std::clog.flush();
            return TxDecision::Pass;
        }

        const unsigned n =
            g_startup_suppressed_count.fetch_add(1, std::memory_order_relaxed);
        if (n < 16U) {
            LogObject("TX-SUPPRESSED-STARTOFF", can);
        } else if (n == 16U) {
            std::lock_guard<std::mutex> lock(g_clog_mutex);
            std::clog
                << "[rollstuhl.emu][POWER] further START-OFF suppressed "
                   "frames hidden from log until GUI ON\n";
            std::clog.flush();
        }
        return TxDecision::Suppress;
    }

    // If the old internal state machine also tries to answer one of the
    // write-finalization requests, discard that duplicate. V7 already emitted
    // the stateful answer directly from the RX side.
    if (!can.IsExtended() && !can.IsRemote() && can.GetID() == 0x790U &&
        data && size >= CAN_MTU) {
        const auto& f = *static_cast<const can_frame*>(data);
        if (f.can_dlc == 8U) {
            const std::uint8_t tc = f.data[0];
            const std::uint8_t odi = f.data[1];

            bool suppress = false;
            if (tc == 0x4FU && odi == 0x88U)
                suppress = g_suppress_base_88.exchange(false, std::memory_order_acq_rel);
            else if (tc == 0x4FU && odi == 0x89U)
                suppress = g_suppress_base_89.exchange(false, std::memory_order_acq_rel);
            else if (tc == 0x4FU && odi == 0x8AU)
                suppress = g_suppress_base_8a.exchange(false, std::memory_order_acq_rel);
            else if (tc == 0x2FU && odi == 0x8BU)
                suppress = g_suppress_base_8b.exchange(false, std::memory_order_acq_rel);
            else if (tc == 0x4FU && odi == 0x8FU)
                suppress = g_suppress_base_8f.exchange(false, std::memory_order_acq_rel);

            if (suppress) {
                LogObject("TX-SUPPRESSED-DUPLICATE-POSTWRITE", can);
                return TxDecision::Suppress;
            }
        }
    }

    LogObject("TX", can);

    // Observed end of the R-Net sleep sequence:
    //     000#R  (STD ID 0x000, RTR)
    //
    // Let this final sleep frame reach the bus. Only after successful send do
    // we latch the emulated chair OFF. The later automatic ~10 s wake sequence
    // (beginning with 00C#) is then suppressed.
    if (!can.IsExtended() && can.IsRemote() && can.GetID() == 0x000U) {
        return TxDecision::PassAndLatchOff;
    }

    return TxDecision::Pass;
}

void CommitLatchIfNeeded(TxDecision decision, ssize_t rc) {
    if (decision != TxDecision::PassAndLatchOff || rc < 0) {
        return;
    }

    g_poweroff_latched.store(true, std::memory_order_release);

    std::lock_guard<std::mutex> lock(g_clog_mutex);
    std::clog
        << "[rollstuhl.emu][POWER] completed 000#R sleep; "
           "OFF-LATCH SET -- automatic re-wake disabled\n";
    std::clog.flush();
}

bool IsWrite82QuickRequest(const can_frame& frame) {
    if ((frame.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG)) != 0U) {
        return false;
    }
    if ((frame.can_id & CAN_SFF_MASK) != 0x78FU || frame.can_dlc != 8U) {
        return false;
    }

    // Exact request at the observed Programmer 92% stop:
    //   78F#20 82 00 00 30 00 00 00
    //
    // The current rollstuhl.emu consumes this request but sends no CPOP
    // response. The matching server-side quick-transfer acknowledge is:
    //   790#4F 82 00 00 00 00 00 00
    //
    // Match the ODI/transfer type only. This also covers the same quick-write
    // register with another 32-bit value without affecting reads/block frames.
    return frame.data[0] == 0x20U
        && frame.data[1] == 0x82U
        && frame.data[2] == 0x00U
        && frame.data[3] == 0x00U;
}

void MaybeAckWrite82(int fd, const void* data, std::size_t size) {
    if (g_poweroff_latched.load(std::memory_order_acquire) ||
        g_startup_off_latched.load(std::memory_order_acquire)) {
        return;
    }
    if (!data || size < CAN_MTU) {
        return;
    }

    const auto& request = *static_cast<const can_frame*>(data);
    if (!IsWrite82QuickRequest(request)) {
        return;
    }

    can_frame ack{};
    ack.can_id = 0x790U;
    ack.can_dlc = 8U;
    ack.data[0] = 0x4FU;
    ack.data[1] = 0x82U;
    ack.data[2] = 0x00U;
    ack.data[3] = 0x00U;
    ack.data[4] = 0x00U;
    ack.data[5] = 0x00U;
    ack.data[6] = 0x00U;
    ack.data[7] = 0x00U;

    // Bypass our own --wrap=write hook so the ACK is emitted exactly once.
    const ssize_t rc = __real_write(fd, &ack, CAN_MTU);
    if (rc == CAN_MTU) {
        const auto ackObject = MakeCAN(ack);
        LogObject("TX-WRITE82-ACK", ackObject);
    } else {
        std::lock_guard<std::mutex> lock(g_clog_mutex);
        std::clog << "[rollstuhl.emu][WRITE82] ACK send failed rc=" << rc << '\n';
        std::clog.flush();
    }
}

bool IsWrite84CommitRequest(const can_frame& frame) {
    if ((frame.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG)) != 0U) return false;
    if ((frame.can_id & CAN_SFF_MASK) != 0x78FU || frame.can_dlc != 8U) return false;

    // Observed at the 92% write point, immediately after the working 0x82 ACK:
    // 78F#20 84 00 00 01 00 00 00
    return frame.data[0] == 0x20U
        && frame.data[1] == 0x84U
        && frame.data[2] == 0x00U
        && frame.data[3] == 0x00U
        && frame.data[4] == 0x01U
        && frame.data[5] == 0x00U
        && frame.data[6] == 0x00U
        && frame.data[7] == 0x00U;
}

void MaybeAckWrite84(int fd, const void* data, std::size_t size) {
    if (g_poweroff_latched.load(std::memory_order_acquire) ||
        g_startup_off_latched.load(std::memory_order_acquire)) return;
    if (!data || size < CAN_MTU) return;

    const auto& request = *static_cast<const can_frame*>(data);
    if (!IsWrite84CommitRequest(request)) return;

    can_frame ack{};
    ack.can_id = 0x790U;
    ack.can_dlc = 8U;
    ack.data[0] = 0x4FU;
    ack.data[1] = 0x84U;

    const ssize_t rc = __real_write(fd, &ack, CAN_MTU);
    if (rc == CAN_MTU) {
        LogObject("TX-WRITE84-ACK", MakeCAN(ack));
    } else {
        std::lock_guard<std::mutex> lock(g_clog_mutex);
        std::clog << "[rollstuhl.emu][WRITE84] ACK send failed rc=" << rc << '\n';
        std::clog.flush();
    }
}


bool IsStd78F(const can_frame& frame) {
    return (frame.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG)) == 0U
        && (frame.can_id & CAN_SFF_MASK) == 0x78FU
        && frame.can_dlc == 8U;
}

std::uint32_t Le24(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0])
        | (static_cast<std::uint32_t>(p[1]) << 8)
        | (static_cast<std::uint32_t>(p[2]) << 16);
}

std::uint32_t Le32(const std::uint8_t* p) {
    return Le24(p) | (static_cast<std::uint32_t>(p[3]) << 24);
}

bool WriteCanFrameDirect(int fd, const can_frame& frame, const char* tag) {
    const ssize_t rc = __real_write(fd, &frame, CAN_MTU);
    if (rc == CAN_MTU) {
        LogObject(tag, MakeCAN(frame));
        return true;
    }

    std::lock_guard<std::mutex> lock(g_clog_mutex);
    std::clog << "[rollstuhl.emu][" << tag << "] send failed rc=" << rc << '\n';
    std::clog.flush();
    return false;
}

void ObserveBlockWriteControl(const can_frame& frame) {
    if (!IsStd78F(frame) || frame.data[0] != 0x20U) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_block_write_mutex);

    if (frame.data[1] == 0x81U && frame.data[2] == 0U && frame.data[3] == 0U) {
        std::copy(frame.data + 4, frame.data + 8, g_block_write.selector.begin());
        g_block_write.selectorValid = true;
        return;
    }

    if (frame.data[1] == 0x82U && frame.data[2] == 0U && frame.data[3] == 0U) {
        g_block_write.armed = true;
        g_block_write.receiving = false;
        g_block_write.awaitingFinalAck = false;
        g_block_write.expectedSize = Le32(frame.data + 4);
        g_block_write.window = 64;
        g_block_write.segmentsSinceAck = 0;
        g_block_write.lastSegment = 0;
        g_block_write.data.clear();

        std::lock_guard<std::mutex> loglock(g_clog_mutex);
        std::clog << "[rollstuhl.emu][WRITE8C] armed expectedSize="
                  << g_block_write.expectedSize << '\n';
        std::clog.flush();
    }
}

bool MaybeStartSegmentedWrite(int fd, const can_frame& frame) {
    if (!IsStd78F(frame)
        || frame.data[0] != 0x10U
        || frame.data[1] != 0x8CU
        || frame.data[2] != 0U
        || frame.data[3] != 0U) {
        return false;
    }

    if (g_poweroff_latched.load(std::memory_order_acquire) ||
        g_startup_off_latched.load(std::memory_order_acquire)) {
        return false;
    }

    std::uint32_t transferSize = Le24(frame.data + 4);

    {
        std::lock_guard<std::mutex> lock(g_block_write_mutex);
        if (transferSize == 0) {
            transferSize = g_block_write.expectedSize;
        }
        if (transferSize == 0) {
            return false;
        }

        g_block_write.armed = true;
        g_block_write.receiving = true;
        g_block_write.awaitingFinalAck = false;
        g_block_write.expectedSize = transferSize;
        g_block_write.window = 64;
        g_block_write.segmentsSinceAck = 0;
        g_block_write.lastSegment = 0;
        g_block_write.data.clear();
        g_block_write.data.reserve(transferSize);
    }

    can_frame ready{};
    ready.can_id = 0x790U;
    ready.can_dlc = 8U;
    ready.data[0] = 0x4FU;
    ready.data[1] = 0x8CU;
    ready.data[4] = static_cast<std::uint8_t>(transferSize);
    ready.data[5] = static_cast<std::uint8_t>(transferSize >> 8);
    ready.data[6] = static_cast<std::uint8_t>(transferSize >> 16);
    ready.data[7] = 64U;

    const bool ok = WriteCanFrameDirect(fd, ready, "TX-WRITE8C-READY");
    if (ok) {
        std::lock_guard<std::mutex> loglock(g_clog_mutex);
        std::clog << "[rollstuhl.emu][WRITE8C] segmented download started size="
                  << transferSize << " block=64\n";
        std::clog.flush();
    }
    return ok;
}

bool MaybeCaptureSegmentedWriteData(int fd, const can_frame& frame) {
    if ((frame.can_id & CAN_EFF_FLAG) == 0U ||
        (frame.can_id & (CAN_RTR_FLAG | CAN_ERR_FLAG)) != 0U) {
        return false;
    }

    const std::uint32_t id = frame.can_id & CAN_EFF_MASK;
    if ((id & 0x1F000000U) != 0x1E000000U) {
        return false;
    }
    if ((id & 0x1FFF0000U) == 0x1E3F0000U) {
        return false;
    }

    const std::uint32_t segment = id & 0xFFFFU;
    if (segment == 0U || frame.can_dlc == 0U || frame.can_dlc > 8U) {
        return false;
    }

    bool sendAck = false;
    bool complete = false;
    std::uint32_t total = 0;
    std::uint32_t expected = 0;

    {
        std::lock_guard<std::mutex> lock(g_block_write_mutex);
        if (!g_block_write.receiving || g_block_write.expectedSize == 0U) {
            return false;
        }

        expected = g_block_write.expectedSize;
        const std::size_t already =
            std::min<std::size_t>(g_block_write.data.size(), expected);
        const std::size_t need = expected - already;
        const std::size_t take =
            std::min<std::size_t>(frame.can_dlc, need);

        g_block_write.data.insert(
            g_block_write.data.end(), frame.data, frame.data + take);
        g_block_write.lastSegment = segment;
        ++g_block_write.segmentsSinceAck;

        total = static_cast<std::uint32_t>(g_block_write.data.size());
        complete = total >= expected;
        sendAck = g_block_write.segmentsSinceAck >= g_block_write.window || complete;

        if (sendAck) {
            g_block_write.segmentsSinceAck = 0;
        }

        if (complete) {
            g_block_write.data.resize(expected);
            g_block_write.receiving = false;
            g_block_write.awaitingFinalAck = true;
        }
    }

    {
        std::lock_guard<std::mutex> loglock(g_clog_mutex);
        std::clog << "[rollstuhl.emu][WRITE8C] segment=" << segment
                  << " dlc=" << static_cast<unsigned>(frame.can_dlc)
                  << " total=" << total << '/' << expected << '\n';
        std::clog.flush();
    }

    if (sendAck) {
        can_frame segAck{};
        segAck.can_id = CAN_EFF_FLAG | 0x1E410000U;
        segAck.can_dlc = 0U;
        WriteCanFrameDirect(fd, segAck, "TX-WRITE8C-SEGACK");
    }

    if (complete) {
        std::lock_guard<std::mutex> loglock(g_clog_mutex);
        std::clog << "[rollstuhl.emu][WRITE8C] data complete; waiting TC_END\n";
        std::clog.flush();
    }

    return true;
}

bool MaybeFinishSegmentedWrite(int fd, const can_frame& frame) {
    if (!IsStd78F(frame)
        || frame.data[0] != 0x80U
        || frame.data[1] != 0x8CU
        || frame.data[2] != 0U
        || frame.data[3] != 0U) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_block_write_mutex);
        if (!g_block_write.awaitingFinalAck) {
            return false;
        }
    }

    can_frame endAck{};
    endAck.can_id = 0x790U;
    endAck.can_dlc = 8U;
    endAck.data[0] = 0x8FU;
    endAck.data[1] = 0x8CU;

    const bool ok = WriteCanFrameDirect(fd, endAck, "TX-WRITE8C-END");

    if (ok) {
        std::scoped_lock lock(g_block_write_mutex, g_post_write_mutex);
        g_post_write = PostWriteState{};
        g_post_write.active = true;
        g_post_write.data = g_block_write.data;
        g_block_write = BlockWriteState{};
    }
    return ok;
}

void HandleSegmentedWrite(int fd, const can_frame& frame) {
    ObserveBlockWriteControl(frame);

    if (MaybeStartSegmentedWrite(fd, frame)) {
        return;
    }
    if (MaybeCaptureSegmentedWriteData(fd, frame)) {
        return;
    }
    (void)MaybeFinishSegmentedWrite(fd, frame);
}


std::uint16_t Crc16CcittFalse(const std::uint8_t* data, std::size_t size) {
    std::uint16_t crc = 0xFFFFU;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= static_cast<std::uint16_t>(data[i]) << 8;
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U)
                ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021U)
                : static_cast<std::uint16_t>(crc << 1);
        }
    }
    return crc;
}

void CrcFeedLe32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24));
}

std::uint16_t CurrentPostWriteChecksumLocked() {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(12U + g_post_write.data.size());

    bytes.insert(bytes.end(),
                 g_post_write.key88.begin(), g_post_write.key88.end());
    CrcFeedLe32(bytes, g_post_write.value89);
    CrcFeedLe32(bytes, static_cast<std::uint32_t>(g_post_write.data.size()));
    bytes.insert(bytes.end(), g_post_write.data.begin(), g_post_write.data.end());

    return Crc16CcittFalse(bytes.data(), bytes.size());
}

bool IsQuickRequest(const can_frame& frame, std::uint8_t tc, std::uint8_t odi) {
    return IsStd78F(frame)
        && frame.data[0] == tc
        && frame.data[1] == odi
        && frame.data[2] == 0U
        && frame.data[3] == 0U;
}

bool SendQuickAck(int fd, std::uint8_t odi, const char* tag) {
    can_frame ack{};
    ack.can_id = 0x790U;
    ack.can_dlc = 8U;
    ack.data[0] = 0x4FU;
    ack.data[1] = odi;
    return WriteCanFrameDirect(fd, ack, tag);
}

void HandlePostWrite(int fd, const can_frame& frame) {
    {
        std::lock_guard<std::mutex> lock(g_post_write_mutex);
        if (!g_post_write.active) {
            return;
        }
    }

    // Previously working write completion (October 5):
    //   20/89 ACK
    //   20/88 ACK (4-byte key)
    //   20/8A ACK
    //   40/8B -> stateful CRC-16/CCITT-FALSE
    //   20/8F ACK, then write reaches 100%.
    if (IsQuickRequest(frame, 0x20U, 0x89U)) {
        {
            std::lock_guard<std::mutex> lock(g_post_write_mutex);
            g_post_write.value89 = Le32(frame.data + 4);
            g_post_write.haveValue89 = true;
        }
        g_suppress_base_89.store(true, std::memory_order_release);
        (void)SendQuickAck(fd, 0x89U, "TX-WRITE89-ACK");
        return;
    }

    if (IsQuickRequest(frame, 0x20U, 0x88U)) {
        {
            std::lock_guard<std::mutex> lock(g_post_write_mutex);
            std::copy(frame.data + 4, frame.data + 8, g_post_write.key88.begin());
            g_post_write.haveKey88 = true;
        }
        g_suppress_base_88.store(true, std::memory_order_release);
        (void)SendQuickAck(fd, 0x88U, "TX-WRITE88-ACK");
        return;
    }

    if (IsQuickRequest(frame, 0x20U, 0x8AU)) {
        g_suppress_base_8a.store(true, std::memory_order_release);
        (void)SendQuickAck(fd, 0x8AU, "TX-WRITE8A-ACK");
        return;
    }

    if (IsQuickRequest(frame, 0x40U, 0x8BU)) {
        std::uint16_t crc = 0;
        bool ready = false;
        {
            std::lock_guard<std::mutex> lock(g_post_write_mutex);
            ready = g_post_write.haveValue89 && g_post_write.haveKey88;
            if (ready) {
                crc = CurrentPostWriteChecksumLocked();
            }
        }

        if (!ready) {
            std::lock_guard<std::mutex> lock(g_clog_mutex);
            std::clog
                << "[rollstuhl.emu][WRITE-FINAL] 40/8B received before "
                   "20/89 + 20/88; leaving base state machine responsible\n";
            std::clog.flush();
            return;
        }

        can_frame reply{};
        reply.can_id = 0x790U;
        reply.can_dlc = 8U;
        reply.data[0] = 0x2FU;
        reply.data[1] = 0x8BU;
        reply.data[4] = static_cast<std::uint8_t>(crc);
        reply.data[5] = static_cast<std::uint8_t>(crc >> 8);

        g_suppress_base_8b.store(true, std::memory_order_release);
        if (WriteCanFrameDirect(fd, reply, "TX-WRITE8B-CRC")) {
            std::lock_guard<std::mutex> lock(g_clog_mutex);
            std::clog << "[rollstuhl.emu][WRITE-FINAL] CRC16="
                      << std::hex << static_cast<unsigned>(crc)
                      << std::dec << '\n';
            std::clog.flush();
        }
        return;
    }

    if (IsQuickRequest(frame, 0x20U, 0x8FU)) {
        g_suppress_base_8f.store(true, std::memory_order_release);
        if (SendQuickAck(fd, 0x8FU, "TX-WRITE8F-ACK")) {
            std::lock_guard<std::mutex> lock(g_post_write_mutex);
            g_post_write.active = false;
            g_post_write.data.clear();
        }
        return;
    }
}

void ObserveFirstConnectRequest(int fd, const can_frame& frame) {
    if (!IsQuickRequest(frame, 0x20U, 0x80U)) {
        return;
    }

    // Exact first connect request seen in the failing run:
    // 78F#20 80 00 00 1F 00 00 00
    if (Le32(frame.data + 4) != 0x1FU ||
        g_connect_ready.load(std::memory_order_acquire)) {
        return;
    }

    g_connect_pending_fd.store(fd, std::memory_order_release);
    if (!g_connect_pending.exchange(true, std::memory_order_acq_rel)) {
        std::lock_guard<std::mutex> lock(g_clog_mutex);
        std::clog
            << "[rollstuhl.emu][CONNECT1] first 20/80 request held until "
               "startup reaches 0x7B0\n";
        std::clog.flush();
    }
}

bool IsStartupReady7B0(const void* data, std::size_t size) {
    if (!data || size < CAN_MTU) {
        return false;
    }
    const auto& frame = *static_cast<const can_frame*>(data);
    return (frame.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG)) == 0U
        && (frame.can_id & CAN_SFF_MASK) == 0x7B0U;
}

void AfterSuccessfulTX(int fd, const void* data, std::size_t size, ssize_t rc) {
    if (rc < 0 || !IsStartupReady7B0(data, size)) {
        return;
    }

    g_connect_ready.store(true, std::memory_order_release);

    if (!g_connect_pending.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    int rxfd = g_connect_pending_fd.exchange(-1, std::memory_order_acq_rel);
    if (rxfd < 0) {
        rxfd = fd;
    }

    if (SendQuickAck(rxfd, 0x80U, "TX-CONNECT1-80-ACK")) {
        std::lock_guard<std::mutex> lock(g_clog_mutex);
        std::clog
            << "[rollstuhl.emu][CONNECT1] startup ready at 0x7B0; "
               "first connection request acknowledged\n";
        std::clog.flush();
    }
}


void HandleRX(int fd, const void* data, std::size_t size) {
    dongle_recovered::CCANMsg can;
    if (DecodeCAN(data, size, can)) {
        LogObject("RX", can);

        if (data && size >= CAN_MTU) {
            const auto& frame = *static_cast<const can_frame*>(data);
            ObserveFirstConnectRequest(fd, frame);
            HandleSegmentedWrite(fd, frame);
            HandlePostWrite(fd, frame);
        }

        MaybeAckWrite82(fd, data, size);
        MaybeAckWrite84(fd, data, size);
    }
}

std::size_t IovBytes(const iovec* iov, std::size_t count) {
    std::size_t total = 0;
    for (std::size_t i = 0; i < count; ++i) {
        total += iov[i].iov_len;
    }
    return total;
}

TxDecision InspectIovTX(const iovec* iov, std::size_t count) {
    if (!iov || count == 0) {
        return TxDecision::Pass;
    }
    return InspectTX(iov[0].iov_base, iov[0].iov_len);
}

void HandleIovRX(int fd, const iovec* iov, std::size_t count, std::size_t transferred) {
    if (!iov || count == 0 || transferred < CAN_MTU) {
        return;
    }
    HandleRX(fd, iov[0].iov_base, std::min<std::size_t>(iov[0].iov_len, transferred));
}

} // namespace

extern "C" {

ssize_t __real_read(int, void*, size_t);
ssize_t __real_send(int, const void*, size_t, int);
ssize_t __real_recv(int, void*, size_t, int);
ssize_t __real_sendto(int, const void*, size_t, int, const sockaddr*, socklen_t);
ssize_t __real_recvfrom(int, void*, size_t, int, sockaddr*, socklen_t*);
ssize_t __real_sendmsg(int, const msghdr*, int);
ssize_t __real_recvmsg(int, msghdr*, int);
ssize_t __real_writev(int, const iovec*, int);
ssize_t __real_readv(int, const iovec*, int);

ssize_t __wrap_write(int fd, const void* buf, size_t count) {
    if (!IsCanFd(fd)) {
        return __real_write(fd, buf, count);
    }

    const TxDecision decision = InspectTX(buf, count);
    if (decision == TxDecision::Suppress) {
        return static_cast<ssize_t>(count);
    }
    if (decision == TxDecision::SuppressError) {
        errno = ENETDOWN;
        return -1;
    }

    const ssize_t rc = __real_write(fd, buf, count);
    CommitLatchIfNeeded(decision, rc);
    AfterSuccessfulTX(fd, buf, count, rc);
    return rc;
}

ssize_t __wrap_read(int fd, void* buf, size_t count) {
    const ssize_t rc = __real_read(fd, buf, count);
    if (rc > 0 && IsCanFd(fd)) {
        HandleRX(fd, buf, static_cast<std::size_t>(rc));
    }
    return rc;
}

ssize_t __wrap_send(int fd, const void* buf, size_t len, int flags) {
    if (!IsCanFd(fd)) {
        return __real_send(fd, buf, len, flags);
    }

    const TxDecision decision = InspectTX(buf, len);
    if (decision == TxDecision::Suppress) {
        return static_cast<ssize_t>(len);
    }
    if (decision == TxDecision::SuppressError) {
        errno = ENETDOWN;
        return -1;
    }

    const ssize_t rc = __real_send(fd, buf, len, flags);
    CommitLatchIfNeeded(decision, rc);
    AfterSuccessfulTX(fd, buf, len, rc);
    return rc;
}

ssize_t __wrap_recv(int fd, void* buf, size_t len, int flags) {
    const ssize_t rc = __real_recv(fd, buf, len, flags);
    if (rc > 0 && IsCanFd(fd)) {
        HandleRX(fd, buf, static_cast<std::size_t>(rc));
    }
    return rc;
}

ssize_t __wrap_sendto(int fd, const void* buf, size_t len, int flags,
                      const sockaddr* addr, socklen_t addrlen) {
    if (!IsCanFd(fd)) {
        return __real_sendto(fd, buf, len, flags, addr, addrlen);
    }

    const TxDecision decision = InspectTX(buf, len);
    if (decision == TxDecision::Suppress) {
        return static_cast<ssize_t>(len);
    }
    if (decision == TxDecision::SuppressError) {
        errno = ENETDOWN;
        return -1;
    }

    const ssize_t rc = __real_sendto(fd, buf, len, flags, addr, addrlen);
    CommitLatchIfNeeded(decision, rc);
    AfterSuccessfulTX(fd, buf, len, rc);
    return rc;
}

ssize_t __wrap_recvfrom(int fd, void* buf, size_t len, int flags,
                        sockaddr* addr, socklen_t* addrlen) {
    const ssize_t rc = __real_recvfrom(fd, buf, len, flags, addr, addrlen);
    if (rc > 0 && IsCanFd(fd)) {
        HandleRX(fd, buf, static_cast<std::size_t>(rc));
    }
    return rc;
}

ssize_t __wrap_sendmsg(int fd, const msghdr* msg, int flags) {
    if (!msg || !IsCanFd(fd)) {
        return __real_sendmsg(fd, msg, flags);
    }

    const TxDecision decision = InspectIovTX(msg->msg_iov, msg->msg_iovlen);
    if (decision == TxDecision::Suppress) {
        return static_cast<ssize_t>(IovBytes(msg->msg_iov, msg->msg_iovlen));
    }
    if (decision == TxDecision::SuppressError) {
        errno = ENETDOWN;
        return -1;
    }

    const ssize_t rc = __real_sendmsg(fd, msg, flags);
    CommitLatchIfNeeded(decision, rc);
    if (msg->msg_iovlen > 0) {
        AfterSuccessfulTX(fd, msg->msg_iov[0].iov_base,
                          msg->msg_iov[0].iov_len, rc);
    }
    return rc;
}

ssize_t __wrap_recvmsg(int fd, msghdr* msg, int flags) {
    const ssize_t rc = __real_recvmsg(fd, msg, flags);
    if (rc > 0 && msg && IsCanFd(fd)) {
        HandleIovRX(fd, msg->msg_iov, msg->msg_iovlen, static_cast<std::size_t>(rc));
    }
    return rc;
}

ssize_t __wrap_writev(int fd, const iovec* iov, int iovcnt) {
    if (!iov || iovcnt <= 0 || !IsCanFd(fd)) {
        return __real_writev(fd, iov, iovcnt);
    }

    const TxDecision decision = InspectIovTX(iov, static_cast<std::size_t>(iovcnt));
    if (decision == TxDecision::Suppress) {
        return static_cast<ssize_t>(IovBytes(iov, static_cast<std::size_t>(iovcnt)));
    }
    if (decision == TxDecision::SuppressError) {
        errno = ENETDOWN;
        return -1;
    }

    const ssize_t rc = __real_writev(fd, iov, iovcnt);
    CommitLatchIfNeeded(decision, rc);
    AfterSuccessfulTX(fd, iov[0].iov_base, iov[0].iov_len, rc);
    return rc;
}

ssize_t __wrap_readv(int fd, const iovec* iov, int iovcnt) {
    const ssize_t rc = __real_readv(fd, iov, iovcnt);
    if (rc > 0 && iov && iovcnt > 0 && IsCanFd(fd)) {
        HandleIovRX(fd, iov, static_cast<std::size_t>(iovcnt), static_cast<std::size_t>(rc));
    }
    return rc;
}

} // extern "C"
