# Contributing to freeR-Net

Thank you for helping improve the project.

## Before opening a change

Please keep the architecture boundary intact:

- transport/framing/FTD2XX work belongs under `ftd2xx/`;
- shared message representation belongs under `common/`;
- wheelchair protocol/state behaviour belongs under `rollstuhl.emu/`;
- SocketCAN bridging belongs under `tools/rnet-can-proxy/`.

## Protocol changes

For changes based on observed R-Net traffic, include:

1. mode (`emu` or `real`);
2. exact CAN interface(s);
3. relevant `candump` extract;
4. relevant `ftd2xx-emu` / `rollstuhl.emu` log extract;
5. expected request/response sequence;
6. whether the change affects Power-On/Power-Off behaviour.

Do not include private personal data in logs.

## Build check

A normal development check is:

```bash
bash -n start.sh

cmake -S . --preset mingw32-release
cmake --build --preset mingw32-release --parallel

cmake -S tools/rnet-can-proxy -B build/rnet-can-proxy -G Ninja
cmake --build build/rnet-can-proxy --parallel

cmake -S rollstuhl.emu -B build/rollstuhl.emu -G Ninja
cmake --build build/rollstuhl.emu --parallel
```

## Pull requests

Keep pull requests focused. Update documentation when protocol, CLI, safety or
runtime behaviour changes.
