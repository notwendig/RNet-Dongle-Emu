#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointF>
#include <QRectF>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSizePolicy>
#include <QSocketNotifier>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr char kGuiMarker[] = "RNET-CHAIR-CJSM2-GUI-V23";
constexpr int kJsmModule = 3;
constexpr int kMaxProfiles = 5;
constexpr int kMaxModes = 4;

struct CanMessage {
    std::uint32_t id = 0;
    bool extended = false;
    bool remote = false;
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
    if (m.remote) {
        return QStringLiteral("%1#R")
            .arg(m.id, width, 16, QLatin1Char('0'))
            .toUpper();
    }

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

enum class SendCanResult {
    Sent,
    WouldBlock,
    Error
};

SendCanResult sendCan(int fd, const CanMessage& m)
{
    struct can_frame f {};
    f.can_id = m.id;
    if (m.extended)
        f.can_id |= CAN_EFF_FLAG;
    if (m.remote)
        f.can_id |= CAN_RTR_FLAG;
    f.can_dlc = m.dlc;
    std::copy_n(m.data.begin(), m.dlc, f.data);

    const ssize_t n = ::write(fd, &f, sizeof(f));
    if (n == static_cast<ssize_t>(sizeof(f)))
        return SendCanResult::Sent;

    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return SendCanResult::WouldBlock;

    return SendCanResult::Error;
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
    m.remote = (f.can_id & CAN_RTR_FLAG) != 0;
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

CanMessage sff(std::uint32_t id, std::initializer_list<std::uint8_t> bytes)
{
    CanMessage m;
    m.id = id & CAN_SFF_MASK;
    m.extended = false;
    m.dlc = static_cast<std::uint8_t>(std::min<std::size_t>(8, bytes.size()));
    std::copy_n(bytes.begin(), m.dlc, m.data.begin());
    return m;
}

CanMessage sffRtr(std::uint32_t id)
{
    CanMessage m;
    m.id = id & CAN_SFF_MASK;
    m.extended = false;
    m.remote = true;
    m.dlc = 0;
    return m;
}

class JoystickPad final : public QWidget
{
public:
    explicit JoystickPad(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(260, 260);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(QStringLiteral("Joystick"));
    }

    std::function<void(int, int)> onChanged;

    int xValue() const { return x_; }
    int yValue() const { return y_; }

    void setPosition(int x, int y, bool notify = true)
    {
        x = std::clamp(x, -100, 100);
        y = std::clamp(y, -100, 100);
        if (x == x_ && y == y_)
            return;

        x_ = x;
        y_ = y;
        update();
        if (notify && onChanged)
            onChanged(x_, y_);
    }

    void center(bool notify = true)
    {
        setPosition(0, 0, notify);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), QColor(28, 32, 36));

        const QPointF c(width() / 2.0, height() / 2.0);
        const double r = std::max(20.0, std::min(width(), height()) / 2.0 - 18.0);

        p.setPen(QPen(QColor(150, 160, 168), 2));
        p.setBrush(QColor(45, 51, 57));
        p.drawEllipse(c, r, r);

        p.setPen(QPen(QColor(95, 104, 112), 1));
        p.drawLine(QPointF(c.x() - r, c.y()), QPointF(c.x() + r, c.y()));
        p.drawLine(QPointF(c.x(), c.y() - r), QPointF(c.x(), c.y() + r));

        const QPointF knob(c.x() + (x_ / 100.0) * r * 0.72,
                           c.y() + (y_ / 100.0) * r * 0.72);
        p.setPen(QPen(QColor(235, 235, 235), 2));
        p.setBrush(QColor(96, 148, 194));
        p.drawEllipse(knob, 20.0, 20.0);

        p.setPen(QColor(225, 225, 225));
        p.drawText(QRectF(0, 6, width(), 24), Qt::AlignCenter,
                   QStringLiteral("X %1   Y %2").arg(x_).arg(y_));
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton) {
            dragging_ = true;
            updateFromPoint(e->position());
            setFocus();
        }
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (dragging_)
            updateFromPoint(e->position());
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && dragging_) {
            dragging_ = false;
            center();
        }
    }

    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->isAutoRepeat()) {
            e->accept();
            return;
        }

        switch (e->key()) {
        case Qt::Key_Left:  setPosition(-100, 0); break;
        case Qt::Key_Right: setPosition(100, 0); break;
        case Qt::Key_Up:    setPosition(0, -100); break;
        case Qt::Key_Down:  setPosition(0, 100); break;
        case Qt::Key_Space: center(); break;
        default: QWidget::keyPressEvent(e); return;
        }
        e->accept();
    }

    void keyReleaseEvent(QKeyEvent* e) override
    {
        if (!e->isAutoRepeat() &&
            (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right ||
             e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)) {
            center();
            e->accept();
            return;
        }
        QWidget::keyReleaseEvent(e);
    }

private:
    void updateFromPoint(const QPointF& p)
    {
        const QPointF c(width() / 2.0, height() / 2.0);
        const double r = std::max(20.0, std::min(width(), height()) / 2.0 - 18.0);
        double dx = p.x() - c.x();
        double dy = p.y() - c.y();
        const double len = std::hypot(dx, dy);
        if (len > r && len > 0.0) {
            dx = dx / len * r;
            dy = dy / len * r;
        }

        setPosition(static_cast<int>(std::lround(dx / r * 100.0)),
                    static_cast<int>(std::lround(dy / r * 100.0)));
    }

    int x_ = 0;
    int y_ = 0;
    bool dragging_ = false;
};

