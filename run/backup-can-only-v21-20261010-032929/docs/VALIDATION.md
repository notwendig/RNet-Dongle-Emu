# Validation status

## Confirmed emulator result — 2026-10-08

The current emulator/test setup completes the R-Net Programmer workflow
end-to-end:

- **connect: OK**
- **read: OK**
- **write: OK**

The Programmer can connect to the emulated environment, read configuration data
and complete a write transaction without the earlier read/write stall.

The project supports two backend forms:

- `Device=emu` for self-contained emulation.
- `Device=canN` for a SocketCAN interface such as `can0`.

For controlled CAN-pair testing, `rollstuhl.emu` can be used as the software
counterpart on the second CAN interface.

## Stateful configuration behavior

The emulator tracks POP repository selectors, accepts segmented repository
writes, stores modified data in the local RNB2 state and serves subsequent reads
from the updated state instead of blindly returning stale replay data.

## Real CAN boundary

A separate authorized test with a You-Q test wheelchair confirmed that the
Programmer can read the real wheelchair configuration through a CAN interface.

No claim is made here that writes to a real wheelchair are validated or safe.
Device-specific live-chair wiring/programming instructions and the
Dealer-to-OEM dongle switch are intentionally excluded.

This document records interoperability results only; it is not a safety
certification or product-conformance statement.
