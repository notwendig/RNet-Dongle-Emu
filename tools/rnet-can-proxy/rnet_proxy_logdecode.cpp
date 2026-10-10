// SPDX-License-Identifier: GPL-3.0-only
// R-Net Dongle Emulator V42
// In-process RNetMsgBroker SocketCAN logger with explicit TX/RX direction.
//
// Direction is derived from SocketCAN recvmsg() flags:
//   MSG_DONTROUTE set   -> locally generated frame -> TX
//   MSG_DONTROUTE clear -> frame received from bus  -> RX
//
// This keeps the logger passive: it does not alter or inject CAN frames.

#include "rnetmsgbroker.h"

#include <QByteArray>
#include <QDateTime>
#include <QFileInfo>
#include <QString>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

#include <limits.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr const char *kMarker = "RNET-PROXY-DECODE-V42";

struct DecodeState {
    RNetMsgBroker broker;
    std::mutex mutex;
    bool ready = false;
};

void writeAll(int fd, const char *data, std::size_t size)
{
    while (size > 0) {
        const ssize_t n = ::write(fd, data, size);
        if (n > 0) {
            data += static_cast<std::size_t>(n);
            size -= static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        break;
    }
}

void writeLine(int fd, const QString &line)
{
    QByteArray utf8 = line.toUtf8();
    utf8.append('\n');
    writeAll(fd, utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

QString executableDir()
{
    char path[PATH_MAX + 1]{};
    const ssize_t n = ::readlink("/proc/self/exe", path, PATH_MAX);
    if (n <= 0 || n > PATH_MAX)
        return QString();
    path[n] = '\0';
    return QFileInfo(QString::fromLocal8Bit(path)).absolutePath();
}

QString defaultJsonPath()
{
    const QByteArray env = qgetenv("RNET_JSON");
    if (!env.isEmpty())
        return QString::fromLocal8Bit(env);

    const QString dir = executableDir();
    if (dir.isEmpty())
        return QString();

    return dir + QStringLiteral("/R-Net.json");
}

QString compactComment(const QString &decoded)
{
    if (decoded.startsWith(QStringLiteral("UNKNOWN"), Qt::CaseInsensitive))
        return QStringLiteral("UNKNOWN");
    return decoded.simplified();
}

QString decodeComment(DecodeState &state, quint32 canId, const QByteArray &data,
                      bool extended, bool remote)
{
    std::lock_guard<std::mutex> lock(state.mutex);
    return compactComment(
        state.broker.toString(canId, data, extended, remote, false));
}

QString frameText(const struct can_frame &frame)
{
    const bool extended = (frame.can_id & CAN_EFF_FLAG) != 0;
    const bool remote = (frame.can_id & CAN_RTR_FLAG) != 0;
    const quint32 id =
        frame.can_id & (extended ? CAN_EFF_MASK : CAN_SFF_MASK);
    const int width = extended ? 8 : 3;

    QString out =
        QStringLiteral("%1#").arg(id, width, 16, QLatin1Char('0')).toUpper();

    if (remote)
        return out + QStringLiteral("R");

    const int dlc = std::min<int>(frame.can_dlc, CAN_MAX_DLEN);
    for (int i = 0; i < dlc; ++i) {
        out += QStringLiteral("%1")
                   .arg(frame.data[i], 2, 16, QLatin1Char('0'))
                   .toUpper();
    }
    return out;
}

void monitorCanBus(DecodeState *state, QByteArray iface)
{
    const int fd = ::socket(PF_CAN, SOCK_RAW | SOCK_CLOEXEC, CAN_RAW);
    if (fd < 0) {
        writeLine(
            STDERR_FILENO,
            QStringLiteral("%1: CAN monitor socket failed: %2")
                .arg(QString::fromLatin1(kMarker),
                     QString::fromLocal8Bit(std::strerror(errno))));
        return;
    }

    struct ifreq ifr {};
    std::snprintf(ifr.ifr_name, IFNAMSIZ, "%s", iface.constData());

    if (::ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        writeLine(
            STDERR_FILENO,
            QStringLiteral("%1: CAN monitor interface %2 failed: %3")
                .arg(QString::fromLatin1(kMarker),
                     QString::fromLocal8Bit(iface),
                     QString::fromLocal8Bit(std::strerror(errno))));
        ::close(fd);
        return;
    }

    struct sockaddr_can addr {};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (::bind(fd, reinterpret_cast<struct sockaddr *>(&addr),
               sizeof(addr)) < 0) {
        writeLine(
            STDERR_FILENO,
            QStringLiteral("%1: CAN monitor bind %2 failed: %3")
                .arg(QString::fromLatin1(kMarker),
                     QString::fromLocal8Bit(iface),
                     QString::fromLocal8Bit(std::strerror(errno))));
        ::close(fd);
        return;
    }

    writeLine(
        STDERR_FILENO,
        QStringLiteral(
            "%1: passive CAN monitor active on %2; direction column=TX/RX")
            .arg(QString::fromLatin1(kMarker),
                 QString::fromLocal8Bit(iface)));

    for (;;) {
        struct can_frame frame {};

        struct iovec iov {};
        iov.iov_base = &frame;
        iov.iov_len = sizeof(frame);

        struct msghdr msg {};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;

        const ssize_t n = ::recvmsg(fd, &msg, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n != static_cast<ssize_t>(sizeof(frame)))
            break;
        if ((frame.can_id & CAN_ERR_FLAG) != 0)
            continue;

        const bool extended = (frame.can_id & CAN_EFF_FLAG) != 0;
        const bool remote = (frame.can_id & CAN_RTR_FLAG) != 0;
        const quint32 id =
            frame.can_id & (extended ? CAN_EFF_MASK : CAN_SFF_MASK);
        const int dlc =
            remote ? 0 : std::min<int>(frame.can_dlc, CAN_MAX_DLEN);
        const QByteArray data(
            reinterpret_cast<const char *>(frame.data), dlc);

        // SocketCAN marks locally generated loopback traffic with
        // MSG_DONTROUTE.  For can0 this is the DLL/proxy -> CAN direction.
        const QString direction =
            (msg.msg_flags & MSG_DONTROUTE)
                ? QStringLiteral("TX")
                : QStringLiteral("RX");

        const QString line =
            QStringLiteral("%1 %2 %3 %4 ; %5")
                .arg(
                    QDateTime::currentDateTime().toString(
                        QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                    direction,
                    QString::fromLocal8Bit(iface),
                    frameText(frame),
                    decodeComment(
                        *state, id, data, extended, remote));

        writeLine(STDERR_FILENO, line);
    }

    ::close(fd);
}

class ProxyLogDecodeInstaller {
public:
    ProxyLogDecodeInstaller()
    {
        const QByteArray disabled = qgetenv("RNET_PROXY_DECODE");
        if (disabled == "0" ||
            disabled.compare("off", Qt::CaseInsensitive) == 0) {
            return;
        }

        // Lifetime intentionally equals the process lifetime.
        auto *state = new DecodeState;

        const QString json = defaultJsonPath();
        QString error;

        if (!json.isEmpty() &&
            QFileInfo(json).isFile() &&
            state->broker.readJson(json, &error)) {
            state->ready = true;

            writeLine(
                STDERR_FILENO,
                QStringLiteral("%1: in-process RNetMsgBroker, json=%2")
                    .arg(QString::fromLatin1(kMarker), json));
        } else {
            writeLine(
                STDERR_FILENO,
                QStringLiteral("%1: R-Net.json nicht geladen: %2 %3")
                    .arg(QString::fromLatin1(kMarker), json, error));
            return;
        }

        const QByteArray iface = qgetenv("RNET_PROXY_CAN");
        if (!iface.isEmpty())
            std::thread(monitorCanBus, state, iface).detach();
    }
};

ProxyLogDecodeInstaller g_proxyLogDecodeInstaller;

} // namespace