class MainWindow final : public QMainWindow
{
public:
    MainWindow(QString iface, QString replayPath, QWidget* parent = nullptr)
        : QMainWindow(parent),
          iface_(std::move(iface)),
          replayPath_(std::move(replayPath))
    {
        setWindowTitle(QStringLiteral("rollstuhl.emu — CJSM2 — %1").arg(iface_));
        resize(1180, 820);

        // RNET-CHAIR-POSITION-V18: keep the proven window-position persistence.
        {
            QSettings settings(QStringLiteral("notwendig"),
                               QStringLiteral("rollstuhl.emu"));
            const QString key = QStringLiteral("window/position");
            if (settings.contains(key))
                move(settings.value(key).toPoint());
        }

        buildGui();
        wireControls();

        QString socketError;
        fd_ = openCanSocket(iface_, socketError);
        if (fd_ < 0) {
            appendLog(QStringLiteral("FEHLER: %1").arg(socketError));
            statusLabel_->setText(QStringLiteral("CAN FEHLER"));
            setControlsEnabled(false);
            renderDisplay();
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
                    QStringLiteral("Replay: %1  —  %2 CAN-Anfragemuster")
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

        responseTimer_ = new QTimer(this);
        responseTimer_->setTimerType(Qt::PreciseTimer);
        responseTimer_->setInterval(1);
        connect(responseTimer_, &QTimer::timeout, this, [this] {
            flushTxQueues();
        });

        powerTimer_ = new QTimer(this);
        powerTimer_->setTimerType(Qt::PreciseTimer);
        powerTimer_->setInterval(1);
        connect(powerTimer_, &QTimer::timeout, this, [this] {
            flushPowerSequence();
        });

        displayTimer_ = new QTimer(this);
        displayTimer_->setInterval(250);
        connect(displayTimer_, &QTimer::timeout, this, [this] {
            flashPhase_ = !flashPhase_;
            renderDisplay();
        });
        displayTimer_->start();

        appendLog(QStringLiteral(
            "RNET-CHAIR-REPLAY-V17: Replay-Antworten gepuffert, 1 CAN-Frame/ms"));
        appendLog(QString::fromLatin1(kGuiMarker));
        appendLog(QStringLiteral(
            "CJSM2 GUI: Joystick, beide Paddles, MODE, PROFILE, HORN, 4 Screen-Tasten und externe Jacks"));
        appendLog(QStringLiteral(
            "V23: Startzustand Power OFF; JSM-Y invertiert; PowerOn/PowerOff senden CAN-Sequenzen"));

        refreshStatus();
        renderDisplay();
    }

    ~MainWindow() override
    {
        {
            QSettings settings(QStringLiteral("notwendig"),
                               QStringLiteral("rollstuhl.emu"));
            settings.setValue(QStringLiteral("window/position"), pos());
            settings.sync();
        }

        if (fd_ >= 0)
            ::close(fd_);
    }

private:
    struct QueuedControl {
        CanMessage msg;
        QString label;
    };

    enum class PowerTransition {
        Stable,
        Starting,
        Stopping
    };

    struct ScheduledPowerFrame {
        qint64 dueMs = 0;
        CanMessage msg;
        QString label;
    };

    void buildGui()
    {
        auto* central = new QWidget(this);
        auto* root = new QVBoxLayout(central);
        root->setContentsMargins(10, 10, 10, 10);
        root->setSpacing(8);

        auto* header = new QHBoxLayout;
        auto* title = new QLabel(QStringLiteral("<b>R-Net Rollstuhl-Emulator — CJSM2</b>"), central);
        header->addWidget(title, 1);

        ifaceLabel_ = new QLabel(QStringLiteral("CAN: %1").arg(iface_), central);
        ifaceLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        header->addWidget(ifaceLabel_);
        root->addLayout(header);

        replayLabel_ = new QLabel(central);
        replayLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        replayLabel_->setWordWrap(true);
        root->addWidget(replayLabel_);

        auto* body = new QHBoxLayout;
        body->setSpacing(10);
        root->addLayout(body, 1);

        // ---------------- CJSM2 control panel ----------------
        auto* controllerBox = new QGroupBox(QStringLiteral("CJSM2 Bedienelemente"), central);
        auto* controllerLayout = new QVBoxLayout(controllerBox);
        controllerLayout->setSpacing(8);

        auto* screenRow = new QHBoxLayout;
        auto* leftScreenButtons = new QVBoxLayout;
        auto* rightScreenButtons = new QVBoxLayout;

        screenTopLeft_ = new QPushButton(QStringLiteral("WARNBLINKER\n1 s: SETTINGS"), controllerBox);
        screenBottomLeft_ = new QPushButton(QStringLiteral("BLINKER\nLINKS"), controllerBox);
        screenTopRight_ = new QPushButton(QStringLiteral("LICHT"), controllerBox);
        screenBottomRight_ = new QPushButton(QStringLiteral("BLINKER\nRECHTS"), controllerBox);

        for (QPushButton* b : {screenTopLeft_, screenBottomLeft_, screenTopRight_, screenBottomRight_}) {
            b->setMinimumSize(112, 72);
            b->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        }

        screenBottomLeft_->setCheckable(true);
        screenTopRight_->setCheckable(true);
        screenBottomRight_->setCheckable(true);

        leftScreenButtons->addWidget(screenTopLeft_);
        leftScreenButtons->addStretch(1);
        leftScreenButtons->addWidget(screenBottomLeft_);

        rightScreenButtons->addWidget(screenTopRight_);
        rightScreenButtons->addStretch(1);
        rightScreenButtons->addWidget(screenBottomRight_);

        scene_ = new QGraphicsScene(this);
        scene_->setSceneRect(0, 0, 500, 320);
        displayView_ = new QGraphicsView(scene_, controllerBox);
        displayView_->setMinimumSize(500, 330);
        displayView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        displayView_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        displayView_->setRenderHint(QPainter::Antialiasing, true);
        displayView_->setAccessibleName(QStringLiteral("CJSM2 LCD Display"));

        screenRow->addLayout(leftScreenButtons);
        screenRow->addWidget(displayView_, 1);
        screenRow->addLayout(rightScreenButtons);
        controllerLayout->addLayout(screenRow, 1);

        auto* centerButtons = new QHBoxLayout;
        modeButton_ = new QPushButton(QStringLiteral("MODE"), controllerBox);
        hornButton_ = new QPushButton(QStringLiteral("HORN"), controllerBox);
        profileButton_ = new QPushButton(QStringLiteral("PROFILE"), controllerBox);
        for (QPushButton* b : {modeButton_, hornButton_, profileButton_})
            b->setMinimumHeight(52);
        centerButtons->addWidget(modeButton_);
        centerButtons->addWidget(hornButton_);
        centerButtons->addWidget(profileButton_);
        controllerLayout->addLayout(centerButtons);

        auto* paddleRow = new QHBoxLayout;
        onOffPaddle_ = new QPushButton(QStringLiteral("LINKES PADDLE\nEIN / AUS"), controllerBox);
        profileModePaddle_ = new QPushButton(QStringLiteral("LINKES PADDLE\nPROFIL / MODE"), controllerBox);
        speedDownButton_ = new QPushButton(QStringLiteral("RECHTES PADDLE\nSPEED −"), controllerBox);
        speedUpButton_ = new QPushButton(QStringLiteral("RECHTES PADDLE\nSPEED +"), controllerBox);
        for (QPushButton* b : {onOffPaddle_, profileModePaddle_, speedDownButton_, speedUpButton_})
            b->setMinimumHeight(58);
        paddleRow->addWidget(onOffPaddle_);
        paddleRow->addWidget(profileModePaddle_);
        paddleRow->addWidget(speedDownButton_);
        paddleRow->addWidget(speedUpButton_);
        controllerLayout->addLayout(paddleRow);

        body->addWidget(controllerBox, 3);

        // ---------------- Joystick and external jack inputs ----------------
        auto* inputBox = new QGroupBox(QStringLiteral("Joystick / externe Eingänge"), central);
        auto* inputLayout = new QVBoxLayout(inputBox);
        joystick_ = new JoystickPad(inputBox);
        inputLayout->addWidget(joystick_, 1);

        auto* joyButtons = new QGridLayout;
        joyUpButton_ = new QPushButton(QStringLiteral("↑"), inputBox);
        joyLeftButton_ = new QPushButton(QStringLiteral("←"), inputBox);
        joyCenterButton_ = new QPushButton(QStringLiteral("CENTER"), inputBox);
        joyRightButton_ = new QPushButton(QStringLiteral("→"), inputBox);
        joyDownButton_ = new QPushButton(QStringLiteral("↓"), inputBox);
        for (QPushButton* b : {joyUpButton_, joyLeftButton_, joyCenterButton_, joyRightButton_, joyDownButton_})
            b->setMinimumSize(72, 52);
        joyButtons->addWidget(joyUpButton_, 0, 1);
        joyButtons->addWidget(joyLeftButton_, 1, 0);
        joyButtons->addWidget(joyCenterButton_, 1, 1);
        joyButtons->addWidget(joyRightButton_, 1, 2);
        joyButtons->addWidget(joyDownButton_, 2, 1);
        inputLayout->addLayout(joyButtons);

        auto* externalLabel = new QLabel(QStringLiteral("Externe Jack-Sockets"), inputBox);
        externalLabel->setAlignment(Qt::AlignCenter);
        inputLayout->addWidget(externalLabel);

        externalOnOffButton_ = new QPushButton(QStringLiteral("EXTERN EIN / AUS"), inputBox);
        externalProfileModeButton_ = new QPushButton(QStringLiteral("EXTERN PROFIL / MODE"), inputBox);
        externalOnOffButton_->setMinimumHeight(50);
        externalProfileModeButton_->setMinimumHeight(50);
        inputLayout->addWidget(externalOnOffButton_);
        inputLayout->addWidget(externalProfileModeButton_);

        body->addWidget(inputBox, 1);

        auto* statusRow = new QHBoxLayout;
        statusLabel_ = new QLabel(central);
        countersLabel_ = new QLabel(central);
        countersLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        statusRow->addWidget(statusLabel_, 1);
        statusRow->addWidget(countersLabel_);
        root->addLayout(statusRow);

        log_ = new QPlainTextEdit(central);
        log_->setReadOnly(true);
        log_->setMaximumBlockCount(500);
        log_->setMaximumHeight(145);
        root->addWidget(log_);

        setCentralWidget(central);
    }

    void wireControls()
    {
        connect(onOffPaddle_, &QPushButton::clicked, this, [this] {
            setPowerState(!powerOn_, QStringLiteral("Ein/Aus-Paddle"));
        });
        connect(externalOnOffButton_, &QPushButton::clicked, this, [this] {
            setPowerState(!powerOn_, QStringLiteral("externer Ein/Aus-Schalter"));
        });

        connect(profileModePaddle_, &QPushButton::clicked, this, [this] {
            cycleProfileMode(QStringLiteral("Profil/Mode-Paddle"));
        });
        connect(externalProfileModeButton_, &QPushButton::clicked, this, [this] {
            cycleProfileMode(QStringLiteral("externer Profil/Mode-Schalter"));
        });

        connect(profileButton_, &QPushButton::clicked, this, [this] {
            selectNextProfile(QStringLiteral("PROFILE"));
        });
        connect(modeButton_, &QPushButton::clicked, this, [this] {
            selectNextMode(QStringLiteral("MODE"));
        });

        connect(speedUpButton_, &QPushButton::clicked, this, [this] {
            setSpeedSetting(std::min(100, speedSetting_ + 25));
        });
        connect(speedDownButton_, &QPushButton::clicked, this, [this] {
            setSpeedSetting(std::max(0, speedSetting_ - 25));
        });

        connect(hornButton_, &QPushButton::pressed, this, [this] {
            enqueueControl(ext(0x0C040300u, {}), QStringLiteral("Horn EIN"));
            hornActive_ = true;
            renderDisplay();
        });
        connect(hornButton_, &QPushButton::released, this, [this] {
            enqueueControl(ext(0x0C040301u, {}), QStringLiteral("Horn AUS"));
            hornActive_ = false;
            renderDisplay();
        });

        connect(screenTopLeft_, &QPushButton::pressed, this, [this] {
            topLeftHold_.restart();
        });
        connect(screenTopLeft_, &QPushButton::released, this, [this] {
            const qint64 held = topLeftHold_.isValid() ? topLeftHold_.elapsed() : 0;
            if (held >= 1000) {
                settingsOpen_ = !settingsOpen_;
                settingsIndex_ = 0;
                settingsLastDirection_ = 0;
                joystick_->center(false);
                joystickX_ = 0;
                joystickY_ = 0;
                updateJoystickPeriodic();
                appendLog(settingsOpen_ ? QStringLiteral("Settings-Menü geöffnet")
                                        : QStringLiteral("Settings-Menü geschlossen"));
                renderDisplay();
                return;
            }
            hazardsOn_ = !hazardsOn_;
            enqueueControl(ext(0x0C000403u, {}),
                           hazardsOn_ ? QStringLiteral("Warnblinker EIN")
                                      : QStringLiteral("Warnblinker AUS"));
            updateControlStyles();
            renderDisplay();
        });

        connect(screenTopRight_, &QPushButton::toggled, this, [this](bool on) {
            lightsOn_ = on;
            enqueueControl(ext(0x0C000404u, {}),
                           on ? QStringLiteral("Licht EIN") : QStringLiteral("Licht AUS"));
            updateControlStyles();
            renderDisplay();
        });
        connect(screenBottomLeft_, &QPushButton::toggled, this, [this](bool on) {
            leftIndicatorOn_ = on;
            enqueueControl(ext(0x0C000401u, {}),
                           on ? QStringLiteral("Blinker links EIN")
                              : QStringLiteral("Blinker links AUS"));
            updateControlStyles();
            renderDisplay();
        });
        connect(screenBottomRight_, &QPushButton::toggled, this, [this](bool on) {
            rightIndicatorOn_ = on;
            enqueueControl(ext(0x0C000402u, {}),
                           on ? QStringLiteral("Blinker rechts EIN")
                              : QStringLiteral("Blinker rechts AUS"));
            updateControlStyles();
            renderDisplay();
        });

        joystick_->onChanged = [this](int x, int y) {
            handleJoystick(x, y);
        };

        connect(joyCenterButton_, &QPushButton::clicked, this, [this] {
            joystick_->center();
        });
        connect(joyUpButton_, &QPushButton::pressed, this, [this] {
            joystick_->setPosition(0, -100);
        });
        connect(joyDownButton_, &QPushButton::pressed, this, [this] {
            joystick_->setPosition(0, 100);
        });
        connect(joyLeftButton_, &QPushButton::pressed, this, [this] {
            joystick_->setPosition(-100, 0);
        });
        connect(joyRightButton_, &QPushButton::pressed, this, [this] {
            joystick_->setPosition(100, 0);
        });
        for (QPushButton* b : {joyUpButton_, joyDownButton_, joyLeftButton_, joyRightButton_}) {
            connect(b, &QPushButton::released, this, [this] {
                joystick_->center();
            });
        }

        updateControlStyles();
    }

    void setControlsEnabled(bool enabled)
    {
        for (QPushButton* b : {
                 onOffPaddle_, profileModePaddle_, speedDownButton_, speedUpButton_,
                 modeButton_, profileButton_, hornButton_, screenTopLeft_,
                 screenBottomLeft_, screenTopRight_, screenBottomRight_,
                 externalOnOffButton_, externalProfileModeButton_, joyUpButton_,
                 joyDownButton_, joyLeftButton_, joyRightButton_, joyCenterButton_}) {
            if (b) b->setEnabled(enabled);
        }
        if (joystick_) joystick_->setEnabled(enabled);
    }

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
        updateJoystickPeriodic();
    }

