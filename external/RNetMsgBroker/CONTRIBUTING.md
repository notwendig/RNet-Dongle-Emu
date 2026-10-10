# Contributing

## Grundregel

Änderungen an Decoder, JSON-Katalog und Skripten sollen nachvollziehbar, testbar und sicher bleiben. Keine Fahrversuche mit geänderten R-Net/CAN-Frames ohne sichere Testumgebung.

## Lokaler Ablauf

```bash
./scripts/check.sh
./build/RNetMsgBrokerTest --libversion
```

Das prüft JSON, baut das Projekt und erzeugt eine Beispiel-CSV aus den Testdaten, sofern Qt/CMake lokal verfügbar sind.

## JSON-Regeln

Neue Frame-Definitionen in `R-Net.json` sollen möglichst enthalten:

```json
"rnet_name": "RNet...",
"frametype": "...",
"phase": "startup.serial_auth",
"quelle": "jsm",
"senke": "pm",
"summary": "..."
```

Für unsichere Frames bitte nicht raten, sondern markieren:

```json
"phase": "unknown.observed",
"summary": "Observed frame; exact meaning unknown."
```

## CSV-Kompatibilität

Die CSV-Spaltenüberschrift ist bewusst stabil:

```text
zeile;zeit;interface;can_id;format;rtr;dlc;data;name;typ;phase;quelle;senke;ausgabe
```

Änderungen daran bitte nur mit Versionssprung und README-/Doku-Anpassung.

## Pull Requests

Bitte angeben:

- betroffene Frames / CAN-IDs
- Quelle: candump, Dokumentation oder Code
- Sicherheitsrelevanz
- Testergebnis von `./scripts/check.sh`

## Version in Meldungen

Bitte bei Bugs immer die Ausgabe von `RNetMsgBrokerTest --libversion` oder `RNetMsgBrokerTest --version` angeben.
