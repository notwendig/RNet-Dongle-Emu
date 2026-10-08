#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

struct CanMessage {
    std::uint32_t id = 0;
    bool extended = false;
    std::uint8_t dlc = 0;
    std::array<std::uint8_t, 8> data{};
};

struct RequestKey {
    std::uint32_t id = 0;
    bool extended = false;
    std::uint8_t dlc = 0;
    std::array<std::uint8_t, 8> data{};

    bool operator<(const RequestKey& rhs) const
    {
        if (id != rhs.id) return id < rhs.id;
        if (extended != rhs.extended) return extended < rhs.extended;
        if (dlc != rhs.dlc) return dlc < rhs.dlc;
        return data < rhs.data;
    }
};

struct ReplayBatch {
    std::vector<CanMessage> responses;
};

struct ReplayEntry {
    std::vector<ReplayBatch> batches;
    std::size_t next = 0;
};

using ReplayMap = std::map<RequestKey, ReplayEntry>;

struct PeriodicFrame {
    CanMessage msg;
    qint64 intervalMs = 0;
    qint64 nextDueMs = 0;
};

QString hexBytes(const std::array<std::uint8_t, 8>& data, unsigned dlc)
{
    QString s;
    for (unsigned i = 0; i < dlc; ++i) {
        if (!s.isEmpty()) s += ' ';
        s += QStringLiteral("%1").arg(data[i], 2, 16, QLatin1Char('0')).toUpper();
    }
    return s;
}

QString canText(const CanMessage& m)
{
    const int width = m.extended ? 8 : 3;
    return QStringLiteral("%1#%2")
        .arg(m.id, width, 16, QLatin1Char('0'))
        .arg(hexBytes(m.data, m.dlc))
        .toUpper();
}

bool parseHex24(QString text, std::array<std::uint8_t, 24>& bytes)
{
    text = text.trimmed();
    if (text.size() != 48) return false;

    for (int i = 0; i < 24; ++i) {
        bool ok = false;
        const int v = text.mid(i * 2, 2).toInt(&ok, 16);
        if (!ok) return false;
        bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v);
    }
    return true;
}

std::optional<CanMessage> decodeFtdiCan(const std::array<std::uint8_t, 24>& frame)
{
    if (frame[0] != 0x10 || frame[1] != 0x02 ||
        frame[22] != 0x10 || frame[23] != 0xFE)
        return std::nullopt;

    const std::uint8_t* p = frame.data() + 2;
    if (p[0] != 0x01) return std::nullopt;

    CanMessage m;
    const std::uint32_t b1 = p[1];
    const std::uint32_t b2 = p[2];
    const std::uint32_t b3 = p[3];
    const std::uint32_t b4 = p[4];

    if ((b2 & 0x08u) != 0) {
        const std::uint32_t temp = (b1 << 24) | (b2 << 16);
        m.id = (((temp & 0xFFE00000u) >> 2) |
                ((b4 | temp) & 0x0007FFFEu) |
                (b3 << 8)) >> 1;
        m.extended = true;
    } else {
        m.id = (b2 >> 5) | (b1 << 3);
        m.extended = false;
    }

    m.dlc = static_cast<std::uint8_t>(p[13] & 0x0Fu);
    if (m.dlc > 8) return std::nullopt;
    std::copy_n(p + 5, m.dlc, m.data.begin());
    return m;
}

RequestKey keyFor(const CanMessage& m)
{
    RequestKey k;
    k.id = m.id;
    k.extended = m.extended;
    k.dlc = m.dlc;
    k.data = m.data;
    return k;
}

bool isStage1PeriodicId(std::uint32_t id)
{
    switch (id) {
    case 0x02000300u:
    case 0x14300000u:
    case 0x140C0001u:
    case 0x0C140000u:
    case 0x0C140100u:
    case 0x1C0C0000u:
    case 0x1C300004u:
        return true;
    default:
        return false;
    }
}

QString findReplayFile(const QString& explicitPath)
{
    QStringList candidates;

    if (!explicitPath.isEmpty())
        candidates << explicitPath;

    const QString env = qEnvironmentVariable("RNET_REPLAY");
    if (!env.isEmpty())
        candidates << env;

    const QString appDir = QCoreApplication::applicationDirPath();
    candidates
        << QDir(appDir).absoluteFilePath("../../rnet-replay.txt")
        << QDir(appDir).absoluteFilePath("../../examples/rnet-replay.txt")
        << QDir(appDir).absoluteFilePath("../../../rnet-replay.txt")
        << QDir(appDir).absoluteFilePath("../../../examples/rnet-replay.txt")
        << QDir(QDir::homePath()).absoluteFilePath("Projects/RNet-Dongle-Emu/rnet-replay.txt")
        << QDir(QDir::homePath()).absoluteFilePath("Projects/RNet-Dongle-Emu/examples/rnet-replay.txt")
        << QDir(QDir::homePath()).absoluteFilePath(
               ".wine/drive_c/Program Files (x86)/PG Drives Technology/"
               "R-net Programmer OEM Generic/rnet-replay.txt");

    candidates.removeDuplicates();

    for (const QString& p : candidates) {
        QFile f(p);
        if (f.exists() && f.size() > 0)
            return QDir::cleanPath(p);
    }
    return {};
}

