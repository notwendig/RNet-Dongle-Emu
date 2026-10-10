// SPDX-License-Identifier: GPL-3.0-only
#include "rnetmsgbroker.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFileInfo>
#include <QString>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    if (argc != 2) {
        err << "Usage: rnet-proxy-decode-test /path/to/R-Net.json\n";
        return 2;
    }

    const QString json = QString::fromLocal8Bit(argv[1]);
    if (!QFileInfo(json).isFile()) {
        err << "FEHLER: R-Net.json fehlt: " << json << '\n';
        return 2;
    }

    RNetMsgBroker broker;
    QString brokerError;
    if (!broker.readJson(json, &brokerError)) {
        err << "FEHLER: RNetMsgBroker::readJson: " << brokerError << '\n';
        return 3;
    }

    const QByteArray data = QByteArray::fromHex("0B00000000000000");
    const QString decoded = broker.toString(0x0C000400u, data, true, false, false);
    out << decoded << '\n';

    if (!decoded.contains(QStringLiteral("RNetLampControlStatus"))) {
        err << "FEHLER: RNetLampControlStatus nicht erkannt\n";
        return 4;
    }

    out << "[OK] RNET-PROXY-DECODE-V35\n";
    return 0;
}
