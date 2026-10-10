# Validation status

## Confirmed software emulator result

The controlled CAN-pair emulator setup completes the examined R-Net Programmer
workflow:

- **connect: OK**
- **read: OK**
- **write: OK**

The active topology is now exclusively:

```text
Programmer -> CAN-only ftd2xx.dll -> can0 <-> can1 -> rollstuhl.emu
```

`Device=emu` is no longer a supported backend. This makes validation easier to
interpret because all wheelchair CAN behavior comes from one component:
`rollstuhl.emu`.

## Startup validation

`start.sh emu` configures both CAN interfaces at 125000 bit/s, deploys
`Device=can0`, starts the proxy and chair emulator, verifies the localhost proxy
port and then starts the Programmer.

The runtime can be inspected with:

```bash
./start.sh status
```

## DLL boundary

The DLL continues to emulate the FTD2XX/R-Net dongle interface itself, but it no
longer serves configuration/replay data as a local virtual wheelchair target.
CAN requests are forwarded to the external CAN side and replies must arrive from
that side.

## Real CAN boundary

A separate authorized test with a You-Q test wheelchair previously confirmed
that the Programmer can read configuration through a CAN interface.

No claim is made that writes to a real wheelchair are validated or safe.
Device-specific live-chair wiring/programming instructions and the
Dealer-to-OEM dongle switch remain intentionally excluded.
