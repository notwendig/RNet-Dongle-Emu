# Local input files

The tools need data that is intentionally not committed to this repository.
Typical local files are:

- `Generic_V33_1_1245.rnd` — legally obtained original parameter database
- `Generic_V33_1_1245.dec.bin` — local output from `rnd-decrypt`
- `example.R-net` — your own saved Programmer configuration
- `rnet-replay.txt` — your own authorized replay capture
- `rnet-block-state.bin` — runtime state created by the emulator

Keep these files outside version control. The root `.gitignore` excludes the
common names/extensions.
