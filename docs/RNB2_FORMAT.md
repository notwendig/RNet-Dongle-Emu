# RNB2 state format

`rnet-block-state.bin` is the emulator's persistent repository overlay. All
integers are little-endian.

## File header

| Offset | Size | Meaning |
|---:|---:|---|
| 0 | 4 | ASCII `RNB2` |
| 4 | 4 | record count (`uint32`) |

## Record

| Size | Meaning |
|---:|---|
| 4 | repository selector |
| 4 | flags (`bit 0`: ODI 0x89 is valid) |
| 4 | ODI `0x89` value |
| 4 | payload size |
| N | payload bytes |

RNB1 is accepted by the emulator for backwards compatibility but does not carry
ODI `0x89` metadata. New state files are written as RNB2.

The state file is runtime/user data and is ignored by Git.
