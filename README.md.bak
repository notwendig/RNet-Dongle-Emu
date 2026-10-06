# RNet-Dongle-Emu

Offline FTD2XX/R-Net Programmer interoperability emulator plus tools for
inspecting PGDT R-Net parameter/configuration data.

> **Research / interoperability project.** No physical wheelchair CAN/USB
> backend is included. Do not use this project to approve or tune an operational
> mobility device. See [SECURITY.md](SECURITY.md).

## What is included

- **`ftd2xx.dll` emulator** — a 32-bit clean-room FTD2XX-compatible DLL for the
  examined R-Net Programmer environment.
- **Replay + stateful repository overlay** — replays captured startup traffic,
  accepts repository writes, persists them as RNB2, and serves later reads from
  the updated state.
- **`rnd-decrypt`** — reproduces the Blowfish decryption used by the examined
  Generic V33.1.1245 parameter database.
- **`rnet-bin2md.py`** — converts a saved `.R-net` file or RNB2 overlay into a
  Markdown inventory using a locally decrypted RND database.

The analyzer covers the complete main Programmer tree:

`Profile Management`, `Configuration`, `Speeds`, `Controls`, `Latched`,
`Seating`, `Motor`, `Input Output Module`, `Omni`, `Mouse 1`, `Mouse 2`,
`iDevice1`, and `iDevice2`.

![R-Net Programmer configuration tree](docs/images/programmer-tree.png)

## Repository layout

```text
src/                    FTD2XX emulator DLL
cmake/toolchains/       MinGW32 cross-compilation toolchain
tools/rnd-decrypt.c     RND decryptor for the tested database generation
tools/rnet-bin2md.py    R-Net/RNB2 -> Markdown analyzer
tests/                  dependency-free parser tests
docs/                   protocol, RND/RNB2 and compatibility notes
examples/               configuration/replay format examples only
scripts/                Wine deployment and release packaging helpers
.github/workflows/      CI for Python, decryptor and MinGW32 DLL
```

## Dependencies

On Fedora:

```bash
sudo dnf install cmake ninja-build mingw32-gcc-c++ gcc openssl-devel make python3
```

Package names differ on other distributions. The emulator target must be
32-bit Windows/i686 because the tested application is a 32-bit process.

## Build the emulator DLL

```bash
cmake --preset mingw32-release
cmake --build --preset mingw32-release
```

Output:

```text
build/mingw32/ftd2xx.dll
```

The project works well with VS Code + CMake Tools: select the
`mingw32-release` configure/build preset.

## Configure the emulator

Copy `examples/ftd2xx-emu.ini.example` to `ftd2xx-emu.ini` next to the DLL.
Replay mode expects an authorized local `rnet-replay.txt` capture. Runtime
writes are stored in `rnet-block-state.bin`.

Neither file is included in the repository.

## Optional Wine deployment helper

```bash
scripts/deploy-wine.sh "/path/to/the/Programmer/directory"
```

The helper backs up an existing `ftd2xx.dll` and installs a symlink to the
new build. Close the Programmer before changing its DLL.

## Decrypt your local RND database

The tested database is `Generic_V33_1_1245.rnd`. Build:

```bash
make -C tools
```

Then, using your own legally obtained file:

```bash
tools/rnd-decrypt \
  Generic_V33_1_1245.rnd \
  Generic_V33_1_1245.dec.bin
```

The decryptor is intentionally version-specific. See
[docs/RND_FORMAT.md](docs/RND_FORMAT.md).

## Generate a complete Markdown report

For a saved Programmer configuration:

```bash
python3 tools/rnet-bin2md.py \
  example.R-net \
  report.md \
  --rnd Generic_V33_1_1245.dec.bin
```

For an emulator RNB2 overlay:

```bash
python3 tools/rnet-bin2md.py \
  rnet-block-state.bin \
  report.md \
  --rnd Generic_V33_1_1245.dec.bin
```

If the RNB2 file is only an overlay and you also have a complete saved file:

```bash
python3 tools/rnet-bin2md.py \
  rnet-block-state.bin \
  report.md \
  --base example.R-net \
  --rnd Generic_V33_1_1245.dec.bin
```

The report lists all RND dictionary definitions under the 13 main Programmer
sections and adds confirmed repository selectors/NV offsets where the RND uses
ordinary NV-location records. Unknown packed layouts stay marked as unknown;
the tool does not invent offsets.

## Tests

```bash
python3 -m unittest discover -s tests -v
python3 -m py_compile tools/rnet-bin2md.py
make -C tools
```

CI additionally cross-builds the 32-bit Windows DLL with MinGW.

## Current interoperability results

The tested snapshot supports the complete offline flow needed during this
research: Programmer startup, dongle re-enumeration, stateful repository write,
persistent RNB2 storage and subsequent stateful read-back. Details are in
[docs/VALIDATION.md](docs/VALIDATION.md).

Compatibility information is documented in
[docs/COMPATIBILITY.md](docs/COMPATIBILITY.md).

## Data and licensing boundary

No PGDT executable, DLL, `.rnd` database, decrypted database, `.R-net` customer
file, replay capture, or device state is distributed here. See
[NOTICE.md](NOTICE.md).

Project source code is licensed under the MIT License. Third-party software,
data and trademarks remain subject to their respective owners' terms.

> **Replay note:** `rnet-replay.txt` is capture-specific and is intentionally not
> included in the public repository. `scripts/deploy-wine.sh` reuses an existing
> valid replay or accepts `RNET_REPLAY_FILE=/path/to/rnet-replay.txt`.