    void updateJoystickPeriodic()
    {
        for (auto& p : periodic_) {
            if (p.msg.id != 0x02000300u)
                continue;
            p.msg.dlc = 2;
            p.msg.data[0] = static_cast<std::uint8_t>(static_cast<std::int8_t>(joystickX_));
            // Qt screen coordinates grow downwards. R-Net JSM Y is the opposite:
            // forward/up must be positive on CAN, backward/down negative.
            const int canY = -joystickY_;
            p.msg.data[1] = static_cast<std::uint8_t>(static_cast<std::int8_t>(canY));
            return;
        }
    }

    void armPeriodic()
    {
        const qint64 now = clock_.isValid() ? clock_.elapsed() : 0;
        for (auto& p : periodic_)
            p.nextDueMs = now;
    }

    void sendDuePeriodic()
    {
        if (!powerOn_ || powerTransition_ != PowerTransition::Stable || fd_ < 0)
            return;

        // Replay-Antworten and GUI control events have priority over the 10-ms
        // cyclic traffic. This preserves the proven V17 read/write pacing.
        if (!responseQueue_.empty() || !controlQueue_.empty())
            return;

        const qint64 now = clock_.elapsed();
        for (auto& p : periodic_) {
            if (now < p.nextDueMs)
                continue;

            if (sendCan(fd_, p.msg) == SendCanResult::Sent)
                ++txCount_;

            do {
                p.nextDueMs += p.intervalMs;
            } while (p.nextDueMs <= now);
        }

        if ((txCount_ & 0x3Fu) == 0)
            refreshCounters();
    }

