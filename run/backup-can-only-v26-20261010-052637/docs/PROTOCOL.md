# Protocol and backend notes

## CAN-only architecture

Beginning with `RNET-CAN-ONLY-V21`, the FTD2XX replacement has one runtime
backend form only:

```ini
Device=canN
```

For example, `Device=can0` maps the DLL to the native localhost
`rnet-can-proxy` endpoint for Linux `can0`. The DLL itself cannot open Linux
SocketCAN directly because it runs as PE32 under Wine.

The standard emulator topology is:

```text
Programmer -> ftd2xx.dll -> UDP localhost -> rnet-can-proxy -> can0
                                                               |
                                                          CAN test pair
                                                               |
                                                             can1
                                                               |
                                                        rollstuhl.emu
```

There is no `Device=emu` runtime backend anymore. Wheelchair replay/state,
cyclic frames and CJSM2 GUI events belong to `rollstuhl.emu`, not the DLL.

## What remains local to the DLL

The DLL still implements the FTDI/R-Net **dongle-local** commands expected by
the Programmer: enumeration, EEPROM/firmware/filter control and the observed
FTD2XX behavior. Those are not wheelchair CAN emulation.

The live-gated attached/filter status is emitted only after actual CAN RX is
seen from the external bus. When the CAN side is silent, the DLL does not invent
chair traffic.

## PC-to-dongle framing

Two transmitted wire forms are handled:

- `10 02` + one 19-byte raw message + XOR + `10 FE`
- `10 01` + three compact 14-byte CAN messages + XOR + `10 FE`

CAN payloads extracted from those frames are forwarded through the proxy. CAN
RX from the proxy is converted back into the R-Net dongle wire framing expected
by the Programmer.

## Power-off gate

The native proxy preserves the observed bus rule: after the first standard
`0x002` RTR PowerOff request, DLL-originated CAN TX is suppressed until a fresh
standard data frame `0x00C` marks PowerOn. CAN RX can continue while TX is
silent.

## Validation boundary

The software emulator path completes connect, read and write. This document does
not provide device-specific real-wheelchair programming instructions.
