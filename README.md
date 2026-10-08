# RNet-Dongle-Emu

FTD2XX/R-Net Programmer interoperability emulator and analysis toolkit.

The project provides a clean-room 32-bit `ftd2xx.dll` replacement for the
examined R-Net Programmer environment. It supports a self-contained emulator
backend and a SocketCAN backend for controlled bench interoperability work.

> **Research / interoperability project.**
> This is not a safety certification and must not be used to approve a mobility
> device for operation. Device-specific real-wheelchair wiring, live programming
> procedures and the Dealer-to-OEM dongle switch are intentionally not
> documented here. See [SECURITY.md](SECURITY.md).

## Current status — 2026-10-08

The emulator path has now been exercised successfully end-to-end:

- **connect: OK**
- **read: OK**
- **write: OK**

This result applies to the emulator/test setup. It is not a claim that writing
to a real wheelchair has been validated or is safe.

A separate real-CAN test with a **You-Q test wheelchair** has previously
confirmed that the Programmer can read the wheelchair configuration through a
CAN interface. The device-specific connection procedure and the method for
switching from a Dealer dongle to an OEM dongle remain outside this repository
for safety and liability reasons.

## Runtime backends

The backend is selected in `ftd2xx-emu.ini`.

### `Device=emu`

Self-contained emulator mode. The DLL provides the R-Net/FTD2XX behavior needed
by the Programmer without a physical wheelchair bus.

### `Device=canN`

SocketCAN mode. `canN` is the Linux CAN interface used by the backend, for
example `can0`.

A typical controlled test topology is:

```text
R-Net Programmer
       |
   ftd2xx.dll
       |
  Device=can0
       |
      can0
       |
   CAN test pair
       |
      can1
       |
 rollstuhl.emu
```

The same generic SocketCAN transport can be used with authorized bench hardware,
but real-device setup details are deliberately not part of the public
documentation.

## What is included

- `src/ftd2xx_emu.cpp` — 32-bit FTD2XX-compatible emulator DLL.
- `Device=emu` — self-contained R-Net Programmer emulation.
- `Device=canN` — SocketCAN transport for bench interoperability.
- `rollstuhl.emu/` — software counterpart for CAN-pair testing.
- Replay/stateful repository handling for POP configuration transactions.
- Persistent RNB2 repository state.
- `tools/rnd-decrypt.c` — decryptor for the examined
  `Generic_V33_1_1245.rnd` database generation.
- `tools/rnet-bin2md.py` — `.R-net`/RNB2 to Markdown inventory generator.
- CMake/MinGW32 build support, tests and Wine deployment helpers.

## Repository layout

```text
src/                    FTD2XX emulator DLL
rollstuhl.emu/          software R-Net/CAN test counterpart
cmake/toolchains/       MinGW32 cross-compilation toolchain
tools/                  analysis/decryption utilities
tests/                  parser and protocol tests
docs/                   protocol, format, validation and compatibility notes
examples/               configuration examples
scripts/                deployment, release and cleanup helpers
run/                    runtime helpers
.github/workflows/      CI
```

## Dependencies

On Fedora:

```bash
sudo dnf install cmake ninja-build mingw32-gcc-c++ gcc openssl-devel make python3
```

The Programmer process is 32-bit, therefore the FTD2XX replacement is built for
Windows/i686.

## Build

```bash
cmake --preset mingw32-release
cmake --build --preset mingw32-release
```

Expected DLL:

```text
build/mingw32/ftd2xx.dll
```

The project is intended to work with VS Code and CMake Tools using the
`mingw32-release` preset.

## Configure

`ftd2xx-emu.ini` selects the backend:

```ini
Device=emu
```

or, for example:

```ini
Device=can0
```

Replay/state files are local runtime data and are not distributed as repository
content.

Important local files can include:

- `rnet-replay.txt`
- `rnet-block-state.bin`

The cleanup helper intentionally preserves those files unless an explicit
full-purge mode is selected.

## Wine deployment

Use the existing project deployment helper for the local Programmer directory.
Close the Programmer before replacing or relinking the DLL.

## RND analysis

The examined parameter database is `Generic_V33_1_1245.rnd`.

Build the decryptor:

```bash
make -C tools
```

Use only a legally obtained local database file:

```bash
tools/rnd-decrypt \
  Generic_V33_1_1245.rnd \
  Generic_V33_1_1245.dec.bin
```

See [docs/RND_FORMAT.md](docs/RND_FORMAT.md).

## Markdown configuration report

Saved Programmer configuration:

```bash
python3 tools/rnet-bin2md.py \
  example.R-net \
  report.md \
  --rnd Generic_V33_1_1245.dec.bin
```

Emulator state:

```bash
python3 tools/rnet-bin2md.py \
  rnet-block-state.bin \
  report.md \
  --rnd Generic_V33_1_1245.dec.bin
```

## Validation and compatibility

- [Validation](docs/VALIDATION.md)
- [Compatibility](docs/COMPATIBILITY.md)
- [Protocol notes](docs/PROTOCOL.md)
- [RNB2 state format](docs/RNB2_FORMAT.md)
- [RND analysis notes](docs/RND_FORMAT.md)

## Data and licensing boundary

No vendor executable, vendor DLL, original/decrypted `.rnd` database, customer
`.R-net` file, replay capture or device state is distributed as project content.
See [NOTICE.md](NOTICE.md).

Project source code is licensed under the MIT License. Third-party software,
data, product names and trademarks remain subject to their respective owners'
terms.

## Screenshots

### R-Net Programmer with the emulated dongle

![R-Net Programmer with the emulated dongle](docs/images/rnet-programmer-connected.png)

### R-Net Programmer application information

![R-Net Programmer OEM Generic application information](docs/images/programmer-about.png)

## Acknowledgements

Special thanks to **Constantin Grosch <groschorama@gmail.com>**, who provided an
R-Net Programmer dongle free of charge for approximately 1.5 years. That loan
made the protocol analysis, comparison testing and development work possible.

Thanks also to **ChatGPT by OpenAI** for assistance with protocol/log analysis,
debugging, documentation and coding during development.
