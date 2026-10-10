# Tested compatibility

The implementation has been developed against the following observed software
set:

- Application: **R-Net OEM Generic 6.1.0**
- Executable: **R-net Application V33**, executable version **6.3.8**
- Parameter information: **1.1245**, format version **3**, Generic, English
- Parameter information creation date: **2019-01-23**
- Dongle firmware/version shown by application: **4.75**
- Dongle Interface DLL: **03.02.08.00**
- Dongle Interface Driver: **02.12.36.00**

![Programmer About dialog](images/programmer-about.png)

## Backend compatibility

`RNET-CAN-ONLY-V21` accepts only:

- `Device=canN` — SocketCAN via the native proxy, for example `Device=can0`.

The earlier `Device=emu` DLL-local wheelchair backend is no longer supported.
The software emulator counterpart is `rollstuhl.emu` on a second CAN interface.

The current default test pair is `can0 <-> can1`, classic CAN at 125000 bit/s.
The interface names are configurable through `start.sh` environment variables.

The emulator/test workflow is confirmed to connect, read and write. A separate
real-CAN test previously confirmed reading the configuration of a You-Q test
wheelchair; this is not a real-wheelchair write-validation statement.

Other Programmer, RND or Dongle Interface releases can differ in FTD2XX calls,
startup behavior, CAN ordering, repository layout, RND encryption or parameter
metadata.
