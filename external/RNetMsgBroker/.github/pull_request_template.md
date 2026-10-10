## Änderung

-

## Betroffene Frames / Dateien

-

## Quelle der Information

- [ ] eigener candump
- [ ] Dokumentation / Referenz
- [ ] Code-Änderung ohne neue Frame-Bedeutung

## Tests

```bash
./scripts/check.sh
./build/RNetMsgBrokerTest --libversion
```

Ergebnis:

```text

```

## Sicherheit

- [ ] Keine aktiven CAN-TX-Änderungen
- [ ] Keine unsicheren Bedeutungen geraten
- [ ] `phase=unknown.observed` genutzt, falls Bedeutung unklar ist
