# Contributing

Contributions are welcome when they improve reproducibility, documentation,
format parsing, tests, or offline emulator compatibility.

## Ground rules

- Do not commit proprietary vendor binaries or databases.
- Do not commit personal/customer configuration files or unsanitized captures.
- Add tests for parsers whenever practical.
- Keep the physical-device boundary explicit: this repository targets offline
  emulation/interoperability, not live wheelchair control.
- Preserve unknown fields as unknown rather than guessing semantics.

## Development checks

```bash
python3 -m unittest discover -s tests -v
python3 -m py_compile tools/rnet-bin2md.py
make -C tools
cmake --preset mingw32-release
cmake --build --preset mingw32-release
```
