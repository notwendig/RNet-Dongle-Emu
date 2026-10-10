# Security and safety

This repository is an interoperability/research project. It contains a
replacement FTD2XX/R-Net dongle interface, a native SocketCAN proxy and an
external CAN-side wheelchair emulator, but it is **not** a safety-certified
service or programming system for an operational wheelchair.

## Safety boundary

- The software emulator topology is `Device=canN` plus `rollstuhl.emu` on the
  second CAN interface.
- `Device=emu` is no longer supported by the DLL.
- Use the CAN pair only in a controlled, authorized bench environment.
- Never test experimental programming or control logic on an occupied chair.
- Do not rely on emulator success as evidence that a real-device write is safe.
- Follow manufacturer-approved service procedures for operational mobility
  equipment.

The repository intentionally omits device-specific live-wheelchair wiring,
programming instructions and the procedure used to switch from a Dealer dongle
to an OEM dongle.

## Data handling

Do not publish unsanitized logs, captures, saved `.R-net` configurations,
decrypted parameter databases or other files containing device/customer
identifiers.

Local runtime/research data such as CAN/replay captures, legacy RNB2 state and
proprietary parameter databases must remain outside committed project content.

## Reporting security issues

Use a private GitHub security advisory for security-sensitive code issues rather
than publishing exploit details in a public issue.
