# Historical implementation notes

This file preserves the detailed incremental implementation notes that were previously kept in the project README.

# DongleInterface

<!-- DONGLEINTERFACE-PURE-DONGLE-CPOP-V1 -->
## Pure-Dongle-/Rollstuhl-Trennung

`R-Net Programmer -> DongleInterface.dll -> ftd2xx.dll -> SocketCAN -> rollstuhl.emu`

- `ftd2xx.dll` enthält nur FTDI-/Dongle-Verhalten und CAN-Transport.
- Gültige `10/02` Raw24 und `10/01` CAN-Bursts werden in `CRawMsg`,
  `CUSBMsg`/`CCANMsg` und bei POP zusätzlich `CPOPMsg` instanziiert.
- Ungültige Wire-Frames werden `JUNK/NOISE` geloggt und verworfen.
- USB-Kommandos werden lokal im Dongle bearbeitet.
- CAN wird transparent übertragen.
- POP-/Block-/Rollstuhl-State-Machines gehören nur in `rollstuhl.emu`.
- `rollstuhl.emu` ist Unterprojekt von `DongleInterface`; seine GUI bleibt gleich.
- CAN RX/TX wird an der SocketCAN-I/O-Grenze als `CCANMsg` bzw. `CPOPMsg`
  instanziiert und mit `std::clog << CPOPMsg` protokolliert.

<!-- NO-AUTO-REWAKE-V1 -->
## Kein automatisches Wieder-Einschalten

Nach einer abgeschlossenen Power-Off-/Sleep-Sequenz bleibt die Emulation aus.

- `0x002 RTR` setzt den Proxy-TX-Gate auf SILENT.
- `0x00C` wird **nicht** mehr als Power-On/Freigabe des DLL-TX interpretiert.
- Nach dem abschließenden `0x000 RTR` setzt `rollstuhl.emu` einen OFF-Latch.
- Nach gesetztem OFF-Latch werden spätere automatische TX-Frames verworfen.
- Ein Neustart von `rollstuhl.emu` beginnt eine neue Sitzung.
- Der bereits funktionierende initiale Connect-/Read-Pfad bleibt unverändert.

<!-- WRITE82-V2 -->
## Write-at-92%-Fix: POP ODI 0x82

Der Programmer sendet beim bisher reproduzierbaren 92%-Abbruch den Quick-Write
`78F#20 82 00 00 30 00 00 00`. `rollstuhl.emu` muss diesen Request quittieren.

- RX: `78F#20 82 00 00 xx xx xx xx`
- TX-Ack: `790#4F 82 00 00 00 00 00 00`
- Logmarker: `[rollstuhl.emu][TX-WRITE82-ACK]`
- Die FTDI-Schicht bleibt transparent; der Request wurde bereits 24/24 Byte geschrieben.
- Nach Power-Off bleibt der CAN-TX durch den OFF-Latch stumm.
- `0x00C` hebt den Proxy-PowerOff-Gate nicht mehr auf.

<!-- WRITE92-STARTOFF-V4 -->
## Write 92 % / Start Power OFF V4

- `ODI 0x82` Quick-Write Ack bleibt erhalten.
- `ODI 0x84 = 1` Quick-Write Ack bleibt erhalten.
- Startzustand ist Power OFF / CAN still.
- Wenn der aktuelle GUI-Source keinen sicher patchbaren Startzustand enthält,
  übernimmt `RNET_STARTOFF_FALLBACK=1` den Start-AUS-Gate im CAN-I/O-Wrapper.
- Die erste automatische `00C`-Wake-Sequenz wird dann unterdrückt.
- Eine spätere explizite GUI-EIN-Sequenz darf den Start-AUS-Gate lösen.
- Nach abgeschlossenem Power-Off bleibt der OFF-Latch bestehen.
- `0x00C` hebt den Proxy-Silent-Latch nicht auf.
<!-- SEGMENTED-WRITE-V5 -->
## POP Segmented Write V5

- `rollstuhl.emu` owns the POP write protocol state; the FTD/DLL remains CAN transport.
- `78F#10 8C ...` starts `SegmentedDownload`.
- Server ready response: `790#4F 8C ... <size> 40`.
- Client segmented frames are buffered on the rollstuhl side.
- End of a block/final data sends `1E410000#`.
- Final `78F#80 8C ...` is completed with `790#8F 8C ...`.
- Startup stays Power OFF until explicit GUI ON.
- Proxy PowerOff gating is sticky: `0x00C` does not re-enable DLL CAN TX.

<!-- WRITE100-CONNECT1-HARDOFF-V6 -->
## Programmer write completion / first connect / hard Power-Off V6

- First connect: a `20/80=0x1f` received before startup-ready is held until
  the emulated JSM reaches `0x7B0`, then answered once with `4F/80`.
- Segmented `0x8C` write remains on the `rollstuhl.emu` side.
- The complete previously working post-write sequence is implemented:
  `20/89`, `20/88`, `20/8A`, stateful `40/8B`, `20/8F`.
- `40/8B` uses CRC-16/CCITT-FALSE over:
  `key88(4) + value89(u32 LE) + size(u32 LE) + block data`.
- After completed `000#R`, a later automatic `0x00C` is rejected with
  `ENETDOWN` instead of being falsely reported as successfully transmitted.
  The existing TX OFF latch remains the bus-safety fallback.
