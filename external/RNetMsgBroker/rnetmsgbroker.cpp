// SPDX-License-Identifier: LicenseRef-ProjectOwner
//
// RNetMsgBroker core implementation.
//
// The broker loads editable R-Net/CAN frame definitions from strict,
// syntactically valid JSON, matches incoming CAN frames against ID/data masks,
// and renders a compact or metadata-rich text representation.

#include "rnetmsgbroker.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QtGlobal>

#include <algorithm>

namespace {
constexpr int AnyFlag = -1;
constexpr int FalseFlag = 0;
constexpr int TrueFlag = 1;

QString trimHexPrefix(QString text)
{
    text = text.trimmed();
    if (text.startsWith(QLatin1Char('+')))
        text.remove(0, 1);
    if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        text.remove(0, 2);
    return text;
}
}

RNetMsgBroker::RNetMsgBroker(QObject *parent)
    : QObject(parent)
{
}

QString RNetMsgBroker::versionString()
{
#ifdef RNETMSGBROKER_VERSION_STRING
    return QStringLiteral(RNETMSGBROKER_VERSION_STRING);
#else
    return QStringLiteral("1.0.0");
#endif
}

// Load a complete frame catalogue. Parsing is transactional: the existing
// catalogue is replaced only after every definition was parsed successfully.
bool RNetMsgBroker::readJson(const QString &jsonFilename, QString *errorString)
{
    QFile file(jsonFilename);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorString)
            *errorString = tr("Kann JSON-Datei nicht öffnen: %1").arg(file.errorString());
        return false;
    }

    const QByteArray rawJson = file.readAll();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(rawJson, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorString)
            *errorString = tr("JSON-Parsefehler bei Offset %1: %2")
                               .arg(parseError.offset)
                               .arg(parseError.errorString());
        return false;
    }

    const QList<QJsonObject> objects = objectsFromDocument(doc, errorString);
    if (objects.isEmpty())
        return false;

    DefinitionMap parsed;
    for (int i = 0; i < objects.size(); ++i) {
        QString localError;
        if (!addObjectToMap(objects.at(i), &parsed, &localError)) {
            if (errorString)
                *errorString = tr("Fehler in Definition %1: %2").arg(i).arg(localError);
            return false;
        }
    }

    m_definitionsByIdResult = parsed;
    return true;
}

bool RNetMsgBroker::add(const QJsonObject &json, QString *errorString)
{
    return addObjectToMap(json, &m_definitionsByIdResult, errorString);
}

bool RNetMsgBroker::add(const QJsonDocument &json, QString *errorString)
{
    const QList<QJsonObject> objects = objectsFromDocument(json, errorString);
    if (objects.isEmpty())
        return false;

    DefinitionMap parsed;
    for (int i = 0; i < objects.size(); ++i) {
        QString localError;
        if (!addObjectToMap(objects.at(i), &parsed, &localError)) {
            if (errorString)
                *errorString = tr("Fehler in add()-Definition %1: %2").arg(i).arg(localError);
            return false;
        }
    }

    for (auto it = parsed.cbegin(); it != parsed.cend(); ++it)
        m_definitionsByIdResult.insert(it.key(), it.value());
    return true;
}

