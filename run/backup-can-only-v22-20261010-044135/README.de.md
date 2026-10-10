# RNet-Dongle-Emu

FTD2XX/R-Net-Programmer-Bridge, CAN-Rollstuhl-Emulator und Analysewerkzeuge.

> **Forschungs-/Interoperabilitätsprojekt.**
> Das Projekt ist keine Sicherheitsfreigabe für einen Rollstuhl. Konkrete
> Anleitungen zur realen Rollstuhlverkabelung, zum Live-Programmieren eines
> Fahrzeugs und zur Umschaltung vom Dealer- auf den OEM-Dongle sind bewusst
> nicht Bestandteil der Dokumentation.

## Architektur ab 10.10.2026: nur noch CAN

Die **DLL-lokale Rollstuhl-/Replay-Emulation ist aus dem Laufzeitpfad entfernt**.
`ftd2xx.dll` akzeptiert nur noch `Device=canN`. Die DLL stellt die vom
Programmer benötigte FTD2XX/R-Net-Dongleschnittstelle bereit und reicht
Rollstuhl-CAN über den nativen `rnet-can-proxy` weiter.

Die Rollstuhlemulation ist jetzt eindeutig eine eigene CAN-Gegenstelle:

```text
R-Net Programmer
       |
   ftd2xx.dll
       |
  Device=can0
       |
rnet-can-proxy
       |
      can0
       |
   CAN-Testpaar
       |
      can1
       |
 rollstuhl.emu
```

`Device=emu` wird nicht mehr unterstützt. Damit gibt es nicht mehr gleichzeitig
einen versteckten Rollstuhl-Emulator in der DLL und `rollstuhl.emu` auf CAN.

Die **dongle-lokalen** FTD2XX/R-Net-Steuerkommandos bleiben in der DLL, weil sie
zur Programmer-Kommunikation gehören. Rollstuhl-CAN selbst kommt ausschließlich
vom externen CAN-Bus bzw. von `rollstuhl.emu`.

## Stand

Der Software-Testpfad funktioniert vollständig:

- **Connect: OK**
- **Read: OK**
- **Write: OK**

Diese Aussage gilt für den Emulator-/Testaufbau. Sie ist ausdrücklich keine
Bestätigung für Schreibzugriffe auf einen realen Rollstuhl.

Separat wurde bereits eine reale CAN-Anbindung an einen **You-Q-Testrollstuhl**
erprobt; darüber konnte die Konfiguration gelesen werden. Gerätespezifische
Anbindungsdetails und die Dealer/OEM-Umschaltung bleiben außerhalb des
Repositories.

## Start: `start.sh emu`

Der komplette lokale Testaufbau wird jetzt mit einem Kommando gestartet:

```bash
./start.sh emu
```

`start.sh` erledigt dabei selbst:

1. alte Programmer-/Proxy-/Chair-Prozesse stoppen;
2. `can0` und `can1` auf **125000 bit/s** Classic CAN einrichten und aktivieren;
3. fehlende Komponenten bauen;
4. CAN-only-DLL deployen und `Device=can0` setzen;
5. `rnet-can-proxy` auf `can0` starten;
6. die Qt6-`rollstuhl.emu` auf `can1` starten;
7. R-Net Programmer unter Wine starten.

Weitere Befehle:

```bash
./start.sh status
./start.sh stop
```

Ohne Änderung am Skript können die Interfaces angepasst werden:

```bash
RNET_CAN_DONGLE=can0 \
RNET_CAN_CHAIR=can1 \
RNET_CAN_BITRATE=125000 \
./start.sh emu
```

`RNET_CAN_SETUP=0` überspringt die CAN-Konfiguration, falls sie bereits extern
erledigt wurde.

## `rollstuhl.emu`

`rollstuhl.emu` ist die eigentliche Software-Rollstuhl-Gegenstelle. Die aktuelle
Qt6-GUI enthält die CJSM2-Bedienelemente; das Display wird mit
`QGraphicsScene`/`QGraphicsView` aufgebaut. Replayantworten, GUI-Ereignisse und
zyklische Frames laufen über `can1`.

## Konfiguration

```ini
[emulator]
Device=can0
LogFile=ftd2xx-emu.log
```

Gültig sind `can0`, `can1`, ... . `Device=emu`, `Mode=replay` und ein lokaler
`ReplayFile`-Backendmodus der DLL gehören nicht mehr zur Laufzeitarchitektur.

## Build

```bash
scripts/build-can0-rollstuhl-emu.sh release emu
```

Erzeugt werden:

```text
build/mingw32/ftd2xx.dll
build/rnet-can-proxy/rnet-can-proxy
build/rollstuhl.emu/rollstuhl.emu
```

Beim CAN-only-Release werden Function/Data-Sections und Linker-GC aktiviert,
damit nicht mehr referenzierte Legacy-Helfer der früheren lokalen Emulation
nicht in der Release-DLL verbleiben.

## Dokumentation

- [Protokoll/Architektur](docs/PROTOCOL.md)
- [Validierung](docs/VALIDATION.md)
- [Kompatibilität](docs/COMPATIBILITY.md)

Vendor-Programme, Vendor-DLLs, originale/entschlüsselte `.rnd`-Datenbanken,
Kunden-`.R-net`-Dateien, Replay-Captures und Gerätezustände werden nicht als
Projektinhalt verteilt.

## Danksagung

Mein besonderer Dank gilt **Constantin Grosch <groschorama@gmail.com>**, der mir
für ungefähr **1,5 Jahre** kostenlos einen R-Net Programmer Dongle zur Verfügung
gestellt hat. Diese Leihgabe ermöglichte die Protokollanalyse,
Vergleichstests und Entwicklungsarbeit.

Außerdem danke ich **ChatGPT von OpenAI** für die Unterstützung bei Protokoll-
und Loganalysen, Fehlersuche, Dokumentation und Coding.
