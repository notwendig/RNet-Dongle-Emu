#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr std::uint16_t kBasePort = 39000;
constexpr std::size_t kPacketSize = 20;
constexpr unsigned char kHello = 1;
constexpr unsigned char kCanTx = 2;
constexpr unsigned char kCanRx = 3;
constexpr unsigned char kHelloAck = 4;
volatile std::sig_atomic_t g_stop = 0;

void on_signal(int) { g_stop = 1; }

int interface_index(const std::string& name) {
    if (name.rfind("can", 0) != 0 || name.size() <= 3) return -1;
    char* end = nullptr;
    const long n = std::strtol(name.c_str() + 3, &end, 10);
    if (!end || *end != '\0' || n < 0 || n > 999) return -1;
    return static_cast<int>(n);
}

std::uint32_t get_u32le(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

void put_u32le(unsigned char* p, std::uint32_t v) {
    p[0] = static_cast<unsigned char>(v);
    p[1] = static_cast<unsigned char>(v >> 8);
    p[2] = static_cast<unsigned char>(v >> 16);
    p[3] = static_cast<unsigned char>(v >> 24);
}

std::array<unsigned char, kPacketSize> make_packet(
    unsigned char type, std::uint32_t canId, unsigned char dlc,
    const unsigned char* data) {
    std::array<unsigned char, kPacketSize> p{};
    p[0] = 'R'; p[1] = 'N'; p[2] = 'C'; p[3] = '1';
    p[4] = type;
    p[5] = dlc > 8 ? 8 : dlc;
    put_u32le(p.data() + 6, canId & CAN_EFF_MASK);
    if (data && p[5]) std::memcpy(p.data() + 10, data, p[5]);
    return p;
}

bool valid_packet(const unsigned char* p, std::size_t n) {
    return n == kPacketSize && p[0] == 'R' && p[1] == 'N' &&
           p[2] == 'C' && p[3] == '1' && p[5] <= 8;
}

void print_can_frame(const char* tag, const std::string& iface,
                     std::uint32_t id, unsigned char dlc,
                     const unsigned char* data) {
    if (id <= CAN_SFF_MASK)
        std::printf("%s %s %03X#", tag, iface.c_str(), id);
    else
        std::printf("%s %s %08X#", tag, iface.c_str(), id);
    for (unsigned i = 0; i < dlc; ++i)
        std::printf("%02X", data[i]);
    std::putchar('\n');
    std::fflush(stdout);
} // RX-CHAIN-LOGGING
}

int main(int argc, char** argv) {
    const std::string iface = argc > 1 ? argv[1] : "can0";
    const int idx = interface_index(iface);
    if (idx < 0) {
        std::fprintf(stderr, "Usage: %s canN [udp-port]\n", argv[0]);
        return 2;
    }
    const int port = argc > 2 ? std::atoi(argv[2]) : (kBasePort + idx);
    if (port <= 0 || port > 65535) {
        std::fprintf(stderr, "Invalid UDP port\n");
        return 2;
    }

    const int canfd = ::socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW);
    if (canfd < 0) { std::perror("socket(PF_CAN)"); return 1; }

    const unsigned ifindex = if_nametoindex(iface.c_str());
    if (!ifindex) {
        std::fprintf(stderr, "SocketCAN interface not found: %s\n", iface.c_str());
        ::close(canfd);
        return 1;
    }

    sockaddr_can canAddr{};
    canAddr.can_family = AF_CAN;
    canAddr.can_ifindex = static_cast<int>(ifindex);
    if (::bind(canfd, reinterpret_cast<sockaddr*>(&canAddr), sizeof(canAddr)) < 0) {
        std::perror("bind(PF_CAN)");
        ::close(canfd);
        return 1;
    }

    const int udpfd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (udpfd < 0) { std::perror("socket(UDP)"); ::close(canfd); return 1; }

    sockaddr_in udpAddr{};
    udpAddr.sin_family = AF_INET;
    udpAddr.sin_port = htons(static_cast<std::uint16_t>(port));
    udpAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(udpfd, reinterpret_cast<sockaddr*>(&udpAddr), sizeof(udpAddr)) < 0) {
        std::perror("bind(UDP)");
        ::close(udpfd);
        ::close(canfd);
        return 1;
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    sockaddr_in peer{};
    socklen_t peerLen = sizeof(peer);
    bool havePeer = false;

    std::printf("rnet-can-proxy: %s <-> 127.0.0.1:%d\n", iface.c_str(), port);
    std::printf("waiting for FTD2XX Device=%s ...\n", iface.c_str());
    std::fflush(stdout);

    pollfd fds[2]{};
    fds[0].fd = udpfd; fds[0].events = POLLIN;
    fds[1].fd = canfd; fds[1].events = POLLIN;

    while (!g_stop) {
        const int pr = ::poll(fds, 2, 250);
        if (pr < 0) {
            if (errno == EINTR) continue;
            std::perror("poll");
            break;
        }

        if (fds[0].revents & POLLIN) {
            std::array<unsigned char, kPacketSize> p{};
            sockaddr_in from{};
            socklen_t fromLen = sizeof(from);
            const ssize_t n = ::recvfrom(udpfd, p.data(), p.size(), 0,
                                         reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (n > 0 && valid_packet(p.data(), static_cast<std::size_t>(n))) {
                peer = from;
                peerLen = fromLen;
                havePeer = true;

                if (p[4] == kHello) {
                    const auto ack = make_packet(kHelloAck, 0, 0, nullptr);
                    ::sendto(udpfd, ack.data(), ack.size(), 0,
                             reinterpret_cast<sockaddr*>(&peer), peerLen);
                    std::printf("FTD2XX peer connected\n");
                    std::fflush(stdout);
                } else if (p[4] == kCanTx) {
                    can_frame f{};
                    const std::uint32_t id = get_u32le(p.data() + 6) & CAN_EFF_MASK;
                    f.can_id = id > CAN_SFF_MASK ? (id | CAN_EFF_FLAG) : id;
                    f.can_dlc = p[5];
                    if (f.can_dlc) std::memcpy(f.data, p.data() + 10, f.can_dlc);
                    const ssize_t wr = ::write(canfd, &f, sizeof(f));
                    if (wr != static_cast<ssize_t>(sizeof(f))) {
                        std::perror("write(can)");
                    } else {
                        print_can_frame("CAN-TX", iface, id, f.can_dlc, f.data);
                    }
                }
            }
        }

        if (fds[1].revents & POLLIN) {
            can_frame f{};
            const ssize_t n = ::read(canfd, &f, sizeof(f));
            if (n == static_cast<ssize_t>(sizeof(f)) && havePeer) {
                if (f.can_id & (CAN_RTR_FLAG | CAN_ERR_FLAG)) continue;
                const std::uint32_t id = f.can_id & CAN_EFF_MASK;
                const unsigned char dlc = f.can_dlc > 8 ? 8 : f.can_dlc;
                print_can_frame("CAN-RX", iface, id, dlc, f.data); // RX-CHAIN-LOGGING
                const auto p = make_packet(kCanRx, id, dlc, f.data);
                ::sendto(udpfd, p.data(), p.size(), 0,
                         reinterpret_cast<sockaddr*>(&peer), peerLen);
            }
        }
    }

    ::close(udpfd);
    ::close(canfd);
    return 0;
}
