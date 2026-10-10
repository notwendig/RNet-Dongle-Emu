## 2026-10-10 — V28 branding resource fix

- fixed Qt resource aliases for the bundled free R-Net branding assets
- added fallback loading from filesystem paths for app icon and GUI logo
- switched the emulator header to the actual logo graphic with text fallback

## 2026-10-10 — V27 GUI logo + stronger window icon

- set the `rollstuhl.emu` main window icon explicitly in addition to the QApplication icon
- added the **free R-Net** logo visibly to the emulator GUI header
- updated README/README.de/ANWENDUNG branding notes accordingly

## 2026-10-10 — V26 branding integration

- added bundled **free R-Net** branding assets under `assets/branding/` and `docs/assets/`
- embedded the logo into `README.md` and `README.de.md`
- added `docs/BRANDING.md` with usage notes for docs/app integration
- added a Qt resource file and set the `rollstuhl.emu` window/app icon to the bundled free R-Net icon

# Changelog

## V25 — 2026-10-10

- Installer-DLL-Prüfung repariert: keine `strings | grep -q`-Pipeline mehr unter `set -o pipefail`.
- PE-Marker werden direkt mit `grep -aFq` geprüft; ein erfolgreicher Treffer kann dadurch nicht mehr durch SIGPIPE/Exit 141 als Fehler erscheinen.
- Keine Änderung am CAN-Protokoll oder an der V23-Rollstuhl-Emulation.

## v24 - 2026-10-10

- Repariert: vollständige CMake-Buildstruktur wird wieder mitgeliefert.
- `CMakeLists.txt`, `CMakePresets.json`, MinGW32-Toolchain sowie die CMake-Dateien für Proxy und `rollstuhl.emu` sind Teil des Pakets.
- CAN-only Root-Build kopiert kein DLL-lokales Replay mehr.
- Cleanup schützt die Build-Infrastruktur auch dann, wenn sie lokal noch nicht von Git getrackt wird.


## Development snapshot - 2026-10-10 (V23)

- `rollstuhl.emu` starts with power **OFF**.
- No normal wheelchair cyclic CAN traffic is emitted before the user presses ON/OFF.
- The existing captured PowerOn sequence is used when switching on.


All notable project changes are documented here.

## Development snapshot - 2026-10-10 (V22)

### Changed

- Corrected CJSM2 joystick Y polarity at CAN encoding: GUI up/forward now sends positive R-Net Y.
- Added CAN ON sequence to `rollstuhl.emu` using the observed `00C`/`00E`/`7B3`/network-start prefix before cyclic traffic.
- Added CAN OFF sequence: normal cyclic traffic stops immediately, the captured `002`/`002#R` + `004` sleep cadence runs for about 11.05 s, then `000`/`000#R` closes the sequence and CAN stays silent.
- Added explicit SocketCAN RTR transmission support for the power sequence.

## Development snapshot - 2026-10-10

### Changed

- Removed the DLL-local `Device=emu` wheelchair/replay backend from the runtime architecture.
- `ftd2xx.dll` is CAN-only and accepts `Device=canN`.
- `rollstuhl.emu` is the single software wheelchair counterpart on the second CAN interface.
- `start.sh emu` now configures `can0` and `can1` at 125000 bit/s before starting the test environment.
- `start.sh` directly starts and tracks `rnet-can-proxy`, `rollstuhl.emu` and R-Net Programmer.
- Runtime deployment no longer installs a replay file for the DLL.
- Release builds use function/data sections plus linker garbage collection so unreachable legacy local-emulator helpers are discarded.

### Preserved

- Live-gated dongle-local status required by the Programmer.
- SocketCAN proxy transport and the PowerOff TX-silence gate.
- Qt6 CJSM2 `rollstuhl.emu` GUI and CAN-side replay/cyclic behavior.

## Development snapshot - 2026-10-08

### Confirmed

- Emulator connection from the examined R-Net Programmer succeeds.
- Configuration read succeeds.
- Configuration write succeeds.
- The emulator/test workflow therefore completes **connect + read + write**.

### Added / documented

- Explicit backend selection with `Device=emu` and `Device=canN`.
- SocketCAN bench-test topology with `rollstuhl.emu` as a software counterpart.
- Clear separation between emulator validation and real-wheelchair validation.
- Documentation of the previous successful real-CAN configuration read from a
  You-Q test wheelchair without publishing device-specific connection details.
- Repository cleanup helper for tracked backup files and untracked/generated
  files.

### Safety boundary