    void resetReplayCursors()
    {
        for (auto& [key, entry] : replay_) {
            Q_UNUSED(key);
            entry.next = 0;
        }
    }

    void enqueueControl(const CanMessage& msg, const QString& label)
    {
        if (!powerOn_ || powerTransition_ != PowerTransition::Stable || fd_ < 0) {
            appendLog(QStringLiteral("GUI ignoriert (Power-Zustand nicht stabil): %1").arg(label));
            return;
        }

        controlQueue_.push_back({msg, label});
        if (responseTimer_ && !responseTimer_->isActive())
            responseTimer_->start();
        refreshCounters();
    }

    void flushTxQueues()
    {
        if (!powerOn_ || powerTransition_ != PowerTransition::Stable || fd_ < 0) {
            if (responseTimer_)
                responseTimer_->stop();
            return;
        }

        // Keep programmer replay responses at highest priority so Read/Write
        // from R-Net retains the V17 behavior. GUI events follow afterwards.
        if (!responseQueue_.empty()) {
            const CanMessage& response = responseQueue_.front();
            const SendCanResult result = sendCan(fd_, response);

            if (result == SendCanResult::WouldBlock)
                return;

            if (result == SendCanResult::Error) {
                appendLog(QStringLiteral("TX FEHLER  %1 (%2)")
                              .arg(canText(response),
                                   QString::fromLocal8Bit(std::strerror(errno))));
                responseQueue_.pop_front();
            } else {
                ++txCount_;
                appendLog(QStringLiteral("TX  %1").arg(canText(response)));
                responseQueue_.pop_front();
            }
            refreshCounters();
            return;
        }

        if (!controlQueue_.empty()) {
            const QueuedControl& q = controlQueue_.front();
            const SendCanResult result = sendCan(fd_, q.msg);

            if (result == SendCanResult::WouldBlock)
                return;

            if (result == SendCanResult::Error) {
                appendLog(QStringLiteral("GUI TX FEHLER  %1  %2 (%3)")
                              .arg(q.label, canText(q.msg),
                                   QString::fromLocal8Bit(std::strerror(errno))));
                controlQueue_.pop_front();
            } else {
                ++txCount_;
                appendLog(QStringLiteral("GUI TX  %1  %2")
                              .arg(q.label, canText(q.msg)));
                controlQueue_.pop_front();
            }
            refreshCounters();
            return;
        }

        if (responseTimer_)
            responseTimer_->stop();
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

            if (!powerOn_ || powerTransition_ != PowerTransition::Stable)
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

            for (const CanMessage& response : batch.responses)
                responseQueue_.push_back(response);

            if (!batch.responses.empty() && responseTimer_ && !responseTimer_->isActive())
                responseTimer_->start();

            if (batch.responses.empty()) {
                appendLog(QStringLiteral("Replay-Treffer ohne direkte CAN-Antwort"));
            } else {
                appendLog(QStringLiteral("Replay: %1 Antwort(en) eingeplant, Queue=%2")
                              .arg(batch.responses.size())
                              .arg(responseQueue_.size()));
            }
        }