bool loadReplayMap(const QString& path, ReplayMap& map, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error = QStringLiteral("Replay kann nicht geöffnet werden: %1").arg(path);
        return false;
    }

    QTextStream in(&file);
    ReplayEntry* currentEntry = nullptr;
    ReplayBatch* currentBatch = nullptr;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;

        if (line.startsWith(QStringLiteral("TX "))) {
            currentEntry = nullptr;
            currentBatch = nullptr;

            const QString hex = line.mid(3).trimmed().section(' ', 0, 0);
            std::array<std::uint8_t, 24> raw{};
            if (!parseHex24(hex, raw))
                continue;

            const auto msg = decodeFtdiCan(raw);
            if (!msg)
                continue;

            auto& entry = map[keyFor(*msg)];
            entry.batches.push_back({});
            currentEntry = &entry;
            currentBatch = &entry.batches.back();
            continue;
        }

        if (!line.startsWith(QStringLiteral("RX ")) || !currentEntry || !currentBatch)
            continue;

        const QString rest = line.mid(3).trimmed();
        const QStringList parts = rest.split(QRegularExpression(QStringLiteral("\\s+")),
                                             Qt::SkipEmptyParts);
        if (parts.isEmpty())
            continue;

        const QString hex = parts.back();
        std::array<std::uint8_t, 24> raw{};
        if (!parseHex24(hex, raw))
            continue;

        const auto response = decodeFtdiCan(raw);
        if (!response)
            continue;

        // Stage-1 cyclic traffic is generated independently by rollstuhl.emu.
        // Do not duplicate it as a request response.
        if (response->extended && isStage1PeriodicId(response->id))
            continue;

        currentBatch->responses.push_back(*response);
    }

    for (auto it = map.begin(); it != map.end();) {
        auto& batches = it->second.batches;
        batches.erase(
            std::remove_if(batches.begin(), batches.end(),
                           [](const ReplayBatch& b) { return b.responses.empty(); }),
            batches.end());

        if (batches.empty())
            it = map.erase(it);
        else
            ++it;
    }

    if (map.empty()) {
        error = QStringLiteral("Replay enthält keine verwertbaren CAN-Anfrage/Antwort-Paare.");
        return false;
    }

    return true;
}

int openCanSocket(const QString& iface, QString& error)
{
    const int fd = ::socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW);
    if (fd < 0) {
        error = QStringLiteral("socket(PF_CAN): %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        return -1;
    }

    struct ifreq ifr {};
    const QByteArray name = iface.toLocal8Bit();
    if (name.size() >= IFNAMSIZ) {
        error = QStringLiteral("Interface-Name zu lang: %1").arg(iface);
        ::close(fd);
        return -1;
    }

    std::strncpy(ifr.ifr_name, name.constData(), IFNAMSIZ - 1);
    if (::ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        error = QStringLiteral("SIOCGIFINDEX(%1): %2")
                    .arg(iface, QString::fromLocal8Bit(std::strerror(errno)));
        ::close(fd);
        return -1;
    }

    struct sockaddr_can addr {};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        error = QStringLiteral("bind(%1): %2")
                    .arg(iface, QString::fromLocal8Bit(std::strerror(errno)));
        ::close(fd);
        return -1;
    }

    return fd;
}

bool sendCan(int fd, const CanMessage& m)
{
    struct can_frame f {};
    f.can_id = m.id;
    if (m.extended)
        f.can_id |= CAN_EFF_FLAG;
    f.can_dlc = m.dlc;
    std::copy_n(m.data.begin(), m.dlc, f.data);

    const ssize_t n = ::write(fd, &f, sizeof(f));
    return n == static_cast<ssize_t>(sizeof(f));
}

