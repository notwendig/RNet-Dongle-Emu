# RNet-Dongle-Emu

Emulator der FTD2XX/R-Net-Programmer-Schnittstelle mit Analysewerkzeugen und
optionalem SocketCAN-Backend für kontrollierte Testaufbauten.

> **Forschungs-/Interoperabilitätsprojekt.**
> Das Projekt ist keine Sicherheitsfreigabe für einen Rollstuhl. Konkrete
> Anleitungen zur realen Rollstuhlverkabelung, zum Live-Programmieren eines
> Fahrzeugs und zur Umschaltung vom Dealer- auf den OEM-Dongle sind aus
> Sicherheits- und Haftungsgründen bewusst nicht Bestandteil der Dokumentation.
> Siehe [SECURITY.md](SECURITY.md).

## Aktueller Stand — 08.10.2026

Der Emulator-Testpfad funktioniert jetzt vollständig:

- **Connect: OK**
- **Read: OK**
- **Write: OK**

Diese Aussage gilt für den Emulator-/Testaufbau. Sie ist ausdrücklich **keine**
Bestätigung, dass Schreibzugriffe auf einen realen Rollstuhl getestet oder
sicher sind.

Separat wurde bereits eine reale CAN-Anbindung an meinen **You-Q-Testrollstuhl**
erprobt. Darüber konnte der Programmer die reale Konfiguration des
Testrollstuhls lesen. Die gerätespezifische CAN-Anbindung und die Umschaltung
vom Dealer- zum OEM-Dongle bleiben aus Sicherheits- und Haftungsgründen
außerhalb dieses Repositories.

## Betriebsarten

Die Auswahl erfolgt in `ftd2xx-emu.ini`.

### `Device=emu`

Vollständige interne Emulation ohne realen Rollstuhl-CAN-Bus.

### `Device=canN`

SocketCAN-Backend über das angegebene Linux-CAN-Interface, zum Beispiel
`Device=can0`.

Typischer Testaufbau:

```text
R-Net Programmer
       |
   ftd2xx.dll
       |
  Device=can0
       |
      can0
       |
   CAN-Testpaar
       |
      can1
       |
 rollstuhl.emu
```

`rollstuhl.emu` dient dabei als Software-Gegenstelle für den Testbus.

## Enthalten

- `src/ftd2xx_emu.cpp` — 32-Bit-FTD2XX-Kompatibilitäts-DLL
- `Device=emu` — vollständiger Emulatorbetrieb
- `Device=canN` — generische SocketCAN-Anbindung
- `rollstuhl.emu/` — Software-Gegenstelle für CAN-Testaufbauten
- Replay und zustandsbehaftetes POP-Repository
- persistenter RNB2-State
- `tools/rnd-decrypt.c`
- `tools/rnet-bin2md.py`
- CMake-/MinGW32-Build, Tests und Wine-Hilfen

## Build

Fedora:

```bash
sudo dnf install cmake ninja-build mingw32-gcc-c++ gcc openssl-devel make python3
cmake --preset mingw32-release
cmake --build --preset mingw32-release
```

DLL:

```text
build/mingw32/ftd2xx.dll
```

## Konfiguration

Interne Emulation:

```ini
Device=emu
```

SocketCAN, Beispiel:

```ini
Device=can0
```

Lokale Laufzeitdaten wie `rnet-replay.txt` und `rnet-block-state.bin` gehören
nicht in das öffentliche Repository.

## RND lokal analysieren

```bash
make -C tools
tools/rnd-decrypt Generic_V33_1_1245.rnd Generic_V33_1_1245.dec.bin
```

Originale und entschlüsselte RND-Dateien gehören nicht ins Git-Repository.

## Bericht erzeugen

Aus einer gespeicherten Programmer-Konfiguration:

```bash
python3 tools/rnet-bin2md.py \
  example.R-net \
  report.md \
  --rnd Generic_V33_1_1245.dec.bin
```

Aus dem Emulator-State:

```bash
python3 tools/rnet-bin2md.py \
  rnet-block-state.bin \
  report.md \
  --rnd Generic_V33_1_1245.dec.bin
```

Weitere Details stehen unter [docs/](docs/), insbesondere in
[docs/VALIDATION.md](docs/VALIDATION.md) und
[docs/COMPATIBILITY.md](docs/COMPATIBILITY.md).

## Daten- und Sicherheitsgrenze

Vendor-Programme, Vendor-DLLs, `.rnd`-Datenbanken, Kunden-`.R-net`-Dateien,
Replay-Captures und Gerätezustände werden nicht als Projektinhalt verteilt.

## Screenshots

### R-Net Programmer mit emuliertem Dongle

![R-Net Programmer mit emuliertem Dongle](docs/images/rnet-programmer-connected.png)

### Programminformationen

![R-Net Programmer OEM Generic – Programminformationen](docs/images/programmer-about.png)

## Danksagung

Mein besonderer Dank gilt **Constantin Grosch <groschorama@gmail.com>**, der mir
für ungefähr **1,5 Jahre** kostenlos einen R-Net Programmer Dongle zur Verfügung
gestellt hat. Diese Leihgabe ermöglichte die Protokollanalyse,
Vergleichstests und Entwicklungsarbeit.

Außerdem danke ich **ChatGPT von OpenAI** für die Unterstützung bei Protokoll-
und Loganalysen, Fehlersuche, Dokumentation und Coding.
