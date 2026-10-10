# RNetMsgBroker als Subprojekt (V35)

`RNetMsgBroker` wird direkt in den nativen Proxy-Build eingebunden:

```text
RNet-Dongle-Emu/
├── external/
│   └── RNetMsgBroker/
├── rnet-can-proxy/
├── rollstuhl.emu/
└── src/
```

`rnet-can-proxy/CMakeLists.txt` verwendet `add_subdirectory()` auf
`external/RNetMsgBroker` und linkt das Target `RNetMsgBroker` direkt.
Die Decoderbasis ist `external/RNetMsgBroker/R-Net.json`.

## Woher kommt das Subprojekt?

V35 verwendet diese Reihenfolge:

1. bereits vorhandenes `external/RNetMsgBroker`;
2. `RNETMSGBROKER_DIR=/pfad/zum/RNetMsgBroker`;
3. bekannte lokale Analyzer-/Broker-Bäume;
4. sonst den öffentlichen Broker-Tag `v1.0.0` von
   `https://github.com/notwendig/RNetMsgBroker.git`.

Der Git-Checkout erfolgt ausschließlich unter `/tmp/RNetMsgBroker-v35-*`.
Anschließend werden die getrackten Quellen nach `external/RNetMsgBroker`
kopiert. Im Projekt entsteht **kein `.git` des Brokers** und kein Backup-Baum.
Nach erfolgreichem Apply ist für normale Builds kein Netzwerkzugriff nötig.

Für vollständig offline arbeitende Systeme kann die Quelle explizit angegeben
werden:

```bash
RNETMSGBROKER_DIR=/pfad/RNetMsgBroker ./apply-v35.sh
```

Der Standardtag kann bei Bedarf ausdrücklich überschrieben werden:

```bash
RNETMSGBROKER_TAG=v1.0.0 ./apply-v35.sh
```

Backups von Apply-Änderungen liegen ausschließlich unter
`/tmp/RNet-Dongle-Emu-v35-*`.