std::optional<CanMessage> receiveCan(int fd)
{
    struct can_frame f {};
    const ssize_t n = ::read(fd, &f, sizeof(f));
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return std::nullopt;
        return std::nullopt;
    }
    if (n != static_cast<ssize_t>(sizeof(f)))
        return std::nullopt;

    CanMessage m;
    m.extended = (f.can_id & CAN_EFF_FLAG) != 0;
    m.id = m.extended ? (f.can_id & CAN_EFF_MASK) : (f.can_id & CAN_SFF_MASK);
    m.dlc = std::min<std::uint8_t>(f.can_dlc, 8);
    std::copy_n(f.data, m.dlc, m.data.begin());
    return m;
}

CanMessage ext(std::uint32_t id, std::initializer_list<std::uint8_t> bytes)
{
    CanMessage m;
    m.id = id;
    m.extended = true;
    m.dlc = static_cast<std::uint8_t>(std::min<std::size_t>(8, bytes.size()));
    std::copy_n(bytes.begin(), m.dlc, m.data.begin());
    return m;
}

class MainWindow final : public QMainWindow
{
public:
    MainWindow(QString iface, QString replayPath, QWidget* parent = nullptr)
        : QMainWindow(parent),
          iface_(std::move(iface)),
          replayPath_(std::move(replayPath))
    {
        setWindowTitle(QStringLiteral("rollstuhl.emu — %1").arg(iface_));
        resize(680, 470);

        auto* central = new QWidget(this);
        auto* layout = new QVBoxLayout(central);

        auto* title = new QLabel(QStringLiteral("<b>R-Net Rollstuhl-Emulator</b>"), central);
        layout->addWidget(title);

        ifaceLabel_ = new QLabel(QStringLiteral("CAN: %1").arg(iface_), central);
        layout->addWidget(ifaceLabel_);

        replayLabel_ = new QLabel(central);
        replayLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(replayLabel_);

        powerButton_ = new QPushButton(QStringLiteral("Rollstuhl EIN"), central);
        powerButton_->setCheckable(true);
        powerButton_->setChecked(true);
        powerButton_->setMinimumHeight(48);
        layout->addWidget(powerButton_);

        statusLabel_ = new QLabel(central);
        layout->addWidget(statusLabel_);

        countersLabel_ = new QLabel(central);
        layout->addWidget(countersLabel_);

        log_ = new QPlainTextEdit(central);
        log_->setReadOnly(true);
        log_->setMaximumBlockCount(500);
        layout->addWidget(log_, 1);

        setCentralWidget(central);

        connect(powerButton_, &QPushButton::toggled, this, [this](bool on) {
            powerOn_ = on;
            powerButton_->setText(on ? QStringLiteral("Rollstuhl EIN")
                                     : QStringLiteral("Rollstuhl AUS"));
            if (on) {
                armPeriodic();
                appendLog(QStringLiteral("Rollstuhl EIN"));
            } else {
                appendLog(QStringLiteral("Rollstuhl AUS — keine zyklischen Frames, keine Antworten"));
            }
            refreshStatus();
        });

        QString socketError;
        fd_ = openCanSocket(iface_, socketError);
        if (fd_ < 0) {
            appendLog(QStringLiteral("FEHLER: %1").arg(socketError));
            statusLabel_->setText(QStringLiteral("CAN FEHLER"));
            powerButton_->setEnabled(false);
            return;
        }

        notifier_ = new QSocketNotifier(fd_, QSocketNotifier::Read, this);
        connect(notifier_, &QSocketNotifier::activated, this,
                [this](QSocketDescriptor, QSocketNotifier::Type) {
                    drainCan();
                });

        if (!replayPath_.isEmpty()) {
            QString replayError;
            if (loadReplayMap(replayPath_, replay_, replayError)) {
                replayLabel_->setText(
                    QStringLiteral("Replay: %1\n%2 CAN-Anfragemuster")
                        .arg(replayPath_)
                        .arg(replay_.size()));
                appendLog(QStringLiteral("Replay geladen: %1 CAN-Anfragemuster")
                              .arg(replay_.size()));
            } else {
                replayLabel_->setText(QStringLiteral("Replay FEHLER: %1").arg(replayError));
                appendLog(QStringLiteral("Replay FEHLER: %1").arg(replayError));
            }
        } else {
            replayLabel_->setText(
                QStringLiteral("Replay: nicht gefunden — nur EIN/AUS-Zyklik aktiv"));
            appendLog(QStringLiteral("WARNUNG: rnet-replay.txt nicht gefunden"));
        }

        clock_.start();
        setupPeriodic();
        armPeriodic();

        periodicTimer_ = new QTimer(this);
        periodicTimer_->setTimerType(Qt::PreciseTimer);
        periodicTimer_->setInterval(5);
        connect(periodicTimer_, &QTimer::timeout, this, [this] {
            sendDuePeriodic();
        });
        periodicTimer_->start();

        refreshStatus();
    }