- No real-wheelchair write-validation claim.
- Device-specific live-wheelchair wiring/programming instructions and the
  Dealer-to-OEM dongle-switch procedure remain intentionally excluded.

## 0.1.0 - 2026-10-05

### Added

- 32-bit FTD2XX compatibility DLL for R-Net Programmer emulation.
- Replay scheduler and stateful POP repository overlay.
- RNB2 persistent block-state format with repository metadata.
- Stateful read/write support including segmented POP transfers.
- Dynamic ODI responses required by the examined workflow.
- RND decryptor for the examined Generic V33.1.1245 database generation.
- Markdown inventory generator for the main Programmer configuration sections.
- MinGW32 CMake presets, CI, unit tests and safety/legal documentation.

## 2026-10-10 — D2XX async events + emu/dev V29

- `RNET-D2XX-ASYNC-EVENT-V29`: RX event notification is edge-triggered on newly queued data and uses the application supplied event handle.
- Event status is stored separately from RX queue occupancy and consumed by `FT_GetStatus`/`FT_GetEventStatus`.
- `RNET-GUI-MODES-V29`: `rollstuhl.emu emu|dev`.
- `start.sh emu|dev|stop|status`.
- `dev` does not load Programmer replay responses.
- Installer only accepts canonical active GUI source paths and never searches `run/backup-*`.

## 2026-10-10 — Clean build V30

- `RNET-ZERO-WARNINGS-V30`: removes the observed MinGW function-pointer and unused-helper warnings.
- `build-clean.sh` configures and builds DLL, CAN proxy and GUI itself.
- All builds use `-Werror`; any compiler warning therefore stops installation.
- Build logs are checked for CMake/compiler warnings and errors.

## 2026-10-10 — Zero-Warning Build V31

- `RNET-ZERO-WARNINGS-V31`.
- Keine globale Abschaltung von `-Wunused-function`.
- Absichtlich nicht referenzierte Legacy-Replay/POP-Einstiegspunkte sind lokal mit `[[maybe_unused]]` markiert.
- `-Werror` bleibt für alle übrigen Warnungen aktiv.
- DLL, `rnet-can-proxy` und `rollstuhl.emu` werden vollständig neu gebaut und die Logs auf 0 warnings / 0 errors geprüft.

## 2026-10-10 — Zero-Warning Build V32

- `RNET-ZERO-WARNINGS-V32`.
- `load_stored_blocks()` als absichtlich unreferenzierten Legacy-Helper dokumentiert.
- Keine globale Warnungsabschaltung; `-Werror` bleibt aktiv.
- Vollständiger Clean-Build von DLL, `rnet-can-proxy` und `rollstuhl.emu`.

## 2026-10-10 — R-Net Log Decode V33

- `RNET-LOG-DECODE-V33`.
- CAN-/R-Net-Frames in Laufzeitlogs erhalten `; <RNetMsgBroker-Kurzdecode>`.
- Decoder verwendet die `R-Net.json` des R-Net Analyzers/RNetMsgBroker; keine parallele Matcher-Tabelle.
- FTD2XX-Rohlog bleibt unverändert; kommentierte Begleitdatei unter `run/ftd2xx-emu.decoded.log`.
- `rnet-log-comment` ist rein textverarbeitend und sendet keine CAN-Frames.
- Patch-/Apply-Backups liegen nur noch unter `/tmp`, nicht im Repository.
- `README.md`, `docs/LOGGING.md` und `docs/BUILD.md` aktualisiert.

## 2026-10-10 — RNetMsgBroker Subproject V35

- `RNET-BROKER-SUBPROJECT-V35`.
- `external/RNetMsgBroker` als CMake-Subprojekt integriert; fehlende Quelle wird reproduzierbar als `v1.0.0` nur unter `/tmp` geholt.
- `rnet-can-proxy` linkt `RNetMsgBroker` direkt.
- CAN-RX/CAN-TX-Kommentare entstehen im Proxy-Prozess; kein `rnet-log-comment`-Prozess mehr.
- `R-Net.json` wird beim Proxy-Build neben das Binary kopiert.
- Decoder-Selbsttest prüft `RNetLampControlStatus`.
- Backups ausschließlich unter `/tmp`.
- README und Doku aktualisiert.

## V37
- Power-Off-Screen: vorhandenes `free-r-net-logo-1254.png` statt Textlogo.
- Logo groß, zentriert und seitenverhältnisgetreu skaliert.
- Runtime-Marker `RNET-FRENET-POWEROFF-V37` für korrekte Binary-Prüfung.
