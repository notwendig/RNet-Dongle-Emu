# Pure-Dongle architecture

R-Net Programmer -> DongleInterface.dll -> ftd2xx.dll -> CAN -> rollstuhl.emu

App valid Raw24 -> CRawMsg -> CUSBMsg / CCANMsg -> POP: CPOPMsg
USB             -> local Dongle handler -> Raw24 TX checksum -> App
CAN TX          -> CCANMsg/CPOPMsg -> CAN
CAN RX          -> CCANMsg/CPOPMsg -> CRawMsg -> Raw24 TX checksum -> App
Invalid wire    -> JUNK/NOISE -> discard

Wheelchair/POP transaction state exists only in rollstuhl.emu.
