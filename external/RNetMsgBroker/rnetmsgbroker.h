#ifndef RNETMSGBROKER_H
#define RNETMSGBROKER_H

#include "RNetMsgBroker_global.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMultiMap>
#include <QObject>
#include <QString>
#include <QStringList>

//! Decode editable R-Net/CAN frame catalogues.
//!
//! RNetMsgBroker is a small Qt Core helper that loads frame definitions from
//! R-Net.json and turns CAN frames into readable text. The catalogue can be
//! extended at runtime with add() and reduced again with del().
class RNETMSGBROKER_EXPORT RNetMsgBroker : public QObject
{
    Q_OBJECT

public:
    //! Minimal CAN message representation used by the decoder.
    //!
    //! `extended` selects 29-bit CAN IDs, `remote` marks RTR frames.
    struct CanMsg
    {
        quint32 canId = 0;
        QByteArray data;
        bool extended = false;
        bool remote = false;
    };

    //! Create an empty broker. Call readJson() or add() before decoding.
    explicit RNetMsgBroker(QObject *parent = nullptr);

    //! Load definitions from a JSON file. On failure, `errorString` receives details.
    bool readJson(const QString &jsonFilename, QString *errorString = nullptr);

    //! Add one frame definition at runtime.
    bool add(const QJsonObject &json, QString *errorString = nullptr);
    //! Add all frame definitions from a JSON document.
    bool add(const QJsonDocument &json, QString *errorString = nullptr);
    //! Delete matching definitions using `id-result` and optional `Id-and-mask`.
    bool del(const QJsonObject &json, QString *errorString = nullptr);

    //! Decode a CAN message into text. `full=true` appends metadata.
    QString toString(const CanMsg &msg, bool full = false) const;
    //! Convenience overload for callers that do not build CanMsg explicitly.
    QString toString(quint32 canId, const QByteArray &data, bool extended = false, bool remote = false, bool full = false) const;

    //! Compile-time library/test-app version.
    static QString versionString();

    //! Number of loaded frame definitions.
    int count() const;
    //! Remove all loaded definitions.
    void clear();
    //! Return all loaded frame names. Useful for diagnostics and tests.
    QStringList names() const;

private:
    enum class Endian
    {
        Little,
        Big
    };

    struct IdPart
    {
        quint32 mask = 0;
        QString name;
        Endian endian = Endian::Little;
        int shift = 0;
    };

    struct Field
    {
        QString name;
        QString source = QStringLiteral("data");
        quint64 bitMask = 0;
        int shift = 0;
        int offset = 0;
        int width = 0;
        bool bigEndian = false;
        bool isSigned = false;
        bool hasByteWindow = false;
        QString cycle;       // JSON: fields[].zyklus; normally inherited from Definition::cycle
        QString unit;        // JSON: unit/einheit
        QString description; // JSON: description
    };

    struct DataMatcher
    {
        bool enabled = false;
        quint64 value = 0;
        quint64 mask = 0xFFFFFFFFFFFFFFFFull;
        int offset = 0;
        int width = 0;
        bool bigEndian = false;
    };

    struct Definition
    {
        quint32 idAndMask = 0;
        quint32 idResult = 0;
        quint32 keyMask = 0xFFFFFFFFu;
        QString rnetName;
        QString frameType;
        QString phase;   // JSON: frames[].phase, z.B. "startup.serial_auth"
        QString source;  // JSON: frames[].quelle/source, z.B. "jsm"
        QString sink;    // JSON: frames[].senke/sink, z.B. "pm"
        QString summary;
        QString cycle; // JSON: frames[].zyklus, z.B. "20ms" oder "bei Änderung"
        int extended = -1; // -1 = any, 0 = standard, 1 = extended
        int remote = -1;   // -1 = any, 0 = data, 1 = RTR
        QList<IdPart> idParts;
        QList<Field> fields;
        DataMatcher dataMatcher;
        QJsonObject originalJson;
    };

    using DefinitionMap = QMultiMap<quint32, Definition>;

    bool addObjectToMap(const QJsonObject &json, DefinitionMap *target, QString *errorString) const;
    bool parseDefinition(const QJsonObject &json, Definition *definition, QString *errorString) const;
    bool matches(const Definition &definition, const CanMsg &msg) const;
    QString render(const Definition &definition, const CanMsg &msg, bool full) const;

    static QString hex32(quint32 value);
    static QString hex64(quint64 value);
    static QString hexValue(quint64 value);
    static QString hexBytes(const QByteArray &data);
    static bool valueByNames(const QJsonObject &object, const QStringList &names, QJsonValue *value);
    static bool stringByNames(const QJsonObject &object, const QStringList &names, QString *text);
    static bool textByNames(const QJsonObject &object, const QStringList &names, QString *text);
    static QString jsonValueToText(const QJsonValue &value);
    static bool boolByNames(const QJsonObject &object, const QStringList &names, bool *value);
    static bool numberByNames(const QJsonObject &object, const QStringList &names, quint64 *value);
    static bool jsonValueToUInt64(const QJsonValue &value, quint64 *number);
    static int trailingShift(quint64 mask);
    static int popCount32(quint32 value);
    static int popCount64(quint64 value);
    static Endian endianFromJson(const QJsonObject &object, Endian fallback = Endian::Little);
    static quint64 bytesToInteger(const QByteArray &data, int offset, int width, bool bigEndian);
    static qint64 signExtend(quint64 value, int bitCount);
    static QList<QJsonObject> objectsFromDocument(const QJsonDocument &doc, QString *errorString);

    DefinitionMap m_definitionsByIdResult;
};

#endif // RNETMSGBROKER_H
