# Changelog

All notable project changes are documented here.

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
