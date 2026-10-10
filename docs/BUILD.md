# Build V35

V35 baut weiterhin getrennte Toolchains:

```text
ftd2xx.dll       -> MinGW32
rnet-can-proxy   -> natives Linux + RNetMsgBroker + Qt6::Core
rollstuhl.emu    -> natives Linux / Qt6
```

Komplett:

```bash
./build-clean.sh all
```

Einzeln:

```bash
./build-clean.sh dll
./build-clean.sh proxy
./build-clean.sh gui
```

Der Proxy-Build bindet `external/RNetMsgBroker` per `add_subdirectory()` ein. `-Werror` bleibt aktiv; Buildausgaben mit Compiler-/CMake-Warnungen oder Fehlern werden als Fehler behandelt.

Nach dem Proxy-Build läuft automatisch ein Decoder-Selbsttest. Dabei muss `0C000400#0B00000000000000` als `RNetLampControlStatus` erkannt werden.

Erwartetes Ende:

```text
============================================================
 BUILD V35: 0 errors, 0 warnings
============================================================
```

## Broker-Quelle

Ist `external/RNetMsgBroker` noch nicht vorhanden, beschafft der V35-Apply den
stabilen Tag `v1.0.0` zunächst nach `/tmp` und kopiert ihn anschließend ins
Projekt. `build-clean.sh` selbst greift nie auf das Netz zu.
