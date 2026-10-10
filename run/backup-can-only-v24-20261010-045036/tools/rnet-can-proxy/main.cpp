#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <string>

namespace {

constexpr std::uint16_t kBasePort = 39000;
constexpr std::size_t kPacketSize = 20;

constexpr unsigned char kHello = 1;
constexpr unsigned char kCanTx = 2;
constexpr unsigned char kCanRx = 3;
constexpr unsigned char kHelloAck = 4;

constexpr char kPowerOffGateMarker[] = "RNET-CAN-POWEROFF-GATE-V19";

std::uint16_t interface_port(const std::string& ifname) {
    if (ifname.rfind("can", 0) == 0 && ifname.size() > 3) {
        char* end = nullptr;
        const long idx = std::strtol(ifname.c_str() + 3, &end, 10);
        if (end && *end == '\0' && idx >= 0 && idx < 1000) {
            return static_cast<std::uint16_t>(kBasePort + idx);
        }
    }
    return kBasePort;
}

void put_u32le(unsigned char* p, std::uint32_t v) {
    p[0] = static_cast<unsigned char>(v);
    p[1] = static_cast<unsigned char>(v >> 8);
    p[2] = static_cast<unsigned char>(v >> 16);
    p[3] = static_cast<unsigned char>(v >> 24);
}

std::uint32_t get_u32le(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::array<unsigned char, kPacketSize>
make_packet(unsigned char type, std::uint32_t id = 0, unsigned char dlc = 0,
            const unsigned char* data = nullptr) {
    std::array<unsigned char, kPacketSize> p{};
    p[0] = 'R';
    p[1] = 'N';
    p[2] = 'C';
    p[3] = '1';
    p[4] = type;
    p[5] = dlc;
    put_u32le(p.data() + 6, id);
    if (data && dlc) {
        std::memcpy(p.data() + 10, data, dlc > 8 ? 8 : dlc);
    }
    return p;
}

bool valid_packet(const unsigned char* p, std::size_t n) {
    return n == kPacketSize &&
           p[0] == 'R' && p[1] == 'N' && p[2] == 'C' && p[3] == '1';
}

void log_gate(const char* message) {
    std::printf("%s: %s\n", kPowerOffGateMarker, message);
    std::fflush(stdout);
}

} // namespace

int main(int argc, char** argv) {
    const std::string ifname = argc > 1 ? argv[1] : "can0";
    const std::uint16_t port =
        argc > 2 ? static_cast<std::uint16_t>(std::strtoul(argv[2], nullptr, 10))
                 : interface_port(ifname);

    const int canfd = ::socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW);
    if (canfd < 0) {
        std::perror("socket(PF_CAN)");
        return 1;
    }

