// SPDX-License-Identifier: LicenseRef-ProjectOwner
//
// Command-line test and conversion tool for RNetMsgBroker.
//
// Typical use:
//   RNetMsgBrokerTest --libversion
//   RNetMsgBrokerTest -d candump.txt --full -o candump.csv
// The `--out` path always writes semicolon separated CSV so the result can
// be opened directly in LibreOffice/Calc with German locale defaults.

#include "rnetmsgbroker.h"

#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

#include <cstdio>

// Parse CAN IDs passed on the command line. Values with A-F or 0x are hex;
// purely decimal values stay decimal to keep manual tests ergonomic.
static bool parseUInt(const QString &text, quint32 *value)
{
    QString s = text.trimmed();
    bool ok = false;
    quint64 parsed = 0;
    if (s.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        parsed = s.mid(2).toULongLong(&ok, 16);
    else if (s.contains(QRegularExpression(QStringLiteral("[A-Fa-f]"))))
        parsed = s.toULongLong(&ok, 16);
    else
        parsed = s.toULongLong(&ok, 10);
    if (!ok || parsed > 0x1FFFFFFFu)
        return false;
    if (value)
        *value = static_cast<quint32>(parsed);
    return true;
}

// Accept compact hex (001122), spaced hex (00 11 22) and common separators.
static bool parsePayload(const QString &text, QByteArray *payload)
{
    QByteArray out;
    QString s = text.trimmed();
    s.replace(QLatin1Char(':'), QLatin1Char(' '));
    s.replace(QLatin1Char(','), QLatin1Char(' '));
    s.replace(QLatin1Char(';'), QLatin1Char(' '));

    QStringList tokens = s.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (tokens.size() == 1) {
        QString compact = tokens.first();
        if (compact.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
            compact = compact.mid(2);
        if (compact.size() > 2 && compact.size() % 2 == 0) {
            tokens.clear();
            for (int i = 0; i < compact.size(); i += 2)
                tokens << compact.mid(i, 2);
        }
    }

    for (QString token : tokens) {
        if (token.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
            token = token.mid(2);
        bool ok = false;
        const int byteValue = token.toInt(&ok, 16);
        if (!ok || byteValue < 0 || byteValue > 255)
            return false;
        out.append(static_cast<char>(byteValue));
    }

    if (payload)
        *payload = out;
    return true;
}


struct CandumpRecord
{
    RNetMsgBroker::CanMsg msg;
    int lineNo = 0;
    QString timestamp;
    QString iface;
    QString raw;
};

struct CandumpStats
{
    int printed = 0;
    int skipped = 0;
};

// Parse one candump line such as: `(1622641700.7) can0 00E#FC...`.
// Classic CAN, RTR (`#R`) and minimal CAN-FD (`##`) log forms are accepted.
static bool parseCandumpLine(const QString &line, CandumpRecord *record, QString *errorString)
{
    QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        if (errorString)
            *errorString = QStringLiteral("leere Zeile");
        return false;
    }

    const QStringList tokens = trimmed.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    QString frameToken;
    int frameIndex = -1;
    for (int i = 0; i < tokens.size(); ++i) {
        if (tokens.at(i).contains(QLatin1Char('#'))) {
            frameToken = tokens.at(i);
            frameIndex = i;
            break;
        }
    }

    if (frameToken.isEmpty()) {
        if (errorString)
            *errorString = QStringLiteral("kein candump-Frame token CANID#DATA gefunden");
        return false;
    }

    const int hashPos = frameToken.indexOf(QLatin1Char('#'));
    if (hashPos <= 0) {
        if (errorString)
            *errorString = QStringLiteral("ungueltiges candump-Frame token: %1").arg(frameToken);
        return false;
    }

    QString timestamp;
    if (!tokens.isEmpty() && tokens.first().startsWith(QLatin1Char('(')) && tokens.first().endsWith(QLatin1Char(')')))
        timestamp = tokens.first().mid(1, tokens.first().size() - 2);

    QString iface;
    if (frameIndex > 0) {
        const QString candidate = tokens.at(frameIndex - 1);
        if (!(candidate.startsWith(QLatin1Char('(')) && candidate.endsWith(QLatin1Char(')'))))
            iface = candidate;
    }

    QString idText = frameToken.left(hashPos);
    QString dataText;
    bool isCanFd = false;
    bool remote = false;

    if (hashPos + 1 < frameToken.size() && frameToken.at(hashPos + 1) == QLatin1Char('#')) {
        // CAN-FD-Logform CANID##FD_FLAGS_AND_DATA.
        // RNetMsgBroker kennt aktuell nur klassische CAN-Payloads; das FD-Flag-Byte wird uebersprungen.
        isCanFd = true;
        dataText = frameToken.mid(hashPos + 2);
        if (!dataText.isEmpty())
            dataText.remove(0, 1);
    } else {
        dataText = frameToken.mid(hashPos + 1);
    }

    bool idOk = false;
    QString idHex = idText.trimmed();
    if (idHex.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        idHex = idHex.mid(2);
    const quint64 parsedCanId = idHex.toULongLong(&idOk, 16);
    if (!idOk || parsedCanId > 0x1FFFFFFFULL) {
        if (errorString)
            *errorString = QStringLiteral("ungueltige CAN-ID in candump-Zeile: %1").arg(idText);
        return false;
    }
    const quint32 canId = static_cast<quint32>(parsedCanId);

    QByteArray payload;
    if (dataText.startsWith(QLatin1Char('R'), Qt::CaseInsensitive)) {
        remote = true;
    } else if (!parsePayload(dataText, &payload)) {
        if (errorString)
            *errorString = QStringLiteral("ungueltige candump-Payload: %1").arg(dataText);
        return false;
    }

    if (record) {
        record->timestamp = timestamp;
        record->iface = iface;
        record->raw = trimmed;
        record->msg.canId = canId;
        record->msg.data = payload;
        record->msg.extended = (idText.size() > 3 || canId > 0x7FFu);
        record->msg.remote = remote;
        Q_UNUSED(isCanFd);
    }
    return true;
}

// Escape one field for semicolon CSV. Text is quoted only when necessary.
static QString csvEscape(QString text)
{
    const bool mustQuote = text.contains(QLatin1Char(';'))
                           || text.contains(QLatin1Char('"'))
                           || text.contains(QLatin1Char('\n'))
                           || text.contains(QLatin1Char('\r'));
    text.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return mustQuote ? QStringLiteral("\"%1\"").arg(text) : text;
}

static QString hexCanId(quint32 canId, bool extended)
{
    return QStringLiteral("0x%1").arg(canId, extended ? 8 : 3, 16, QLatin1Char('0')).toUpper();
}

static QString payloadHex(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex()).toUpper();
}

static QString nameFromDecoded(const QString &decoded)
{
    int end = decoded.indexOf(QLatin1Char(';'));
    if (end < 0)
        end = decoded.indexOf(QLatin1Char('{'));
    return (end >= 0 ? decoded.left(end) : decoded).trimmed();
}

static QString metaValue(const QString &fullDecoded, const QString &key)
{
    const int open = fullDecoded.lastIndexOf(QLatin1Char('{'));
    const int close = fullDecoded.lastIndexOf(QLatin1Char('}'));
    if (open < 0 || close <= open)
        return QString();

    const QString meta = fullDecoded.mid(open + 1, close - open - 1);
    const QStringList parts = meta.split(QLatin1Char(','), Qt::SkipEmptyParts);
    const QString needle = key + QStringLiteral("=");
    for (QString part : parts) {
        part = part.trimmed();
        if (part.startsWith(needle))
            return part.mid(needle.size()).trimmed();
    }
    return QString();
}

// Keep this header stable: scripts and spreadsheet imports rely on it.
static void writeCsvHeader(QTextStream &out)
{
    out << QStringLiteral("zeile;zeit;interface;can_id;format;rtr;dlc;data;name;typ;phase;quelle;senke;ausgabe") << '\n';
}

// Write one decoded frame as a single semicolon-CSV row. Metadata columns are
// extracted from the broker full text so the CLI stays independent of internals.
static void writeCsvRow(QTextStream &out,
                        int lineNo,
                        const QString &timestamp,
                        const QString &iface,
                        const RNetMsgBroker::CanMsg &msg,
                        const RNetMsgBroker &broker,
                        bool full)
{
    const QString shortDecoded = broker.toString(msg, false);
    const QString fullDecoded = broker.toString(msg, true);
    const QString decoded = full ? fullDecoded : shortDecoded;

    const QStringList columns = {
        QString::number(lineNo),
        timestamp,
        iface,
        hexCanId(msg.canId, msg.extended),
        msg.extended ? QStringLiteral("EXT") : QStringLiteral("STD"),
        msg.remote ? QStringLiteral("1") : QStringLiteral("0"),
        QString::number(msg.data.size()),
        payloadHex(msg.data),
        nameFromDecoded(shortDecoded),
        metaValue(fullDecoded, QStringLiteral("typ")),
        metaValue(fullDecoded, QStringLiteral("phase")),
        metaValue(fullDecoded, QStringLiteral("quelle")),
        metaValue(fullDecoded, QStringLiteral("senke")),
        decoded
    };

    for (int i = 0; i < columns.size(); ++i) {
        if (i)
            out << QLatin1Char(';');
        out << csvEscape(columns.at(i));
    }
    out << '\n';
}

static int printCandumpFile(const QString &candumpFile, const RNetMsgBroker &broker, bool full, QTextStream &out)
{
    QFile file(candumpFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCritical().noquote() << QStringLiteral("candump-Datei kann nicht gelesen werden: %1").arg(candumpFile);
        return 5;
    }

    QTextStream in(&file);
    int lineNo = 0;
    int printed = 0;
    int skipped = 0;

    while (!in.atEnd()) {
        const QString line = in.readLine();
        ++lineNo;
        if (line.trimmed().isEmpty())
            continue;

        CandumpRecord record;
        record.lineNo = lineNo;
        QString parseError;
        if (!parseCandumpLine(line, &record, &parseError)) {
            ++skipped;
            qWarning().noquote() << QStringLiteral("candump Zeile %1 uebersprungen: %2").arg(lineNo).arg(parseError);
            continue;
        }

        out << broker.toString(record.msg, full) << '\n';
        ++printed;
    }

    out << QStringLiteral("candump: %1 Frames ausgegeben, %2 Zeilen uebersprungen").arg(printed).arg(skipped) << '\n';
    out.flush();
    return skipped == 0 ? 0 : 6;
}

// Convert a whole candump file to CSV. Parse errors are counted and reported
// on stderr; valid frames are still written.
static int writeCandumpCsvFile(const QString &candumpFile,
                               const RNetMsgBroker &broker,
                               bool full,
                               QTextStream &out,
                               CandumpStats *stats)
{
    QFile file(candumpFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCritical().noquote() << QStringLiteral("candump-Datei kann nicht gelesen werden: %1").arg(candumpFile);
        return 5;
    }

    QTextStream in(&file);
    int lineNo = 0;
    int printed = 0;
    int skipped = 0;

    writeCsvHeader(out);

    while (!in.atEnd()) {
        const QString line = in.readLine();
        ++lineNo;
        if (line.trimmed().isEmpty())
            continue;

        CandumpRecord record;
        record.lineNo = lineNo;
        QString parseError;
        if (!parseCandumpLine(line, &record, &parseError)) {
            ++skipped;
            qWarning().noquote() << QStringLiteral("candump Zeile %1 uebersprungen: %2").arg(lineNo).arg(parseError);
            continue;
        }

        writeCsvRow(out, record.lineNo, record.timestamp, record.iface, record.msg, broker, full);
        ++printed;
    }

    out.flush();
    if (stats) {
        stats->printed = printed;
        stats->skipped = skipped;
    }
    return skipped == 0 ? 0 : 6;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("RNetMsgBrokerTest"));
    QCoreApplication::setApplicationVersion(RNetMsgBroker::versionString());

    // Handle version switches before QCommandLineParser processes the
    // command line.  This makes the two diagnostic switches robust even with
    // older Qt builds or stale parser configuration in local build trees.
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--libversion")) {
            QTextStream versionOut(stdout);
            versionOut << RNetMsgBroker::versionString() << '\n';
            versionOut.flush();
            return 0;
        }
        if (arg == QStringLiteral("--version")) {
            QTextStream versionOut(stdout);
            versionOut << QCoreApplication::applicationName()
                       << QLatin1Char(' ')
                       << RNetMsgBroker::versionString() << '\n';
            versionOut.flush();
            return 0;
        }
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Kleine Test-App fuer RNetMsgBroker"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption libVersionOption(QStringLiteral("libversion"),
                                        QStringLiteral("Nur RNetMsgBroker-Library-Version ausgeben"));

    QCommandLineOption jsonOption({QStringLiteral("j"), QStringLiteral("json")},
                                  QStringLiteral("R-Net JSON-Datei"),
                                  QStringLiteral("file"));
    QCommandLineOption candumpOption({QStringLiteral("d"), QStringLiteral("candump")},
                                     QStringLiteral("candump-Datei lesen, Format z.B. '(ts) can0 00E#FC801ECD00000000'"),
                                     QStringLiteral("file"));
    QCommandLineOption outOption({QStringLiteral("o"), QStringLiteral("out")},
                                 QStringLiteral("Semikolon-CSV in Datei schreiben statt Text auf stdout"),
                                 QStringLiteral("file"));
    QCommandLineOption idOption({QStringLiteral("i"), QStringLiteral("id")},
                                QStringLiteral("CAN-ID, z.B. 0x0C000400"),
                                QStringLiteral("canid"));
    QCommandLineOption dataOption(QStringLiteral("data"),
                                  QStringLiteral("Payload, z.B. '0B 00 00 00' oder 0B000000"),
                                  QStringLiteral("bytes"));
    QCommandLineOption extendedOption({QStringLiteral("e"), QStringLiteral("extended")},
                                      QStringLiteral("CAN-ID als Extended Frame auswerten"));
    QCommandLineOption remoteOption({QStringLiteral("r"), QStringLiteral("rtr")},
                                    QStringLiteral("Remote-Transmission-Request / RTR"));
    QCommandLineOption fullOption({QStringLiteral("f"), QStringLiteral("full")},
                                  QStringLiteral("Vollausgabe mit Frame-/Feld-Metadaten"));
    QCommandLineOption bothOption({QStringLiteral("b"), QStringLiteral("both")},
                                  QStringLiteral("Kurz- und Vollausgabe nacheinander testen"));

    parser.addOption(libVersionOption);
    parser.addOption(jsonOption);
    parser.addOption(candumpOption);
    parser.addOption(outOption);
    parser.addOption(idOption);
    parser.addOption(dataOption);
    parser.addOption(extendedOption);
    parser.addOption(remoteOption);
    parser.addOption(fullOption);
    parser.addOption(bothOption);
    parser.process(app);

    if (parser.isSet(libVersionOption)) {
        QTextStream versionOut(stdout);
        versionOut << RNetMsgBroker::versionString() << '\n';
        versionOut.flush();
        return 0;
    }

    QString jsonFile = parser.value(jsonOption);
    if (jsonFile.isEmpty()) {
        const QString local = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("R-Net.json"));
        jsonFile = QFileInfo::exists(local) ? local : QStringLiteral("R-Net.json");
    }

    RNetMsgBroker broker;
    QString error;
    if (!broker.readJson(jsonFile, &error)) {
        qCritical().noquote() << QStringLiteral("readJson fehlgeschlagen: %1").arg(error);
        return 2;
    }

    QFile outputFile;
    QTextStream stdoutStream(stdout);
    QTextStream fileStream;
    QTextStream *out = &stdoutStream;

    if (parser.isSet(outOption)) {
        const QString outFileName = parser.value(outOption);
        if (outFileName.trimmed().isEmpty()) {
            qCritical().noquote() << QStringLiteral("--out/-o braucht einen Dateinamen");
            return 7;
        }
        outputFile.setFileName(outFileName);
        if (!outputFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            qCritical().noquote() << QStringLiteral("Ausgabedatei kann nicht geschrieben werden: %1").arg(outFileName);
            return 8;
        }
        fileStream.setDevice(&outputFile);
        out = &fileStream;
    }

    const bool csvOutput = parser.isSet(outOption);
    if (!csvOutput) {
        *out << QStringLiteral("libversion: %1").arg(RNetMsgBroker::versionString()) << '\n';
        *out << QStringLiteral("geladen: %1 Definitionen aus %2").arg(broker.count()).arg(jsonFile) << '\n';
    } else {
        qInfo().noquote() << QStringLiteral("libversion: %1").arg(RNetMsgBroker::versionString());
        qInfo().noquote() << QStringLiteral("geladen: %1 Definitionen aus %2").arg(broker.count()).arg(jsonFile);
    }

    int rc = 0;

    if (parser.isSet(candumpOption)) {
        if (csvOutput) {
            CandumpStats stats;
            rc = writeCandumpCsvFile(parser.value(candumpOption), broker, parser.isSet(fullOption), *out, &stats);
            qInfo().noquote() << QStringLiteral("CSV geschrieben: %1 (%2 Frames, %3 Zeilen uebersprungen)")
                                   .arg(QFileInfo(outputFile.fileName()).absoluteFilePath())
                                   .arg(stats.printed)
                                   .arg(stats.skipped);
        } else {
            rc = printCandumpFile(parser.value(candumpOption), broker, parser.isSet(fullOption), *out);
        }
        return rc;
    }

    if (parser.isSet(idOption)) {
        quint32 canId = 0;
        if (!parseUInt(parser.value(idOption), &canId)) {
            qCritical().noquote() << QStringLiteral("ungueltige CAN-ID: %1").arg(parser.value(idOption));
            return 3;
        }
        QByteArray data;
        if (parser.isSet(dataOption) && !parsePayload(parser.value(dataOption), &data)) {
            qCritical().noquote() << QStringLiteral("ungueltige Payload-Daten: %1").arg(parser.value(dataOption));
            return 4;
        }
        const bool extended = parser.isSet(extendedOption);
        const bool remote = parser.isSet(remoteOption);
        if (csvOutput) {
            RNetMsgBroker::CanMsg msg{canId, data, extended, remote};
            writeCsvHeader(*out);
            writeCsvRow(*out, 1, QString(), QString(), msg, broker, parser.isSet(fullOption));
            out->flush();
            qInfo().noquote() << QStringLiteral("CSV geschrieben: %1 (1 Frame, 0 Zeilen uebersprungen)")
                                   .arg(QFileInfo(outputFile.fileName()).absoluteFilePath());
        } else if (parser.isSet(bothOption)) {
            *out << QStringLiteral("kurz: ") << broker.toString(canId, data, extended, remote, false) << '\n';
            *out << QStringLiteral("voll: ") << broker.toString(canId, data, extended, remote, true) << '\n';
            out->flush();
        } else {
            *out << broker.toString(canId, data, extended, remote, parser.isSet(fullOption)) << '\n';
            out->flush();
        }
        return 0;
    }

    const QList<RNetMsgBroker::CanMsg> samples = {
        {0x56710u, QByteArray::fromHex("00aabb"), false, false},
        {0x56760u, QByteArray::fromHex("001234"), false, false},
        {0x004u, QByteArray(), false, true},
        {0x060u, QByteArray::fromHex("01020304"), false, false},
        {0x0C000400u, QByteArray::fromHex("0B00000000000000"), true, false},
        {0x1F003000u, QByteArray::fromHex("1122334455667788"), true, false},
        {0x123u, QByteArray::fromHex("01020304"), false, false},
    };

    // Standard-Demo ohne --id/--candump soll beide toString-Varianten wirklich testen.
    // --full alleine zeigt weiterhin nur Vollform; sonst werden kurz+voll ausgegeben.
    const bool printBothInDemo = parser.isSet(bothOption) || !parser.isSet(fullOption);

    if (csvOutput) {
        writeCsvHeader(*out);
        int lineNo = 0;
        for (const RNetMsgBroker::CanMsg &sample : samples)
            writeCsvRow(*out, ++lineNo, QString(), QString(), sample, broker, parser.isSet(fullOption));
        out->flush();
        qInfo().noquote() << QStringLiteral("CSV geschrieben: %1 (%2 Frames, 0 Zeilen uebersprungen)")
                               .arg(QFileInfo(outputFile.fileName()).absoluteFilePath())
                               .arg(samples.size());
        return 0;
    }

    for (const RNetMsgBroker::CanMsg &sample : samples) {
        if (printBothInDemo) {
            *out << QStringLiteral("kurz: ") << broker.toString(sample, false) << '\n';
            *out << QStringLiteral("voll: ") << broker.toString(sample, true) << '\n';
        } else {
            *out << broker.toString(sample, true) << '\n';
        }
    }

    out->flush();
    return 0;
}
