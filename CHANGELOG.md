# Changelog

All notable changes to this project are documented here.

## 0.1.0 - 2026-10-05

### Added

- 32-bit FTD2XX compatibility DLL for offline R-Net Programmer emulation.
- Replay scheduler and stateful POP repository overlay.
- RNB2 persistent block-state format with repository metadata.
- Stateful read/write support including segmented POP transfers.
- Dynamic ODI `0x86`, `0x89`, `0x8A`, and `0x8B` responses.
- RND Blowfish decryptor for the examined Generic V33.1.1245 database.
- `rnet-bin2md.py` complete Markdown inventory generator for the 13 main
  Programmer configuration sections.
- MinGW32 CMake presets, CI, unit tests, safety/legal documentation.