    ifreq ifr{};
    std::snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname.c_str());
    if (::ioctl(canfd, SIOCGIFINDEX, &ifr) < 0) {
        std::perror("SIOCGIFINDEX");
        ::close(canfd);
        return 1;
    }

    sockaddr_can canaddr{};
    canaddr.can_family = AF_CAN;
    canaddr.can_ifindex = ifr.ifr_ifindex;
    if (::bind(canfd, reinterpret_cast<sockaddr*>(&canaddr), sizeof(canaddr)) < 0) {
        std::perror("bind(CAN)");
        ::close(canfd);
        return 1;
    }

    const int udpfd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (udpfd < 0) {
        std::perror("socket(UDP)");
        ::close(canfd);
        return 1;
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = htons(port);
    if (::bind(udpfd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
        std::perror("bind(UDP)");
        ::close(udpfd);
        ::close(canfd);
        return 1;
    }

    std::printf("rnet-can-proxy: %s <-> 127.0.0.1:%u\n",
                ifname.c_str(), static_cast<unsigned>(port));
    std::printf("%s: ACTIVE; 0x002 RTR -> TX SILENT, 0x00C -> TX ACTIVE\n",
                kPowerOffGateMarker);
    std::fflush(stdout);

    sockaddr_in peer{};
    socklen_t peerLen = sizeof(peer);
    bool havePeer = false;

    // R-Net power state rule:
    // - first standard CAN 0x002 RTR is the PowerOff request.
    // - from that instant, the dongle must not transmit anything on CAN.
    // - regular CAN RX continues to be forwarded to the DLL.
    // - standard data frame 0x00C marks a fresh PowerOn and rearms CAN TX.
    bool canTxSilent = false;
    bool dropLogged = false;

    pollfd fds[2]{};
    fds[0].fd = udpfd;
    fds[0].events = POLLIN;
    fds[1].fd = canfd;
    fds[1].events = POLLIN;

    for (;;) {
        const int pr = ::poll(fds, 2, -1);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("poll");
            break;
        }

        // IMPORTANT: process CAN before UDP TX.  If PowerOff and a queued
        // programmer TX become ready in the same poll cycle, the 0x002 RTR
        // must latch TX-silent before any further CAN write is attempted.
        if (fds[1].revents & POLLIN) {
            can_frame f{};
            const ssize_t n = ::read(canfd, &f, sizeof(f));
            if (n == static_cast<ssize_t>(sizeof(f))) {
                if (f.can_id & CAN_ERR_FLAG) {
                    continue;
                }

                const bool isEff = (f.can_id & CAN_EFF_FLAG) != 0;
                const bool isRtr = (f.can_id & CAN_RTR_FLAG) != 0;
                const std::uint32_t id =
                    f.can_id & (isEff ? CAN_EFF_MASK : CAN_SFF_MASK);

                if (!isEff && isRtr && id == 0x002u) {
                    if (!canTxSilent) {
                        log_gate("PowerOff request 0x002 RTR -> CAN TX SILENT");
                    }
                    canTxSilent = true;
                    dropLogged = false;

                    // Preserve the former proxy behaviour: RTR frames are
                    // consumed by the proxy and are not forwarded to the DLL.
                    continue;
                }

                if (!isEff && !isRtr && id == 0x00Cu) {
                    if (canTxSilent) {
                        log_gate("PowerOn frame 0x00C -> CAN TX ACTIVE");
                    }
                    canTxSilent = false;
                    dropLogged = false;
                }

                // Preserve existing behaviour for all other RTR frames.
                if (isRtr) {
                    continue;
                }

                if (havePeer) {
                    const unsigned char dlc = f.can_dlc > 8 ? 8 : f.can_dlc;
                    const auto p = make_packet(kCanRx, id, dlc, f.data);
                    ::sendto(udpfd, p.data(), p.size(), 0,
                             reinterpret_cast<sockaddr*>(&peer), peerLen);
                }
            }
        }

        if (fds[0].revents & POLLIN) {
            std::array<unsigned char, kPacketSize> p{};
            sockaddr_in from{};
            socklen_t fromLen = sizeof(from);
            const ssize_t n =
                ::recvfrom(udpfd, p.data(), p.size(), 0,
                           reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (n > 0 && valid_packet(p.data(), static_cast<std::size_t>(n))) {
                if (p[4] == kHello) {
                    peer = from;
                    peerLen = fromLen;
                    havePeer = true;
                    const auto ack = make_packet(kHelloAck);
                    ::sendto(udpfd, ack.data(), ack.size(), 0,
                             reinterpret_cast<sockaddr*>(&peer), peerLen);
                } else if (p[4] == kCanTx) {
                    const std::uint32_t id = get_u32le(p.data() + 6) & CAN_EFF_MASK;
                    const unsigned char dlc = p[5] > 8 ? 8 : p[5];

                    if (canTxSilent) {
                        if (!dropLogged) {
                            char msg[160]{};
                            std::snprintf(
                                msg, sizeof(msg),
                                "dropping DLL CAN-TX while silent "
                                "(first dropped id=0x%08X dlc=%u)",
                                static_cast<unsigned>(id),
                                static_cast<unsigned>(dlc));
                            log_gate(msg);
                            dropLogged = true;
                        }
                        continue;
                    }

                    can_frame f{};
                    f.can_id = id > CAN_SFF_MASK ? (id | CAN_EFF_FLAG) : id;
                    f.can_dlc = dlc;
                    if (f.can_dlc) {
                        std::memcpy(f.data, p.data() + 10, f.can_dlc);
                    }

                    const ssize_t wr = ::write(canfd, &f, sizeof(f));
                    if (wr != static_cast<ssize_t>(sizeof(f))) {
                        std::perror("write(can)");
                    }
                }
            }
        }
    }

    ::close(udpfd);
    ::close(canfd);
    return 0;
}