        refreshCounters();
    }

    void schedulePowerFrame(qint64 delayMs, const CanMessage& msg, const QString& label)
    {
        powerSequence_.push_back({powerSequenceStartMs_ + delayMs, msg, label});
    }

    void beginPowerOnSequence(const QString& source)
    {
        if (fd_ < 0 || powerTransition_ != PowerTransition::Stable || powerOn_)
            return;

        responseQueue_.clear();
        controlQueue_.clear();
        resetReplayCursors();
        joystick_->center(false);
        joystickX_ = 0;
        joystickY_ = 0;
        updateJoystickPeriodic();

        powerTransition_ = PowerTransition::Starting;
        powerSequence_.clear();
        powerSequenceStartMs_ = clock_.elapsed();

        // Captured CJSM start-up prefix from candump_OhneDongle_OnOff.log.
        // The serial/network discovery frames intentionally precede normal cyclic
        // traffic; after 0x7B0 the regular V17/V20 cyclic set is armed again.
        schedulePowerFrame(0,   sff(0x00Cu, {}), QStringLiteral("PowerOn 00C#"));
        schedulePowerFrame(21,  sff(0x00Eu, {0x6E,0x46,0x21,0xDF,0x00,0x00,0x00,0x00}),
                           QStringLiteral("PowerOn 00E# unique-id"));
        schedulePowerFrame(21,  sff(0x7B3u, {}), QStringLiteral("PowerOn 7B3#"));
        schedulePowerFrame(42,  ext(0x1F00616Eu, {}), QStringLiteral("PowerOn serial 0"));
        schedulePowerFrame(43,  ext(0x1F100D46u, {}), QStringLiteral("PowerOn serial 1"));
        schedulePowerFrame(44,  ext(0x1F200E21u, {}), QStringLiteral("PowerOn serial 2"));
        schedulePowerFrame(45,  ext(0x1F3021DFu, {}), QStringLiteral("PowerOn serial 3"));
        schedulePowerFrame(46,  ext(0x1F405000u, {}), QStringLiteral("PowerOn serial 4"));
        schedulePowerFrame(47,  ext(0x1F50E200u, {}), QStringLiteral("PowerOn serial 5"));
        schedulePowerFrame(48,  ext(0x1F603E00u, {}), QStringLiteral("PowerOn serial 6"));
        schedulePowerFrame(49,  ext(0x1F70D300u, {}), QStringLiteral("PowerOn serial 7"));
        schedulePowerFrame(84,  sff(0x00Eu, {0x6E,0x46,0x21,0xDF,0x00,0x00,0x00,0x00}),
                           QStringLiteral("PowerOn 00E#"));
        schedulePowerFrame(84,  sff(0x7B3u, {}), QStringLiteral("PowerOn 7B3#"));
        schedulePowerFrame(127, sff(0x00Eu, {0x6E,0x46,0x21,0xDF,0x00,0x00,0x00,0x00}),
                           QStringLiteral("PowerOn 00E#"));
        schedulePowerFrame(127, sffRtr(0x7B3u), QStringLiteral("PowerOn 7B3#R"));
        schedulePowerFrame(171, sff(0x00Eu, {0x6E,0x46,0x21,0xDF,0x00,0x00,0x00,0x00}),
                           QStringLiteral("PowerOn 00E#"));
        schedulePowerFrame(234, sff(0x00Eu, {0x6E,0x46,0x21,0xDF,0x00,0x00,0x00,0x00}),
                           QStringLiteral("PowerOn 00E#"));
        schedulePowerFrame(276, sff(0x00Eu, {0x6E,0x46,0x21,0xDF,0x00,0x00,0x00,0x00}),
                           QStringLiteral("PowerOn 00E#"));
        schedulePowerFrame(318, sff(0x00Eu, {0x6E,0x46,0x21,0xDF,0x00,0x00,0x00,0x00}),
                           QStringLiteral("PowerOn 00E#"));
        schedulePowerFrame(339, sff(0x7B1u, {}), QStringLiteral("PowerOn 7B1#"));
        schedulePowerFrame(383, sff(0x00Eu, {0x6E,0x46,0x21,0xDF,0x00,0x00,0x00,0x00}),
                           QStringLiteral("PowerOn 00E#"));
        schedulePowerFrame(383, sff(0x7B0u, {}), QStringLiteral("PowerOn 7B0#"));

        appendLog(QStringLiteral("Rollstuhl EIN-Sequenz gestartet — %1").arg(source));
        if (powerTimer_ && !powerTimer_->isActive())
            powerTimer_->start();
        updateControlStyles();
        refreshStatus();
        renderDisplay();
    }

    void beginPowerOffSequence(const QString& source)
    {
        if (fd_ < 0 || powerTransition_ != PowerTransition::Stable || !powerOn_)
            return;

        responseQueue_.clear();
        controlQueue_.clear();
        if (responseTimer_)
            responseTimer_->stop();

        joystick_->center(false);
        joystickX_ = 0;
        joystickY_ = 0;
        updateJoystickPeriodic();
        hornActive_ = false;

        powerTransition_ = PowerTransition::Stopping;
        powerSequence_.clear();
        powerSequenceStartMs_ = clock_.elapsed();

        // Real capture: after the first PowerOff request the active JSM/PM sleep
        // exchange continues for about 11.05 s. 0x002 RTR requests sleep and
        // 0x004 announces JSM sleep commencing. The sequence terminates in
        // 0x000 data frames followed by 0x000 RTR.
        constexpr qint64 kSleepDurationMs = 11048;
        constexpr qint64 kSleepPeriodMs = 63;
        qint64 sleepCycle = 0;
        for (qint64 t = 0; t < kSleepDurationMs - 60; t += kSleepPeriodMs, ++sleepCycle) {
            if ((sleepCycle & 1) == 0)
                schedulePowerFrame(t, sff(0x002u, {}), QStringLiteral("PowerOff 002#"));
            else
                schedulePowerFrame(t, sffRtr(0x002u), QStringLiteral("PowerOff 002#R"));
            schedulePowerFrame(t + 21, sff(0x004u, {}), QStringLiteral("PowerOff 004#"));
        }

        schedulePowerFrame(kSleepDurationMs,     sff(0x000u, {}), QStringLiteral("PowerOff 000#"));
        schedulePowerFrame(kSleepDurationMs + 1, sff(0x000u, {}), QStringLiteral("PowerOff 000#"));
        schedulePowerFrame(kSleepDurationMs + 2, sff(0x000u, {}), QStringLiteral("PowerOff 000#"));
        schedulePowerFrame(kSleepDurationMs + 3, sffRtr(0x000u), QStringLiteral("PowerOff 000#R"));

        appendLog(QStringLiteral("Rollstuhl AUS-Sequenz gestartet — %1 — normale Zyklik gestoppt")
                      .arg(source));
        if (powerTimer_ && !powerTimer_->isActive())
            powerTimer_->start();
        updateControlStyles();
        refreshStatus();
        renderDisplay();
    }

    void finishPowerOnSequence()
    {
        powerOn_ = true;
        powerTransition_ = PowerTransition::Stable;
        powerSequence_.clear();
        resetReplayCursors();
        armPeriodic();
        appendLog(QStringLiteral("Rollstuhl EIN — PowerOn-Sequenz beendet, Zyklik aktiv"));
        updateControlStyles();
        refreshStatus();
        renderDisplay();
    }

    void finishPowerOffSequence()
    {
        powerOn_ = false;
        powerTransition_ = PowerTransition::Stable;
        powerSequence_.clear();
        responseQueue_.clear();
        controlQueue_.clear();
        appendLog(QStringLiteral("Rollstuhl AUS — PowerOff-Sequenz beendet, CAN jetzt still"));
        updateControlStyles();
        refreshStatus();
        renderDisplay();
    }

    void flushPowerSequence()
    {
        if (fd_ < 0 || powerTransition_ == PowerTransition::Stable) {
            if (powerTimer_)
                powerTimer_->stop();
            return;
        }

        const qint64 now = clock_.elapsed();
        if (powerSequence_.empty()) {
            if (powerTimer_)
                powerTimer_->stop();
            if (powerTransition_ == PowerTransition::Starting)
                finishPowerOnSequence();
            else if (powerTransition_ == PowerTransition::Stopping)
                finishPowerOffSequence();
            return;
        }

        if (now < powerSequence_.front().dueMs)
            return;

        const ScheduledPowerFrame& q = powerSequence_.front();
        const SendCanResult result = sendCan(fd_, q.msg);
        if (result == SendCanResult::WouldBlock)
            return;

        if (result == SendCanResult::Error) {
            appendLog(QStringLiteral("POWER TX FEHLER  %1  %2 (%3)")
                          .arg(q.label, canText(q.msg),
                               QString::fromLocal8Bit(std::strerror(errno))));
        } else {
            ++txCount_;
            appendLog(QStringLiteral("POWER TX  %1  %2").arg(q.label, canText(q.msg)));
        }
        powerSequence_.pop_front();
        refreshCounters();
    }

    void setPowerState(bool on, const QString& source)
    {
        if (powerTransition_ != PowerTransition::Stable) {
            appendLog(QStringLiteral("Power-Taste ignoriert: Sequenz läuft bereits"));
            return;
        }

        if (on == powerOn_)
            return;

        if (on)
            beginPowerOnSequence(source);
        else
            beginPowerOffSequence(source);
    }

    void selectNextProfile(const QString& source)
    {
        if (!powerOn_)
            return;
        profile_ = (profile_ % kMaxProfiles) + 1;
        pmInModes_ = false;
        const std::uint8_t value = static_cast<std::uint8_t>(0x40 | (profile_ & 0x0F));
        enqueueControl(sff(0x051u, {0x00, value, 0x00, 0x00}),
                       QStringLiteral("%1 -> Profil %2").arg(source).arg(profile_));
        refreshStatus();
        renderDisplay();
    }

    void selectNextMode(const QString& source)
    {
        if (!powerOn_)
            return;
        mode_ = (mode_ % kMaxModes) + 1;
        pmInModes_ = true;
        const std::uint8_t value = static_cast<std::uint8_t>(0x40 | (mode_ & 0x0F));
        enqueueControl(sff(0x061u, {0x00, value, 0x00, 0x00}),
                       QStringLiteral("%1 -> Mode %2").arg(source).arg(mode_));
        refreshStatus();
        renderDisplay();
    }

    void cycleProfileMode(const QString& source)
    {
        if (!pmInModes_) {
            if (profile_ < kMaxProfiles) {
                selectNextProfile(source);
                return;
            }
            pmInModes_ = true;
            mode_ = 0;
        }

        if (mode_ < kMaxModes) {
            selectNextMode(source);
            return;
        }

        pmInModes_ = false;
        profile_ = 0;
        selectNextProfile(source);
    }

    void setSpeedSetting(int value)
    {
        value = std::clamp(value, 0, 100);
        if (value == speedSetting_)
            return;
        speedSetting_ = value;
        enqueueControl(ext(0x0A040300u,
                           {static_cast<std::uint8_t>(speedSetting_)}),
                       QStringLiteral("Max Speed %1%").arg(speedSetting_));
        refreshStatus();
        renderDisplay();
    }

    void handleJoystick(int x, int y)
    {
        if (settingsOpen_) {
            handleSettingsJoystick(x, y);
            joystickX_ = 0;
            joystickY_ = 0;
            updateJoystickPeriodic();
            return;
        }

        joystickX_ = std::clamp(x, -100, 100);
        joystickY_ = std::clamp(y, -100, 100);
        updateJoystickPeriodic();
        renderDisplay();
    }

    void handleSettingsJoystick(int x, int y)
    {
        int direction = 0;
        if (y <= -60) direction = -1;
        else if (y >= 60) direction = 1;
        else if (x >= 60) direction = 2;
        else if (x <= -60) direction = -2;

        if (direction == 0) {
            settingsLastDirection_ = 0;
            return;
        }
        if (direction == settingsLastDirection_)
            return;
        settingsLastDirection_ = direction;

        const int itemCount = settingsItems_.size();
        if (direction == -1) {
            settingsIndex_ = (settingsIndex_ + itemCount - 1) % itemCount;
        } else if (direction == 1) {
            settingsIndex_ = (settingsIndex_ + 1) % itemCount;
        } else if (direction == -2) {
            settingsOpen_ = false;
            appendLog(QStringLiteral("Settings-Menü geschlossen"));
        } else if (direction == 2) {
            if (settingsItems_[settingsIndex_] == QStringLiteral("Exit")) {
                settingsOpen_ = false;
                appendLog(QStringLiteral("Settings-Menü: Exit"));
            } else {
                appendLog(QStringLiteral("Settings-Menü: %1 ausgewählt (Anzeige-Gerüst)")
                              .arg(settingsItems_[settingsIndex_]));
            }
        }
        renderDisplay();
    }

    void updateControlStyles()
    {
        const QString active = QStringLiteral("font-weight: bold; background: #356c44; color: white;");
        const QString inactive;

        screenTopLeft_->setStyleSheet(hazardsOn_ ? active : inactive);
        screenTopRight_->setStyleSheet(lightsOn_ ? active : inactive);
        screenBottomLeft_->setStyleSheet(leftIndicatorOn_ ? active : inactive);
        screenBottomRight_->setStyleSheet(rightIndicatorOn_ ? active : inactive);

        QString powerStyle;
        if (powerTransition_ != PowerTransition::Stable) {
            powerStyle = QStringLiteral("font-weight: bold; background: #8a6a24; color: white;");
        } else {
            powerStyle = powerOn_
                ? QStringLiteral("font-weight: bold; background: #2f7042; color: white;")
                : QStringLiteral("font-weight: bold; background: #6d3333; color: white;");
        }
        onOffPaddle_->setStyleSheet(powerStyle);
        externalOnOffButton_->setStyleSheet(powerStyle);
    }

    void addSceneText(const QString& text, const QPointF& pos, int px,
                      const QColor& color = Qt::white, bool bold = false)
    {
        QFont font;
        font.setPixelSize(px);
        font.setBold(bold);
        QGraphicsTextItem* item = scene_->addText(text, font);
        item->setDefaultTextColor(color);
        item->setPos(pos);
    }

    void renderDisplay()
    {
        if (!scene_)
            return;

        scene_->clear();
        scene_->setBackgroundBrush(QColor(12, 15, 18));

        if (powerTransition_ == PowerTransition::Starting) {
            addSceneText(QStringLiteral("R-NET"), QPointF(205, 112), 26, QColor(150, 158, 165), true);
            addSceneText(QStringLiteral("STARTING"), QPointF(190, 154), 22, QColor(210, 170, 65), true);
            return;
        }
        if (powerTransition_ == PowerTransition::Stopping) {
            addSceneText(QStringLiteral("R-NET"), QPointF(205, 112), 26, QColor(150, 158, 165), true);
            addSceneText(QStringLiteral("SHUTDOWN"), QPointF(182, 154), 22, QColor(210, 170, 65), true);
            return;
        }

        if (!powerOn_) {
            addSceneText(QStringLiteral("R-NET"), QPointF(205, 112), 26, QColor(110, 118, 125), true);
            addSceneText(QStringLiteral("OFF"), QPointF(218, 154), 22, QColor(145, 70, 70), true);
            return;
        }

        // Battery indicator: 10 segments across the top.
        for (int i = 0; i < 10; ++i) {
            QColor c;
            if (i < 3) c = QColor(190, 55, 55);
            else if (i < 7) c = QColor(210, 180, 40);
            else c = QColor(58, 166, 83);
            scene_->addRect(QRectF(12 + i * 24, 8, 20, 12), QPen(Qt::NoPen), QBrush(c));
        }

        // Information bar.
        addSceneText(QStringLiteral("P%1  M%2").arg(profile_).arg(mode_), QPointF(12, 24), 14,
                     QColor(220, 220, 220), true);
        addSceneText(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm")),
                     QPointF(438, 24), 14, QColor(220, 220, 220));

        if (settingsOpen_) {
            renderSettingsDisplay();
            return;
        }

        // Main drive area.
        const QPointF center(250, 164);
        const double radius = 104.0;
        scene_->addEllipse(QRectF(center.x() - radius, center.y() - radius,
                                 radius * 2, radius * 2),
                           QPen(QColor(95, 118, 140), 7), QBrush(QColor(22, 27, 32)));
        scene_->addEllipse(QRectF(center.x() - 68, center.y() - 68, 136, 136),
                           QPen(QColor(52, 60, 68), 2), QBrush(QColor(15, 18, 21)));

        const double magnitude = std::min(100.0, std::hypot(static_cast<double>(joystickX_),
                                                            static_cast<double>(joystickY_)));
        const double speed = (magnitude / 100.0) * (speedSetting_ / 100.0) * 12.0;

        addSceneText(QStringLiteral("%1").arg(speed, 0, 'f', 1), QPointF(218, 128), 30,
                     QColor(245, 245, 245), true);
        addSceneText(QStringLiteral("km/h"), QPointF(230, 165), 14, QColor(200, 205, 210));
        addSceneText(QStringLiteral("Profil %1").arg(profile_), QPointF(214, 192), 18,
                     QColor(245, 215, 70), true);

        // Maximum speed indicator.
        const int litSegments = std::max(1, static_cast<int>(std::lround(speedSetting_ / 20.0)));
        for (int i = 0; i < 5; ++i) {
            const QColor c = i < litSegments ? QColor(225, 225, 225) : QColor(70, 76, 82);
            scene_->addRect(QRectF(190 + i * 25, 250, 20, 9), QPen(Qt::NoPen), QBrush(c));
        }
        addSceneText(QStringLiteral("MAX %1%").arg(speedSetting_), QPointF(205, 261), 12,
                     QColor(185, 190, 195));

        // Lighting/indicator symbols around the main area.
        if (hazardsOn_ && flashPhase_)
            addSceneText(QStringLiteral("⚠"), QPointF(34, 68), 30, QColor(245, 175, 40), true);
        if (lightsOn_)
            addSceneText(QStringLiteral("LIGHT"), QPointF(408, 76), 13, QColor(245, 235, 155), true);
        if ((leftIndicatorOn_ || hazardsOn_) && flashPhase_)
            addSceneText(QStringLiteral("◀"), QPointF(46, 218), 32, QColor(65, 210, 95), true);
        if ((rightIndicatorOn_ || hazardsOn_) && flashPhase_)
            addSceneText(QStringLiteral("▶"), QPointF(430, 218), 32, QColor(65, 210, 95), true);
        if (hornActive_)
            addSceneText(QStringLiteral("HORN"), QPointF(218, 78), 14, QColor(235, 110, 80), true);

        // Text bar.
        scene_->addRect(QRectF(0, 294, 500, 26), QPen(Qt::NoPen), QBrush(QColor(35, 40, 45)));
        addSceneText(QStringLiteral("Drive  |  Joystick X %1  Y %2").arg(joystickX_).arg(joystickY_),
                     QPointF(118, 293), 13, QColor(238, 238, 238));
    }

    void renderSettingsDisplay()
    {
        addSceneText(QStringLiteral("Settings Menu"), QPointF(165, 54), 22,
                     QColor(238, 238, 238), true);
        int y = 88;
        for (int i = 0; i < settingsItems_.size(); ++i) {
            const bool selected = i == settingsIndex_;
            if (selected) {
                scene_->addRect(QRectF(95, y - 2, 310, 28), QPen(Qt::NoPen),
                                QBrush(QColor(125, 25, 25)));
            }
            addSceneText(settingsItems_[i], QPointF(112, y), 16,
                         selected ? QColor(255, 255, 255) : QColor(205, 205, 205),
                         selected);
            y += 29;
        }
        addSceneText(QStringLiteral("Joystick ↑↓ wählen, → öffnen, ← zurück"),
                     QPointF(92, 286), 12, QColor(180, 185, 190));
    }

    void refreshStatus()
    {
        if (fd_ < 0) {
            statusLabel_->setText(QStringLiteral("Status: CAN nicht geöffnet"));
            return;
        }

        QString powerText;
        if (powerTransition_ == PowerTransition::Starting)
            powerText = QStringLiteral("EIN-Sequenz");
        else if (powerTransition_ == PowerTransition::Stopping)
            powerText = QStringLiteral("AUS-Sequenz");
        else
            powerText = powerOn_ ? QStringLiteral("EIN") : QStringLiteral("AUS");

        statusLabel_->setText(
            QStringLiteral("Status: %1 | Profil %2 | Mode %3 | Max Speed %4% | %5")
                .arg(powerText)
                .arg(profile_)
                .arg(mode_)
                .arg(speedSetting_)
                .arg(replay_.empty() ? QStringLiteral("nur Zyklik")
                                     : QStringLiteral("Replay aktiv")));
        refreshCounters();
    }

    void refreshCounters()
    {
        countersLabel_->setText(
            QStringLiteral("RX: %1   TX: %2   Replay-Q: %3   GUI-Q: %4   Power-Q: %5")
                .arg(rxCount_)
                .arg(txCount_)
                .arg(responseQueue_.size())
                .arg(controlQueue_.size())
                .arg(powerSequence_.size()));
    }

    void appendLog(const QString& s)
    {
        if (log_)
            log_->appendPlainText(s);
    }

    QString iface_;
    QString replayPath_;

    int fd_ = -1;
    bool powerOn_ = false;
    std::uint64_t rxCount_ = 0;
    std::uint64_t txCount_ = 0;

    ReplayMap replay_;
    std::deque<CanMessage> responseQueue_;
    std::deque<QueuedControl> controlQueue_;
    std::deque<ScheduledPowerFrame> powerSequence_;
    std::vector<PeriodicFrame> periodic_;
    QElapsedTimer clock_;
    PowerTransition powerTransition_ = PowerTransition::Stable;
    qint64 powerSequenceStartMs_ = 0;

    int joystickX_ = 0;
    int joystickY_ = 0;
    int profile_ = 1;
    int mode_ = 1;
    int speedSetting_ = 50;
    bool pmInModes_ = false;
    bool hazardsOn_ = false;
    bool lightsOn_ = false;
    bool leftIndicatorOn_ = false;
    bool rightIndicatorOn_ = false;
    bool hornActive_ = false;
    bool flashPhase_ = true;
    bool settingsOpen_ = false;
    int settingsIndex_ = 0;
    int settingsLastDirection_ = 0;
    const QStringList settingsItems_ = {
        QStringLiteral("Time"),
        QStringLiteral("Distance"),
        QStringLiteral("Backlight"),
        QStringLiteral("Bluetooth"),
        QStringLiteral("IR Setup"),
        QStringLiteral("Programming"),
        QStringLiteral("Exit")
    };

    QSocketNotifier* notifier_ = nullptr;
    QTimer* periodicTimer_ = nullptr;
    QTimer* responseTimer_ = nullptr;
    QTimer* powerTimer_ = nullptr;
    QTimer* displayTimer_ = nullptr;
    QElapsedTimer topLeftHold_;

    QLabel* ifaceLabel_ = nullptr;
    QLabel* replayLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* countersLabel_ = nullptr;
    QPlainTextEdit* log_ = nullptr;

    QGraphicsScene* scene_ = nullptr;
    QGraphicsView* displayView_ = nullptr;
    JoystickPad* joystick_ = nullptr;

    QPushButton* screenTopLeft_ = nullptr;
    QPushButton* screenTopRight_ = nullptr;
    QPushButton* screenBottomLeft_ = nullptr;
    QPushButton* screenBottomRight_ = nullptr;
    QPushButton* modeButton_ = nullptr;
    QPushButton* profileButton_ = nullptr;
    QPushButton* hornButton_ = nullptr;
    QPushButton* onOffPaddle_ = nullptr;
    QPushButton* profileModePaddle_ = nullptr;
    QPushButton* speedDownButton_ = nullptr;
    QPushButton* speedUpButton_ = nullptr;
    QPushButton* externalOnOffButton_ = nullptr;
    QPushButton* externalProfileModeButton_ = nullptr;
    QPushButton* joyUpButton_ = nullptr;
    QPushButton* joyDownButton_ = nullptr;
    QPushButton* joyLeftButton_ = nullptr;
    QPushButton* joyRightButton_ = nullptr;
    QPushButton* joyCenterButton_ = nullptr;
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