    ~MainWindow() override
    {
        if (fd_ >= 0)
            ::close(fd_);
    }

private:
    void setupPeriodic()
    {
        periodic_ = {
            {ext(0x02000300u, {0x00, 0x00}),                                  10, 0},
            {ext(0x14300000u, {0x00, 0x00}),                                 200, 0},
            {ext(0x140C0001u, {0x00, 0x00}),                                 500, 0},
            {ext(0x0C140000u, {0xC0}),                                      1000, 0},
            {ext(0x0C140100u, {0x02}),                                      1000, 0},
            {ext(0x1C0C0000u, {0x28}),                                      1000, 0},
            {ext(0x1C300004u, {0xB7,0xAC,0x13,0x00,0x2F,0xD2,0x06,0x00}),    1000, 0}
        };
    }

    void armPeriodic()
    {
        const qint64 now = clock_.isValid() ? clock_.elapsed() : 0;
        for (auto& p : periodic_)
            p.nextDueMs = now;
    }

    void sendDuePeriodic()
    {
        if (!powerOn_ || fd_ < 0)
            return;

        const qint64 now = clock_.elapsed();
        for (auto& p : periodic_) {
            if (now < p.nextDueMs)
                continue;

            if (sendCan(fd_, p.msg))
                ++txCount_;

            do {
                p.nextDueMs += p.intervalMs;
            } while (p.nextDueMs <= now);
        }

        if ((txCount_ & 0x3Fu) == 0)
            refreshCounters();
    }

    void drainCan()
    {
        if (fd_ < 0)
            return;

        for (;;) {
            const auto msg = receiveCan(fd_);
            if (!msg)
                break;

            ++rxCount_;

            if (!powerOn_)
                continue;

            const auto it = replay_.find(keyFor(*msg));
            if (it == replay_.end())
                continue;

            ReplayEntry& entry = it->second;
            if (entry.batches.empty())
                continue;

            const std::size_t index =
                std::min(entry.next, entry.batches.size() - 1);
            const ReplayBatch& batch = entry.batches[index];
            if (entry.next + 1 < entry.batches.size())
                ++entry.next;

            appendLog(QStringLiteral("RX  %1").arg(canText(*msg)));

            unsigned sent = 0;
            for (const CanMessage& response : batch.responses) {
                if (sendCan(fd_, response)) {
                    ++txCount_;
                    ++sent;
                    appendLog(QStringLiteral("TX  %1").arg(canText(response)));
                } else {
                    appendLog(QStringLiteral("TX FEHLER  %1").arg(canText(response)));
                    break;
                }
            }

            if (sent == 0)
                appendLog(QStringLiteral("Replay-Treffer ohne sendbare Antwort"));
        }

        refreshCounters();
    }

    void refreshStatus()
    {
        if (fd_ < 0) {
            statusLabel_->setText(QStringLiteral("Status: CAN nicht geöffnet"));
            return;
        }

        statusLabel_->setText(
            QStringLiteral("Status: %1 | Antworten: %2")
                .arg(powerOn_ ? QStringLiteral("EIN") : QStringLiteral("AUS"))
                .arg(replay_.empty() ? QStringLiteral("nur Zyklik")
                                     : QStringLiteral("Replay aktiv")));
        refreshCounters();
    }

    void refreshCounters()
    {
        countersLabel_->setText(
            QStringLiteral("RX: %1   TX: %2")
                .arg(rxCount_)
                .arg(txCount_));
    }

    void appendLog(const QString& s)
    {
        log_->appendPlainText(s);
    }

    QString iface_;
    QString replayPath_;

    int fd_ = -1;
    bool powerOn_ = true;
    std::uint64_t rxCount_ = 0;
    std::uint64_t txCount_ = 0;

    ReplayMap replay_;
    std::vector<PeriodicFrame> periodic_;
    QElapsedTimer clock_;

    QSocketNotifier* notifier_ = nullptr;
    QTimer* periodicTimer_ = nullptr;
    QPushButton* powerButton_ = nullptr;
    QLabel* ifaceLabel_ = nullptr;
    QLabel* replayLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* countersLabel_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
};

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QString iface = QStringLiteral("can1");
    QString explicitReplay;

    if (argc >= 2 && argv[1] && *argv[1])
        iface = QString::fromLocal8Bit(argv[1]);

    if (argc >= 3 && argv[2] && *argv[2])
        explicitReplay = QString::fromLocal8Bit(argv[2]);

    const QString replay = findReplayFile(explicitReplay);

    MainWindow w(iface, replay);
    w.show();
    return app.exec();
}
