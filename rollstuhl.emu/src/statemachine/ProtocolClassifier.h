#pragma once

#include "threading/CanMessage.h"

namespace rnetemu {

// 0x00C is a JSM CAN connection test. In the OFF context it can be used as a
// wake trigger; it is not an unconditional Power-On command.
bool isCanConnectionTest(const CanMessage& message) noexcept;
bool isPowerOnRequest(const CanMessage& message) noexcept; // compatibility alias

// Hard Power-Off barrier: 0x002 RTR only. A plain 0x002 data frame is NOT the
// barrier (it is seen as part of JSM init/sleep traffic in the real trace).
bool isPowerOffRequest(const CanMessage& message) noexcept;
bool isPowerOffPrelude(const CanMessage& message) noexcept;
bool isSleepingAck(const CanMessage& message) noexcept;
bool isSleepAllRtr(const CanMessage& message) noexcept;

bool isProgrammerProtocolRequest(const CanMessage& message) noexcept;

// POP control / repository context.
bool isRepositorySelect(const CanMessage& message) noexcept; // 20/81

// Trace markers used only to recognize the post-write reboot window.
// 20/82 is observed only in the Write-to-R-Net run used for V54 validation.
// 4F/10/02 alone is NOT Power-Off; V54 only uses it after 20/82 was seen.
bool isProgrammerWriteSeenMarker(const CanMessage& message) noexcept; // 20/82
bool isProgrammerPostWriteMarker(const CanMessage& message) noexcept; // 4F/10/02

// Read block: selector is established separately, then 50/8C opens the read,
// 1E3Fxxxx acknowledges a transfer window, and 80/8C closes it.
bool isReadBlockStart(const CanMessage& message) noexcept;
bool isReadBlockContinuation(const CanMessage& message) noexcept;
bool isReadBlockFinalize(const CanMessage& message) noexcept;
bool isReadBlockRequest(const CanMessage& message) noexcept;

// Write block: 20/82 arms size+selector, 10/8C starts segmented download,
// 1Exxxxxx carries segments (except 1E3F ACK family), 80/8C closes it.
bool isWriteBlockArm(const CanMessage& message) noexcept;
bool isWriteBlockDownloadStart(const CanMessage& message) noexcept;
bool isWriteBlockSegment(const CanMessage& message) noexcept;
bool isWriteBlockFinish(const CanMessage& message) noexcept;
bool isWriteBlockStart(const CanMessage& message) noexcept; // compatibility: arm
bool isWriteBlockRequest(const CanMessage& message) noexcept;

bool isNamedTransactionRequest(const CanMessage& message) noexcept;

} // namespace rnetemu
