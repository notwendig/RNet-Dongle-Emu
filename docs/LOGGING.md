# Logging V35

V35 dekodiert CAN-Logmeldungen direkt **im Prozess `rnet-can-proxy`**. Ein zusätzlicher `rnet-log-comment`-Prozess ist nicht mehr nötig.

Beispiel:

```text
CAN-RX can0 0C000400#0B00000000000000 ; RNetLampControlStatus; ...
CAN-RX can0 123#01020304 ; UNKNOWN
```

Der Rohframe vor dem Semikolon bleibt unverändert. Hinter `;` steht der Kurztext von `RNetMsgBroker::toString()`.

Decoderquelle:

```text
external/RNetMsgBroker/R-Net.json
```

Beim Proxy-Build wird diese Datei zusätzlich nach

```text
build/rnet-can-proxy/R-Net.json
```

kopiert. Der Proxy findet sie relativ zu seinem eigenen Binary. Optional kann zur Laufzeit mit `RNET_JSON=/anderer/Pfad/R-Net.json` eine andere Datei gewählt werden.

Dekodierung abschalten:

```bash
RNET_PROXY_DECODE=0 ./start.sh emu
```

Das DLL-Rohlog bleibt ein Rohlog. Für CAN-Protokollanalyse ist in V35 `run/rnet-can-proxy.log` die primäre kommentierte Quelle. Dadurch wird die 32-Bit-MinGW-DLL nicht mit Qt/RNetMsgBroker vermischt.
