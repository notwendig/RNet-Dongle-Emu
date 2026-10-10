# Contributing

Contributions are welcome when they improve reproducibility, tests,
documentation, parsing, CAN-only dongle compatibility or controlled bench
interoperability.

## Ground rules

- Do not commit proprietary vendor binaries or databases.
- Do not commit customer/personal configuration files or unsanitized captures.
- Keep the CAN-only architecture explicit: the DLL accepts `Device=canN`;
  software wheelchair behavior belongs in `rollstuhl.emu`.
- Do not reintroduce a DLL-local `Device=emu` wheelchair/replay backend.
- Keep real-wheelchair safety boundaries explicit.
- Do not add device-specific live-chair programming instructions or
  Dealer-to-OEM dongle-switch instructions.
- Preserve unknown fields as unknown instead of inventing semantics.
- Add regression tests for parsers/protocol changes when practical.
- Do not commit temporary patch backups, editor files, build trees or logs.

## Development checks

```bash
python3 -m unittest discover -s tests -v
python3 -m py_compile tools/rnet-bin2md.py
make -C tools
scripts/build-can0-rollstuhl-emu.sh release emu
bash -n start.sh tools/deploy-wine-runtime.sh scripts/build-can0-rollstuhl-emu.sh
```

Before committing, run:

```bash
bash scripts/cleanup-nonproject.sh --check
```

Use `--delete` only after reviewing the displayed list.
