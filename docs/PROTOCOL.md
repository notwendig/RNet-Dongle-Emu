# Protocol notes

The emulator sits at the **FTD2XX API boundary**, not at a physical CAN bus.
It presents the serial/description expected by the examined application and
replays or synthesizes the framed traffic expected by `DongleInterface.dll`.

## USB/dongle framing observed

Two PC-to-dongle wire forms are handled:

- `10 02` + one 19-byte raw message + XOR + `10 FE` (24 bytes total)
- `10 01` + three compact 14-byte CAN messages + XOR + `10 FE` (47 bytes total)

The compact form is used during segmented repository downloads.

## Stateful POP repository overlay

The emulator tracks selectors and replaces stale replay data with persistent
written repository blocks. The important read flow observed is:

1. select repository entry (`20/81`)
2. read metadata (`40/89`, `40/86`)
3. open block read (`50/8C`)
4. segmented payload transfer and acknowledgements
5. final block CRC
6. read post-data metadata (`40/8A`, `40/8B`)

ODI `0x8B` is reproduced from selector, ODI `0x89`, payload size, and payload
using CRC-16/CCITT-FALSE as implemented in the emulator.

Replay captures are intentionally not distributed with this repository.
