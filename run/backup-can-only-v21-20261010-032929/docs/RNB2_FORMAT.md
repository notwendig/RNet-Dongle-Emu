# RNB2 state format

`rnet-block-state.bin` is the emulator's persistent repository state/overlay.
All integer fields are little-endian.

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

RNB1 can be accepted for backwards compatibility but does not carry the same
ODI metadata. Newly written state uses RNB2.

The state file is local runtime/user data, not repository source. It is used by
the emulator to make writes persistent and to serve later reads from the
updated state.

The cleanup helper preserves `rnet-block-state.bin` in normal cleanup mode.