bool RNetMsgBroker::del(const QJsonObject &json, QString *errorString)
{
    quint64 idResult = 0;
    const bool hasIdResult = numberByNames(json,
                                           {QStringLiteral("id-result"), QStringLiteral("id_result"), QStringLiteral("result"), QStringLiteral("value")},
                                           &idResult);

    quint64 idAndMask = 0;
    const bool hasMask = numberByNames(json,
                                       {QStringLiteral("Id-and-mask"), QStringLiteral("id-and-mask"), QStringLiteral("id_and_mask"), QStringLiteral("mask")},
                                       &idAndMask);

    if (!hasIdResult) {
        if (hasMask)
            idResult = idAndMask;
        else {
            if (errorString)
                *errorString = tr("del(json) braucht id-result oder Id-and-mask");
            return false;
        }
    }

    int removed = 0;
    auto it = m_definitionsByIdResult.lowerBound(static_cast<quint32>(idResult));
    const auto end = m_definitionsByIdResult.upperBound(static_cast<quint32>(idResult));
    while (it != end) {
        if (!hasMask || it.value().idAndMask == static_cast<quint32>(idAndMask)) {
            it = m_definitionsByIdResult.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }

    if (removed == 0 && errorString)
        *errorString = tr("Keine passende Definition für id-result=%1 gefunden").arg(hex32(static_cast<quint32>(idResult)));
    return removed > 0;
}

// Decode one CAN message. If multiple definitions match, the most specific
// one wins: more ID bits, explicit STD/EXT/RTR flags and data masks score
// higher than generic families.
QString RNetMsgBroker::toString(const CanMsg &msg, bool full) const
{
    Definition best;
    bool found = false;
    int bestScore = -1;

    for (auto it = m_definitionsByIdResult.cbegin(); it != m_definitionsByIdResult.cend(); ++it) {
        const Definition &definition = it.value();
        if (!matches(definition, msg))
            continue;

        int score = popCount32(definition.idAndMask);
        if (definition.extended != AnyFlag)
            score += 4;
        if (definition.remote != AnyFlag)
            score += 8;
        if (definition.dataMatcher.enabled)
            score += 64 + popCount64(definition.dataMatcher.mask);
        score += definition.fields.size();

        if (!found || score > bestScore) {
            best = definition;
            found = true;
            bestScore = score;
        }
    }

    if (!found) {
        QStringList parts;
        parts << QStringLiteral("UNKNOWN")
              << QStringLiteral("id=%1").arg(hex32(msg.canId))
              << QStringLiteral("%1").arg(msg.extended ? QStringLiteral("EXT") : QStringLiteral("STD"));
        if (msg.remote)
            parts << QStringLiteral("RTR");
        if (!msg.data.isEmpty())
            parts << QStringLiteral("data=%1").arg(hexBytes(msg.data));
        if (full)
            parts << QStringLiteral("dlc=%1").arg(msg.data.size());
        return parts.join(QStringLiteral(" "));
    }

    return render(best, msg, full);
}

QString RNetMsgBroker::toString(quint32 canId, const QByteArray &data, bool extended, bool remote, bool full) const
{
    CanMsg msg;
    msg.canId = canId;
    msg.data = data;
    msg.extended = extended;
    msg.remote = remote;
    return toString(msg, full);
}

int RNetMsgBroker::count() const
{
    return m_definitionsByIdResult.size();
}

void RNetMsgBroker::clear()
{
    m_definitionsByIdResult.clear();
}

QStringList RNetMsgBroker::names() const
{
    QSet<QString> seen;
    QStringList result;
    for (auto it = m_definitionsByIdResult.cbegin(); it != m_definitionsByIdResult.cend(); ++it) {
        const QString name = it.value().rnetName;
        if (!name.isEmpty() && !seen.contains(name)) {
            seen.insert(name);
            result << name;
        }
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

bool RNetMsgBroker::addObjectToMap(const QJsonObject &json, DefinitionMap *target, QString *errorString) const
{
    Definition definition;
    if (!parseDefinition(json, &definition, errorString))
        return false;

    target->insert(definition.idResult, definition);
    return true;
}

bool RNetMsgBroker::parseDefinition(const QJsonObject &json, Definition *definition, QString *errorString) const
{
    if (!definition) {
        if (errorString)
            *errorString = tr("Interner Fehler: definition == nullptr");
        return false;
    }

    quint64 idAndMask = 0;
    if (!numberByNames(json,
                       {QStringLiteral("Id-and-mask"), QStringLiteral("id-and-mask"), QStringLiteral("id_and_mask"), QStringLiteral("mask")},
                       &idAndMask)) {
        if (errorString)
            *errorString = tr("Pflichtfeld Id-and-mask fehlt oder ist keine Zahl");
        return false;
    }

    quint64 idResult = 0;
    if (!numberByNames(json,
                       {QStringLiteral("id-result"), QStringLiteral("id_result"), QStringLiteral("result"), QStringLiteral("value")},
                       &idResult)) {
        // Praktischer Kurzfall: Wenn das Ergebnis fehlt, wird dieselbe Konstante
        // wie bei Id-and-mask benutzt. Das passt z.B. zu 0x56710 & 0x56700 == 0x56700.
        idResult = idAndMask;
    }

    definition->idAndMask = static_cast<quint32>(idAndMask);
    definition->idResult = static_cast<quint32>(idResult);
    definition->keyMask = definition->idAndMask;
    definition->originalJson = json;

    quint64 keyMask = 0;
    if (numberByNames(json, {QStringLiteral("key-mask"), QStringLiteral("key_mask"), QStringLiteral("keyMask")}, &keyMask))
        definition->keyMask = static_cast<quint32>(keyMask);

    stringByNames(json, {QStringLiteral("rnet_name"), QStringLiteral("rnet-name"), QStringLiteral("name")}, &definition->rnetName);
    stringByNames(json, {QStringLiteral("frametype"), QStringLiteral("frame-type"), QStringLiteral("frame_type"), QStringLiteral("type")}, &definition->frameType);
    textByNames(json, {QStringLiteral("phase")}, &definition->phase);
    textByNames(json, {QStringLiteral("quelle")}, &definition->source);
    textByNames(json, {QStringLiteral("senke")}, &definition->sink);
    stringByNames(json, {QStringLiteral("summary"), QStringLiteral("description"), QStringLiteral("desc")}, &definition->summary);
    textByNames(json, {QStringLiteral("zyklus")}, &definition->cycle);

    if (definition->rnetName.isEmpty())
        definition->rnetName = QStringLiteral("RNet_%1_%2").arg(hex32(definition->idResult), hex32(definition->idAndMask));

    bool boolValue = false;
    if (boolByNames(json, {QStringLiteral("extended"), QStringLiteral("is_extended"), QStringLiteral("isExtended")}, &boolValue))
        definition->extended = boolValue ? TrueFlag : FalseFlag;

    if (boolByNames(json, {QStringLiteral("remote"), QStringLiteral("rtr"), QStringLiteral("is_remote")}, &boolValue))
        definition->remote = boolValue ? TrueFlag : FalseFlag;
    else {
        QString remoteText;
        if (stringByNames(json, {QStringLiteral("remote"), QStringLiteral("rtr")}, &remoteText)) {
            remoteText = remoteText.trimmed().toLower();
            if (remoteText == QStringLiteral("any") || remoteText == QStringLiteral("*") || remoteText == QStringLiteral("both"))
                definition->remote = AnyFlag;
            else if (remoteText == QStringLiteral("rtr") || remoteText == QStringLiteral("remote") || remoteText == QStringLiteral("true"))
                definition->remote = TrueFlag;
            else if (remoteText == QStringLiteral("data") || remoteText == QStringLiteral("false"))
                definition->remote = FalseFlag;
        }
    }

    QJsonValue idPartsValue;
    valueByNames(json,
                 {QStringLiteral("id_parts"), QStringLiteral("id-parts"), QStringLiteral("idParts")},
                 &idPartsValue);
    if (idPartsValue.isArray()) {
        const QJsonArray idParts = idPartsValue.toArray();
        for (const QJsonValue &value : idParts) {
            if (!value.isObject())
                continue;
            const QJsonObject item = value.toObject();
            quint64 mask = 0;
            if (!numberByNames(item, {QStringLiteral("mask"), QStringLiteral("bit_mask"), QStringLiteral("bit-mask")}, &mask))
                continue;
            IdPart part;
            part.mask = static_cast<quint32>(mask);
            part.shift = trailingShift(mask);
            part.endian = endianFromJson(item, Endian::Little);
            stringByNames(item, {QStringLiteral("name"), QStringLiteral("sig_name"), QStringLiteral("sig-name")}, &part.name);
            quint64 shift = 0;
            if (numberByNames(item, {QStringLiteral("shift")}, &shift))
                part.shift = static_cast<int>(shift);
            if (part.name.isEmpty())
                part.name = QStringLiteral("id_%1").arg(hex32(part.mask));
            definition->idParts << part;
        }
    }

    QJsonValue fieldsValue;
    valueByNames(json,
                 {QStringLiteral("fields")},
                 &fieldsValue);
    if (fieldsValue.isArray()) {
        const QJsonArray fields = fieldsValue.toArray();
        for (const QJsonValue &value : fields) {
            if (!value.isObject())
                continue;
            const QJsonObject item = value.toObject();
            Field field;
            stringByNames(item, {QStringLiteral("sig_name"), QStringLiteral("sig-name"), QStringLiteral("name")}, &field.name);
            stringByNames(item, {QStringLiteral("source")}, &field.source);
            field.source = field.source.trimmed().toLower();
            if (field.source.isEmpty())
                field.source = QStringLiteral("data");
            if (field.name.isEmpty())
                field.name = QStringLiteral("field%1").arg(definition->fields.size());

            quint64 bitMask = 0;
            if (numberByNames(item,
                              {QStringLiteral("bit_mask"), QStringLiteral("bit-mask"), QStringLiteral("mask")},
                              &bitMask)) {
                field.bitMask = bitMask;
                field.shift = trailingShift(bitMask);
            } else {
                field.bitMask = 0xFFFFFFFFFFFFFFFFull;
                field.shift = 0;
            }

            quint64 shift = 0;
            if (numberByNames(item, {QStringLiteral("shift")}, &shift))
                field.shift = static_cast<int>(shift);

            quint64 offset = 0;
            if (numberByNames(item, {QStringLiteral("offset"), QStringLiteral("byte"), QStringLiteral("byte_offset")}, &offset)) {
                field.offset = static_cast<int>(offset);
                field.hasByteWindow = true;
            }

            quint64 width = 0;
            if (numberByNames(item, {QStringLiteral("width"), QStringLiteral("size"), QStringLiteral("len"), QStringLiteral("length")}, &width)) {
                field.width = static_cast<int>(width);
                field.hasByteWindow = true;
            }

            field.bigEndian = (endianFromJson(item, Endian::Little) == Endian::Big);
            boolByNames(item,
                        {QStringLiteral("big_endian"), QStringLiteral("big-endian")},
                        &field.bigEndian);
            boolByNames(item, {QStringLiteral("signed"), QStringLiteral("is_signed")}, &field.isSigned);
            // Ab v0.2.14 wird nur noch das korrekt geschriebene Feld "zyklus" akzeptiert.
            textByNames(item, {QStringLiteral("zyklus")}, &field.cycle);
            if (field.cycle.isEmpty())
                field.cycle = definition->cycle;
            textByNames(item, {QStringLiteral("einheit"), QStringLiteral("unit"), QStringLiteral("units")}, &field.unit);
            textByNames(item, {QStringLiteral("description")}, &field.description);

            definition->fields << field;
        }
    }

    QJsonValue dataValue;
    if (valueByNames(json,
                     {QStringLiteral("data"), QStringLiteral("data-match"), QStringLiteral("data_match"), QStringLiteral("payload")},
                     &dataValue)) {
        if (dataValue.isObject()) {
            const QJsonObject dataObject = dataValue.toObject();
            quint64 value = 0;
            if (numberByNames(dataObject, {QStringLiteral("value"), QStringLiteral("data"), QStringLiteral("id-result"), QStringLiteral("result")}, &value)) {
                definition->dataMatcher.enabled = true;
                definition->dataMatcher.value = value;
            }
            quint64 mask = 0;
            if (numberByNames(dataObject, {QStringLiteral("mask"), QStringLiteral("bit_mask"), QStringLiteral("bit-mask")}, &mask))
                definition->dataMatcher.mask = mask;
            quint64 offset = 0;
            if (numberByNames(dataObject, {QStringLiteral("offset"), QStringLiteral("byte")}, &offset))
                definition->dataMatcher.offset = static_cast<int>(offset);
            quint64 width = 0;
            if (numberByNames(dataObject, {QStringLiteral("width"), QStringLiteral("size"), QStringLiteral("len")}, &width))
                definition->dataMatcher.width = static_cast<int>(width);
            definition->dataMatcher.bigEndian = (endianFromJson(dataObject, Endian::Little) == Endian::Big);
            boolByNames(dataObject,
                        {QStringLiteral("big_endian"), QStringLiteral("big-endian")},
                        &definition->dataMatcher.bigEndian);
        } else {
            quint64 value = 0;
            if (jsonValueToUInt64(dataValue, &value)) {
                definition->dataMatcher.enabled = true;
                definition->dataMatcher.value = value;
            }
        }
    }

    return true;
}

// Return true when ID/mask, frame format, RTR flag and optional payload
// matcher agree with the incoming frame.
bool RNetMsgBroker::matches(const Definition &definition, const CanMsg &msg) const
{
    if (definition.extended != AnyFlag && (msg.extended ? TrueFlag : FalseFlag) != definition.extended)
        return false;
    if (definition.remote != AnyFlag && (msg.remote ? TrueFlag : FalseFlag) != definition.remote)
        return false;
    if ((msg.canId & definition.idAndMask) != definition.idResult)
        return false;

    if (definition.dataMatcher.enabled) {
        // Wenn ein JSON-Datenmuster eine width angibt, muss die Payload auch
        // mindestens so lang sein. Sonst wuerden z.B. leere Frames faelschlich
        // ein Muster 00000000 treffen.
        if (definition.dataMatcher.width > 0 &&
            msg.data.size() < definition.dataMatcher.offset + definition.dataMatcher.width)
            return false;

        const quint64 value = bytesToInteger(msg.data,
                                             definition.dataMatcher.offset,
                                             definition.dataMatcher.width,
                                             definition.dataMatcher.bigEndian);
        if ((value & definition.dataMatcher.mask) != (definition.dataMatcher.value & definition.dataMatcher.mask))
            return false;
    }

    return true;
}

// Render the selected definition. Short mode is stable for logs; full mode
// appends phase/source/sink/cycle and raw matching metadata.
QString RNetMsgBroker::render(const Definition &definition, const CanMsg &msg, bool full) const
{
    QStringList idPartTexts;
    for (const IdPart &part : definition.idParts) {
        quint32 value = msg.canId & part.mask;
        if (part.shift > 0)
            value >>= part.shift;
        else if (part.shift < 0)
            value <<= -part.shift;
        idPartTexts << QStringLiteral("%1=%2").arg(part.name, QString::number(value));
    }

    QStringList fieldTexts;
    for (const Field &field : definition.fields) {
        quint64 raw = 0;
        if (field.source == QStringLiteral("id") || field.source == QStringLiteral("can_id") || field.source == QStringLiteral("canid")) {
            raw = msg.canId;
        } else if (field.hasByteWindow) {
            raw = bytesToInteger(msg.data, field.offset, field.width, field.bigEndian);
        } else {
            // Ohne offset/width bezieht sich bit_mask auf die CAN-Notation nach dem #:
            // 00aabb wird also als 0x00aabb betrachtet. Damit ergibt
            // bit_mask 0x00ff00 -> 0xaa und 0x0000ff -> 0xbb.
            raw = bytesToInteger(msg.data, 0, msg.data.size(), true);
        }

        quint64 unsignedValue = raw & field.bitMask;
        if (field.shift > 0)
            unsignedValue >>= field.shift;
        else if (field.shift < 0)
            unsignedValue <<= -field.shift;

        QString renderedValue;
        if (field.isSigned) {
            quint64 normalizedMask = field.bitMask >> trailingShift(field.bitMask);
            int bits = 0;
            while (normalizedMask != 0) {
                ++bits;
                normalizedMask >>= 1;
            }
            renderedValue = QString::number(signExtend(unsignedValue, std::max(1, bits)));
        } else {
            renderedValue = hexValue(unsignedValue);
        }
        if (!field.unit.isEmpty())
            renderedValue += field.unit;

        QString fieldText = QStringLiteral("%1=%2").arg(field.name, renderedValue);
        if (full) {
            QStringList meta;
            if (!field.description.isEmpty())
                meta << QStringLiteral("desc=%1").arg(field.description);
            if (!field.cycle.isEmpty() && field.cycle != definition.cycle)
                meta << QStringLiteral("zyklus=%1").arg(field.cycle);
            if (field.source != QStringLiteral("data"))
                meta << QStringLiteral("source=%1").arg(field.source);
            if (field.hasByteWindow)
                meta << QStringLiteral("byte=%1 width=%2").arg(field.offset).arg(field.width);
            else
                meta << QStringLiteral("mask=%1 shift=%2").arg(hex64(field.bitMask)).arg(field.shift);
            if (!field.unit.isEmpty())
                meta << QStringLiteral("einheit=%1").arg(field.unit);
            if (!meta.isEmpty())
                fieldText += QStringLiteral(" [%1]").arg(meta.join(QStringLiteral(", ")));
        }
        fieldTexts << fieldText;
    }

    QString head = definition.rnetName;
    if (!idPartTexts.isEmpty())
        head += QStringLiteral(" (%1)").arg(idPartTexts.join(QStringLiteral(", ")));

    QStringList tail;
    if (msg.remote)
        tail << QStringLiteral("RTR");
    tail.append(fieldTexts);

    if (full) {
        QStringList frameMeta;
        if (!definition.frameType.isEmpty())
            frameMeta << QStringLiteral("typ=%1").arg(definition.frameType);
        if (!definition.phase.isEmpty())
            frameMeta << QStringLiteral("phase=%1").arg(definition.phase);
        if (!definition.source.isEmpty())
            frameMeta << QStringLiteral("quelle=%1").arg(definition.source);
        if (!definition.sink.isEmpty())
            frameMeta << QStringLiteral("senke=%1").arg(definition.sink);
        if (!definition.cycle.isEmpty())
            frameMeta << QStringLiteral("zyklus=%1").arg(definition.cycle);
        if (!definition.summary.isEmpty())
            frameMeta << QStringLiteral("desc=%1").arg(definition.summary);
        frameMeta << QStringLiteral("id=%1").arg(hex32(msg.canId));
        frameMeta << QStringLiteral("mask=%1").arg(hex32(definition.idAndMask));
        frameMeta << QStringLiteral("result=%1").arg(hex32(definition.idResult));
        frameMeta << QStringLiteral("%1").arg(msg.extended ? QStringLiteral("EXT") : QStringLiteral("STD"));
        if (!msg.data.isEmpty())
            frameMeta << QStringLiteral("data=%1").arg(hexBytes(msg.data));
        if (!frameMeta.isEmpty())
            tail << QStringLiteral("{%1}").arg(frameMeta.join(QStringLiteral(", ")));
    }

    if (tail.isEmpty()) {
        if (!definition.summary.isEmpty())
            return QStringLiteral("%1; %2").arg(head, definition.summary);
        return head;
    }

    return QStringLiteral("%1; %2").arg(head, tail.join(QStringLiteral("; ")));
}

QString RNetMsgBroker::hex32(quint32 value)
{
    return QStringLiteral("0x%1").arg(value, 0, 16).toUpper();
}

QString RNetMsgBroker::hex64(quint64 value)
{
    return QStringLiteral("0x%1").arg(value, 0, 16).toUpper();
}

QString RNetMsgBroker::hexValue(quint64 value)
{
    return QStringLiteral("0x%1").arg(value, 0, 16);
}

QString RNetMsgBroker::hexBytes(const QByteArray &data)
{
    QStringList bytes;
    bytes.reserve(data.size());
    for (char ch : data) {
        bytes << QStringLiteral("%1")
                     .arg(static_cast<unsigned char>(ch), 2, 16, QLatin1Char('0'))
                     .toUpper();
    }
    return bytes.join(QLatin1Char(' '));
}

bool RNetMsgBroker::valueByNames(const QJsonObject &object, const QStringList &names, QJsonValue *value)
{
    for (const QString &name : names) {
        if (object.contains(name)) {
            if (value)
                *value = object.value(name);
            return true;
        }
    }
    return false;
}

bool RNetMsgBroker::stringByNames(const QJsonObject &object, const QStringList &names, QString *text)
{
    QJsonValue value;
    if (!valueByNames(object, names, &value))
        return false;
    if (value.isString()) {
        if (text)
            *text = value.toString();
        return true;
    }
    return false;
}

bool RNetMsgBroker::textByNames(const QJsonObject &object, const QStringList &names, QString *text)
{
    QJsonValue value;
    if (!valueByNames(object, names, &value))
        return false;

    const QString converted = jsonValueToText(value).trimmed();
    if (converted.isEmpty())
        return false;

    if (text)
        *text = converted;
    return true;
}

QString RNetMsgBroker::jsonValueToText(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble()) {
        const double d = value.toDouble();
        const qint64 i = static_cast<qint64>(d);
        if (qFuzzyCompare(d + 1.0, static_cast<double>(i) + 1.0))
            return QString::number(i);
        return QString::number(d, 'g', 15);
    }
    return QString();
}

bool RNetMsgBroker::boolByNames(const QJsonObject &object, const QStringList &names, bool *value)
{
    QJsonValue jsonValue;
    if (!valueByNames(object, names, &jsonValue))
        return false;

    if (jsonValue.isBool()) {
        if (value)
            *value = jsonValue.toBool();
        return true;
    }
    if (jsonValue.isDouble()) {
        if (value)
            *value = !qFuzzyIsNull(jsonValue.toDouble());
        return true;
    }
    if (jsonValue.isString()) {
        const QString text = jsonValue.toString().trimmed().toLower();
        if (text == QStringLiteral("true") || text == QStringLiteral("yes") || text == QStringLiteral("1") || text == QStringLiteral("on")) {
            if (value)
                *value = true;
            return true;
        }
        if (text == QStringLiteral("false") || text == QStringLiteral("no") || text == QStringLiteral("0") || text == QStringLiteral("off")) {
            if (value)
                *value = false;
            return true;
        }
    }
    return false;
}

bool RNetMsgBroker::numberByNames(const QJsonObject &object, const QStringList &names, quint64 *value)
{
    QJsonValue jsonValue;
    if (!valueByNames(object, names, &jsonValue))
        return false;
    return jsonValueToUInt64(jsonValue, value);
}

bool RNetMsgBroker::jsonValueToUInt64(const QJsonValue &value, quint64 *number)
{
    if (value.isDouble()) {
        const double d = value.toDouble();
        if (d < 0.0)
            return false;
        if (number)
            *number = static_cast<quint64>(d);
        return true;
    }

    if (value.isString()) {
        QString text = value.toString().trimmed();
        bool ok = false;
        quint64 parsed = 0;
        if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
            parsed = trimHexPrefix(text).toULongLong(&ok, 16);
        } else if (text.contains(QRegularExpression(QStringLiteral("[A-Fa-f]")))) {
            parsed = text.toULongLong(&ok, 16);
        } else {
            parsed = text.toULongLong(&ok, 10);
        }
        if (!ok)
            return false;
        if (number)
            *number = parsed;
        return true;
    }

    return false;
}

int RNetMsgBroker::trailingShift(quint64 mask)
{
    if (mask == 0)
        return 0;
    int shift = 0;
    while (((mask >> shift) & 1ull) == 0ull && shift < 63)
        ++shift;
    return shift;
}

int RNetMsgBroker::popCount32(quint32 value)
{
    int count = 0;
    while (value) {
        value &= value - 1;
        ++count;
    }
    return count;
}

int RNetMsgBroker::popCount64(quint64 value)
{
    int count = 0;
    while (value) {
        value &= value - 1;
        ++count;
    }
    return count;
}

RNetMsgBroker::Endian RNetMsgBroker::endianFromJson(const QJsonObject &object, Endian fallback)
{
    QString text;
    if (!stringByNames(object, {QStringLiteral("endian"), QStringLiteral("byte_order"), QStringLiteral("byte-order")}, &text))
        return fallback;
    text = text.trimmed().toLower();
    if (text == QStringLiteral("b") || text == QStringLiteral("big") || text == QStringLiteral("be") || text == QStringLiteral("msb"))
        return Endian::Big;
    if (text == QStringLiteral("l") || text == QStringLiteral("little") || text == QStringLiteral("le") || text == QStringLiteral("lsb"))
        return Endian::Little;
    return fallback;
}

quint64 RNetMsgBroker::bytesToInteger(const QByteArray &data, int offset, int width, bool bigEndian)
{
    if (offset < 0)
        offset = 0;
    if (offset >= data.size())
        return 0;

    int available = data.size() - offset;
    if (width <= 0 || width > available)
        width = available;
    width = std::min(width, 8);

    quint64 value = 0;
    if (bigEndian) {
        for (int i = 0; i < width; ++i)
            value = (value << 8) | static_cast<unsigned char>(data.at(offset + i));
    } else {
        for (int i = 0; i < width; ++i)
            value |= static_cast<quint64>(static_cast<unsigned char>(data.at(offset + i))) << (8 * i);
    }
    return value;
}

qint64 RNetMsgBroker::signExtend(quint64 value, int bitCount)
{
    if (bitCount <= 0 || bitCount >= 64)
        return static_cast<qint64>(value);
    const quint64 signBit = 1ull << (bitCount - 1);
    const quint64 mask = (1ull << bitCount) - 1ull;
    value &= mask;
    if (value & signBit)
        value |= ~mask;
    return static_cast<qint64>(value);
}

QList<QJsonObject> RNetMsgBroker::objectsFromDocument(const QJsonDocument &doc, QString *errorString)
{
    QList<QJsonObject> objects;

    if (doc.isArray()) {
        const QJsonArray array = doc.array();
        for (const QJsonValue &value : array) {
            if (value.isObject())
                objects << value.toObject();
        }
    } else if (doc.isObject()) {
        const QJsonObject root = doc.object();
        QJsonValue arrayValue;
        if (valueByNames(root,
                         {QStringLiteral("frames"), QStringLiteral("messages"), QStringLiteral("definitions"), QStringLiteral("rnet_frames")},
                         &arrayValue) && arrayValue.isArray()) {
            const QJsonArray array = arrayValue.toArray();
            for (const QJsonValue &value : array) {
                if (value.isObject())
                    objects << value.toObject();
            }
        } else {
            objects << root;
        }
    }

    if (objects.isEmpty() && errorString)
        *errorString = QObject::tr("JSON enthält keine Frame-Objekte");
    return objects;
}