- FTD2XX remains transport-only; these wheelchair protocol states live in
  `rollstuhl.emu`.

<!-- POWER-FSM-MANUAL-ONLY-V41 -->
## Power-FSM V41: ausschließlich manuelles Power-On

- Startzustand ist Power OFF.
- `RNET_STARTOFF_FALLBACK` ist deaktiviert; der erste echte GUI-EIN-Klick
  wird nicht mehr verschluckt.
- Power-On darf nur von `Ein/Aus-Paddle` oder
  `externer Ein/Aus-Schalter` ausgelöst werden.
- Direkte/interne automatische `beginPowerOnSequence()`-Aufrufe werden blockiert.
- Auch `finishPowerOnSequence()` darf den Zustand nur nach einer akzeptierten
  manuellen EIN-Sequenz auf EIN setzen.
- Ein CAN-TX-Fehler während Power-On bricht die Sequenz ab.
- Nach Power-Off bleibt die State-Machine stabil AUS.
- Der HardOff-TX-Latch bleibt zusätzlich als Sicherheitsnetz aktiv.

<!-- COMMON-ONLY-NO-FTD2XX-RNET-CPOP-V1 -->
## Gemeinsame Dongle-/R-Net-Nachrichtenklassen

Die gemeinsame Implementierung liegt ausschließlich unter `common/`:

- `common/DongleMessages.cpp`
- `common/DongleMessages.hpp`
- `common/RNetStream.hpp`

Der frühere Doppelbestand `ftd2xx/rnet-cpop/` wurde entfernt.
`ftd2xx` und `rollstuhl.emu` verwenden die gemeinsamen Klassen aus `common/`.

<!-- VSCODE-ROLLSTUHL-NATIVE-V1 -->
## VS Code / CMake Tools: `rollstuhl.emu`

`rollstuhl.emu` ist eine native Linux/Qt6-Anwendung und wird **nicht** mit der
MinGW32-Toolchain der Wine/FTD2XX-Komponenten gebaut.

Für VS Code liegt deshalb unter `rollstuhl.emu/CMakePresets.json` der Preset
`native-debug`. Er verwendet `/usr/bin/gcc` und `/usr/bin/g++` und baut nach:

`build/rollstuhl.emu-vscode`

Die Workspace-Einstellung `cmake.useCMakePresets=always` sorgt dafür, dass
CMake Tools für dieses Projekt nicht mehr den zuvor ausgewählten MinGW-Kit
verwendet.

Die Wine-/FTD2XX-Komponenten bleiben weiterhin MinGW32-Builds und werden über
ihre eigenen Buildabläufe/Preset-Konfigurationen gebaut.

<!-- START-EMU-REAL-STOP-STATUS-V1 -->
## Starten / Stoppen / Status

Öffentlicher Einstieg:

```text
./start.sh emu
./start.sh real
./start.sh stop
./start.sh status
```

`emu` und `real` führen immer einen vollständigen frischen Ablauf aus:

1. Terminal `reset`
2. laufende Komponenten stoppen
3. `rm -rf build`
4. komplett neu bauen und deployen
5. gewünschten Modus starten

`stop` und `status` bauen nichts.

Der öffentliche Name `real` ersetzt `dev`. Intern wird für den noch
bestehenden Legacy-Startpfad `real -> dev` abgebildet.

<!-- SINGLE-PROJECT-START-V5 -->
## Einheitlicher Start

`start.sh` arbeitet ausschließlich relativ zu seinem eigenen Repository.
Es gibt keinen zweiten/alten Projektpfad und keinen rekursiven Aufruf eines
anderen `start.sh`.

```text
./start.sh emu
./start.sh real
./start.sh stop
./start.sh status
```

`emu` und `real` führen immer aus:

1. Terminal `reset`
2. laufende Projektprozesse direkt beenden
3. `<Projektroot>/build` vollständig löschen
4. Windows-Komponente, SocketCAN-Proxy (falls vorhanden) und `rollstuhl.emu`
   frisch bauen/deployen
5. `candump -tz -ca can0 | tee ~/.rnet.logs/<timestamp>-candump.log`
6. gewünschten Modus starten

`real` ist der öffentliche Name; für `rollstuhl.emu` wird intern weiterhin
dessen Geräteargument `dev` verwendet.

Vorhandene Dateien unter `~/.rnet.logs/` werden nicht gelöscht oder
überschrieben.

<!-- SINGLE-PROJECT-START-V6 -->
## Build-Erkennung im zusammengeführten Repository

`start.sh` setzt nicht mehr voraus, dass die Windows-Komponente direkt im
Projektroot liegt. Für den frischen Build wird rekursiv, außerhalb von
`.git/` und `build/`, in dieser Reihenfolge gesucht:

1. `CMakePresets.json` mit `mingw32-release`
2. `build-wine32.sh`
3. passendes `CMakeLists.txt`
4. `runtime-complete/build-forwarder.sh`

Auch Install-/Deploy-Skripte werden im Repository gesucht.

Alle übrigen Regeln aus V5 bleiben bestehen: ein Repository, kein Legacy-
`start.sh`, kein Selbstaufruf, `rm -rf build`, candump mit Zeitstempel und
Erhalt alter Logs.
