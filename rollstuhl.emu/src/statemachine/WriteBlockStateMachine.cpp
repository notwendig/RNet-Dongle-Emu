#include "statemachine/WriteBlockStateMachine.h"

#include "statemachine/ProtocolClassifier.h"

#include <algorithm>
#include <utility>

namespace rnetemu {

WriteBlockStateMachine::WriteBlockStateMachine(std::shared_ptr<SessionState> session,
                                               ReplayMatcher matcher,
                                               ReplayConsumer consumer)
    : ThreadStateMachine("WriteBlockStateMachine"),
      session_(std::move(session)),
      matcher_(std::move(matcher)),
      consumer_(std::move(consumer))
{
}

WriteBlockProtocolPhase WriteBlockStateMachine::phase() const noexcept
{
    return phase_.load(std::memory_order_acquire);
}

std::uint32_t WriteBlockStateMachine::expectedSize() const noexcept
{
    return expectedSize_.load(std::memory_order_acquire);
}

std::uint32_t WriteBlockStateMachine::receivedBytes() const noexcept
{
    return receivedBytes_.load(std::memory_order_acquire);
}

std::uint32_t WriteBlockStateMachine::u32le(const CanMessage& message,
                                            unsigned offset) noexcept
{
    if (offset + 3u >= message.dlc)
        return 0;
    return static_cast<std::uint32_t>(message.data[offset]) |
           (static_cast<std::uint32_t>(message.data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(message.data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(message.data[offset + 3]) << 24);
}

void WriteBlockStateMachine::reset() noexcept
{
    phase_.store(WriteBlockProtocolPhase::Idle, std::memory_order_release);
    expectedSize_.store(0, std::memory_order_release);
    receivedBytes_.store(0, std::memory_order_release);
}

bool WriteBlockStateMachine::matches(const ProtocolEvent& event)
{
    if (event.type == ProtocolEventType::PowerOnBegin ||
        event.type == ProtocolEventType::PowerOffBegin ||
        event.type == ProtocolEventType::PowerOffComplete)
        return true;

    if (event.type != ProtocolEventType::CanRx || !session_ ||
        !event.powerOnAtDispatch ||
        !session_->generationCurrent(event.generation))
        return false;

    const auto p = phase();

    if (p == WriteBlockProtocolPhase::Idle && isWriteBlockArm(event.message))
        return matcher_ && matcher_(event.message);

    if (p == WriteBlockProtocolPhase::Armed &&
        isWriteBlockDownloadStart(event.message))
        return true;

    if ((p == WriteBlockProtocolPhase::Receiving ||
         p == WriteBlockProtocolPhase::AwaitEnd) &&
        isWriteBlockSegment(event.message))
        return true;

    if ((p == WriteBlockProtocolPhase::Receiving ||
         p == WriteBlockProtocolPhase::AwaitEnd) &&
        isWriteBlockFinish(event.message))
        return true;

    return false;
}

void WriteBlockStateMachine::consume(const ProtocolEvent& event)
{
    if (event.type != ProtocolEventType::CanRx) {
        reset();
        return;
    }

    if (isWriteBlockArm(event.message)) {
        session_->markConnected(event.generation);
        if (!session_->beginActivity(ProtocolActivity::WriteBlock, event.generation))
            return;

        expectedSize_.store(u32le(event.message, 4), std::memory_order_release);
        receivedBytes_.store(0, std::memory_order_release);
        phase_.store(WriteBlockProtocolPhase::Armed, std::memory_order_release);
        if (consumer_)
            consumer_(event.message, event.generation, name());
        return;
    }

    if (phase() == WriteBlockProtocolPhase::Armed &&
        isWriteBlockDownloadStart(event.message)) {
        phase_.store(WriteBlockProtocolPhase::Receiving, std::memory_order_release);
        if (matcher_ && matcher_(event.message) && consumer_)
            consumer_(event.message, event.generation, name());
        return;
    }

    if ((phase() == WriteBlockProtocolPhase::Receiving ||
         phase() == WriteBlockProtocolPhase::AwaitEnd) &&
        isWriteBlockSegment(event.message)) {
        const std::uint32_t now =
            receivedBytes_.fetch_add(event.message.dlc, std::memory_order_acq_rel) +
            event.message.dlc;
        const std::uint32_t expected = expectedSize();
        if (expected != 0 && now >= expected)
            phase_.store(WriteBlockProtocolPhase::AwaitEnd, std::memory_order_release);

        // Most data segments have no immediate response; window-ending segments
        // can still be replay-backed. Only consume the replay when one exists.
        if (matcher_ && matcher_(event.message) && consumer_)
            consumer_(event.message, event.generation, name());
        return;
    }

    if ((phase() == WriteBlockProtocolPhase::Receiving ||
         phase() == WriteBlockProtocolPhase::AwaitEnd) &&
        isWriteBlockFinish(event.message)) {
        if (matcher_ && matcher_(event.message) && consumer_)
            consumer_(event.message, event.generation, name());
        session_->endActivity(ProtocolActivity::WriteBlock, event.generation);
        phase_.store(WriteBlockProtocolPhase::Complete, std::memory_order_release);
        reset();
    }
}

} // namespace rnetemu
