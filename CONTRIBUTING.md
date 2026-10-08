# Contributing

Contributions are welcome when they improve reproducibility, tests,
documentation, parsing, emulator compatibility or controlled bench
interoperability.

## Ground rules

- Do not commit proprietary vendor binaries or databases.
- Do not commit customer/personal configuration files or unsanitized captures.
- Keep `Device=emu` and `Device=canN` behavior clearly separated in code and
  documentation.
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
cmake --preset mingw32-release
cmake --build --preset mingw32-release
```

Before committing, run:

```bash
bash scripts/cleanup-nonproject.sh --check
```

Use `--delete` only after reviewing the displayed list.
