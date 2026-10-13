#include "statemachine/ProtocolClassifier.h"

namespace rnetemu {

namespace {

bool is78f(const CanMessage& message) noexcept
{
    return !message.extended && !message.remote && message.id == 0x78Fu &&
           message.dlc >= 2;
}

bool isPop(const CanMessage& message, unsigned char tc, unsigned char odi) noexcept
{
    return is78f(message) && message.data[0] == tc && message.data[1] == odi;
}

bool is1eFamily(const CanMessage& message) noexcept
{
    return message.extended && !message.remote &&
           ((message.id & 0x1F000000u) == 0x1E000000u);
}

} // namespace

bool isCanConnectionTest(const CanMessage& message) noexcept
{
    return !message.extended && !message.remote && message.id == 0x00Cu;
}

bool isPowerOnRequest(const CanMessage& message) noexcept
{
    return isCanConnectionTest(message);
}

bool isPowerOffRequest(const CanMessage& message) noexcept
{
    // Verified real-bus barrier: only the RTR variant means "PM sleep all".
    return !message.extended && message.remote && message.id == 0x002u;
}

bool isPowerOffPrelude(const CanMessage& message) noexcept
{
    // Observed around shutdown but NOT a cancellation barrier by itself:
    // 002# = JSM init-seen, 004# = JSM sleep commencing.
    return !message.extended && !message.remote &&
           (message.id == 0x002u || message.id == 0x004u);
}

bool isSleepingAck(const CanMessage& message) noexcept
{
    return !message.extended && !message.remote && message.id == 0x000u;
}

bool isSleepAllRtr(const CanMessage& message) noexcept
{
    return !message.extended && message.remote && message.id == 0x000u;
}

bool isProgrammerProtocolRequest(const CanMessage& message) noexcept
{
    // POP quick/control traffic is sent on 0x78F. Segmented transfer traffic
    // uses the 0x1E...... family, including 0x1E3Fxxxx acknowledgements.
    return is78f(message) || is1eFamily(message);
}

bool isRepositorySelect(const CanMessage& message) noexcept
{
    return isPop(message, 0x20u, 0x81u);
}

bool isProgrammerWriteSeenMarker(const CanMessage& message) noexcept
{
    return isPop(message, 0x20u, 0x82u);
}

bool isProgrammerPostWriteMarker(const CanMessage& message) noexcept
{
    return is78f(message) && message.dlc >= 3 &&
           message.data[0] == 0x4Fu && message.data[1] == 0x10u &&
           message.data[2] == 0x02u;
}

bool isReadBlockStart(const CanMessage& message) noexcept
{
    return isPop(message, 0x50u, 0x8Cu);
}

bool isReadBlockContinuation(const CanMessage& message) noexcept
{
    return message.extended && !message.remote &&
           ((message.id & 0x1FFF0000u) == 0x1E3F0000u);
}

bool isReadBlockFinalize(const CanMessage& message) noexcept
{
    return isPop(message, 0x80u, 0x8Cu);
}

bool isReadBlockRequest(const CanMessage& message) noexcept
{
    return isReadBlockStart(message) || isReadBlockContinuation(message) ||
           isReadBlockFinalize(message);
}

bool isWriteBlockArm(const CanMessage& message) noexcept
{
    return isPop(message, 0x20u, 0x82u);
}

bool isWriteBlockDownloadStart(const CanMessage& message) noexcept
{
    return isPop(message, 0x10u, 0x8Cu);
}

bool isWriteBlockSegment(const CanMessage& message) noexcept
{
    if (!is1eFamily(message))
        return false;
    // 1E3Fxxxx is the receiver/window ACK family, not a download data segment.
    return (message.id & 0x1FFF0000u) != 0x1E3F0000u && message.dlc > 0;
}

bool isWriteBlockFinish(const CanMessage& message) noexcept
{
    return isPop(message, 0x80u, 0x8Cu);
}

bool isWriteBlockStart(const CanMessage& message) noexcept
{
    return isWriteBlockArm(message);
}

bool isWriteBlockRequest(const CanMessage& message) noexcept
{
    return isWriteBlockArm(message) || isWriteBlockDownloadStart(message) ||
           isWriteBlockSegment(message) || isWriteBlockFinish(message);
}

bool isNamedTransactionRequest(const CanMessage& message) noexcept
{
    return isReadBlockRequest(message) || isWriteBlockRequest(message) ||
           isPowerOffRequest(message) || isCanConnectionTest(message);
}

} // namespace rnetemu
