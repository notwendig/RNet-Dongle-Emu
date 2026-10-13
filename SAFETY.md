# Safety

freeR-Net is an engineering/research project. It is not a certified wheelchair
controller, medical device, safety controller or replacement for manufacturer
service tools.

## Public repository boundary

The public repository intentionally does not include:

- instructions for binding a CAN interface to a physical R-Net wheelchair;
- procedures for converting/switching dealer hardware to OEM programmer mode;
- claims that protocol emulation is safe for unattended mobility use.

## Power-Off invariant

A completed Power-Off must leave DLL→CAN transmission silent for the remainder of
that proxy session. A later `0x00C` must not automatically re-enable traffic.

Changes that weaken this invariant should not be merged without dedicated bench
captures and review.

## Physical hardware

Use on physical hardware requires independent risk assessment, supervision and a
safe test environment. Do not rely on this software as the sole means of stopping,
controlling or protecting a wheelchair or occupant.
