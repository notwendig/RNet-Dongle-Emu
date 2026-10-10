# RNB2 state format (legacy)

`rnet-block-state.bin` was the persistent repository state/overlay used by the
earlier DLL-local wheelchair/replay emulator. **RNET-CAN-ONLY-V21 does not use
this file at runtime.** Configuration/replay state now belongs to the external
CAN-side `rollstuhl.emu` or the connected CAN device.

The format is documented here only for compatibility with existing research
data. All integer fields are little-endian.

## File header

| Offset | Size | Meaning |
|---:|---:|---|
| 0 | 4 | ASCII `RNB2` |
| 4 | 4 | record count (`uint32`) |

## Record

| Size | Meaning |
|---:|---|
| 4 | repository selector |
| 4 | flags |
| 4 | ODI `0x89` value |
| 4 | payload size |
| N | payload bytes |

Flag bit 0 indicates that ODI `0x89` metadata is valid.

RNB1 can be accepted by historical emulator versions for backwards
compatibility but does not carry the same ODI metadata.

The cleanup helper continues to preserve `rnet-block-state.bin` in normal
cleanup mode so an upgrade does not destroy older research state.
