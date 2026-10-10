# Protocol and backend notes

## Architecture

The project presents the FTD2XX API expected by the examined R-Net Programmer
and supports two runtime backends.

### `Device=emu`

The DLL provides the required dongle behavior internally. Startup, framed
traffic, repository reads and repository writes are handled by the emulator
state/replay logic.

### `Device=canN`

The DLL routes the R-Net/CAN side through the selected Linux SocketCAN interface,
for example `can0`.

A controlled software test setup can use a paired CAN interface with
`rollstuhl.emu` on the other side.

## Observed PC-to-dongle framing

Two transmitted wire forms are handled:

- `10 02` + one 19-byte raw message + XOR + `10 FE`
- `10 01` + three compact 14-byte CAN messages + XOR + `10 FE`

The compact multi-message form is important for segmented POP transfers.

## Stateful POP repository handling

The emulator tracks repository selectors and stores successfully written blocks
so a later read returns the updated data rather than stale replay content.

The observed read sequence includes:

1. repository selector
2. metadata queries
3. block-read open
4. segmented payload transfer
5. acknowledgements
6. final CRC
7. post-data metadata

ODI metadata required by the Programmer is generated from the maintained state
where necessary.

## Validation boundary

As of 2026-10-08 the emulator path completes connect, read and write.

The generic SocketCAN transport is part of the test architecture, but this
documentation intentionally does not provide device-specific real-wheelchair
programming instructions.
