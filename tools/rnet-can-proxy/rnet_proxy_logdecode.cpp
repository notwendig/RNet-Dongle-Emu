// SPDX-License-Identifier: GPL-3.0-only
// R-Net Dongle Emulator V35
// In-process log decoration for rnet-can-proxy using RNetMsgBroker.

#include "rnetmsgbroker.h"

#include <QByteArray>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

namespace {

constexpr const char *kMarker = "RNET-PROXY-DECODE-V35";

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

QString decodeComment(DecodeState &state,
                      quint32 canId,
                      const QByteArray &data,
                      bool extended,
                      bool remote)
{
    std::lock_guard<std::mutex> lock(state.mutex);
    return compactComment(state.broker.toString(canId, data, extended, remote, false));
}

QString decorateLine(const QString &line, DecodeState &state)
{
    if (!state.ready)
        return line;

    static const QRegularExpression frameRe(
        QStringLiteral(R"((?<![0-9A-Fa-f])([0-9A-Fa-f]{1,8})#(R[0-9A-Fa-f]*|[0-9A-Fa-f]*)(?![0-9A-Fa-f]))"));

    const QRegularExpressionMatch match = frameRe.match(line);
    if (!match.hasMatch())
        return line;

    const QString tail = line.mid(match.capturedEnd(0)).trimmed();
    if (tail.startsWith(QLatin1Char(';')))
        return line;

    bool ok = false;
    const quint64 parsedId = match.captured(1).toULongLong(&ok, 16);
    if (!ok || parsedId > 0x1FFFFFFFULL)
        return line;

    const quint32 canId = static_cast<quint32>(parsedId);
    const QString payload = match.captured(2);
    const bool remote = payload.startsWith(QLatin1Char('R'), Qt::CaseInsensitive);
    const bool extended = match.captured(1).size() > 3 || canId > 0x7FFu;

    QByteArray data;
    if (!remote) {
        if ((payload.size() % 2) != 0)
            return line;
        data = QByteArray::fromHex(payload.toLatin1());
        if ((data.size() * 2) != payload.size())
            return line;
    }

    return line + QStringLiteral(" ; ") +
        decodeComment(state, canId, data, extended, remote);
}

void pumpLines(int readFd, int originalFd, DecodeState *state)
{
    std::string pending;
    char buffer[4096];

    for (;;) {
        const ssize_t n = ::read(readFd, buffer, sizeof(buffer));
        if (n > 0) {
            pending.append(buffer, static_cast<std::size_t>(n));
            for (;;) {
                const std::size_t pos = pending.find('\n');
                if (pos == std::string::npos)
                    break;
                std::string raw = pending.substr(0, pos);
                pending.erase(0, pos + 1);
                if (!raw.empty() && raw.back() == '\r')
                    raw.pop_back();
                writeLine(originalFd,
                          decorateLine(QString::fromLocal8Bit(raw.data(),
                                                             static_cast<int>(raw.size())),
                                       *state));
            }
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        break;
    }

    if (!pending.empty()) {
        if (!pending.empty() && pending.back() == '\r')
            pending.pop_back();
        writeLine(originalFd,
                  decorateLine(QString::fromLocal8Bit(pending.data(),
                                                     static_cast<int>(pending.size())),
                               *state));
    }

    ::close(readFd);
    ::close(originalFd);
}

bool redirectFd(int fd, DecodeState *state)
{
    const int originalFd = ::dup(fd);
    if (originalFd < 0)
        return false;

    int p[2] = {-1, -1};
    if (::pipe(p) != 0) {
        ::close(originalFd);
        return false;
    }

    const int flags0 = ::fcntl(p[0], F_GETFD);
    const int flags1 = ::fcntl(p[1], F_GETFD);
    if (flags0 >= 0)
        ::fcntl(p[0], F_SETFD, flags0 | FD_CLOEXEC);
    if (flags1 >= 0)
        ::fcntl(p[1], F_SETFD, flags1 | FD_CLOEXEC);

    if (::dup2(p[1], fd) < 0) {
        ::close(p[0]);
        ::close(p[1]);
        ::close(originalFd);
        return false;
    }
    ::close(p[1]);

    std::thread(pumpLines, p[0], originalFd, state).detach();
    return true;
}

class ProxyLogDecodeInstaller {
public:
    ProxyLogDecodeInstaller()
    {
        const QByteArray disabled = qgetenv("RNET_PROXY_DECODE");
        if (disabled == "0" || disabled.compare("off", Qt::CaseInsensitive) == 0)
            return;

        // Deliberately leaked: detached pump threads may still use this state
        // during normal process shutdown.
        auto *state = new DecodeState;
        const QString json = defaultJsonPath();
        QString error;
        if (!json.isEmpty() && QFileInfo(json).isFile() &&
            state->broker.readJson(json, &error)) {
            state->ready = true;
        } else {
            const QString message = QStringLiteral("%1: R-Net.json nicht geladen: %2 %3\n")
                .arg(QString::fromLatin1(kMarker), json, error);
            const QByteArray raw = message.toLocal8Bit();
            writeAll(STDERR_FILENO, raw.constData(), static_cast<std::size_t>(raw.size()));
        }

        std::fflush(nullptr);
        const bool outOk = redirectFd(STDOUT_FILENO, state);
        const bool errOk = redirectFd(STDERR_FILENO, state);
        if (outOk)
            std::setvbuf(stdout, nullptr, _IOLBF, 0);
        if (errOk)
            std::setvbuf(stderr, nullptr, _IONBF, 0);

        if (outOk || errOk) {
            const QString message = QStringLiteral("%1: in-process RNetMsgBroker, json=%2")
                .arg(QString::fromLatin1(kMarker), json);
            if (errOk)
                writeLine(STDERR_FILENO, message);
            else
                writeLine(STDOUT_FILENO, message);
        }
    }
};

ProxyLogDecodeInstaller g_proxyLogDecodeInstaller;

} // namespace
