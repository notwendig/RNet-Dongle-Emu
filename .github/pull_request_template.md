## Summary

Describe the protocol/build/UI change.

## Architecture boundary

- [ ] FTD/Dongle code remains transport-focused.
- [ ] Wheelchair protocol/state changes are in `rollstuhl.emu`.
- [ ] Documentation is updated when behaviour or CLI changes.

## Verification

- [ ] `bash -n start.sh`
- [ ] Relevant component builds successfully
- [ ] Relevant logs/CAN captures checked
- [ ] Power-Off behaviour checked if affected

## Safety

Describe any effect on physical-hardware use or Power-On/Power-Off behaviour.
