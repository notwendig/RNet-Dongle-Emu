# Changelog

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
