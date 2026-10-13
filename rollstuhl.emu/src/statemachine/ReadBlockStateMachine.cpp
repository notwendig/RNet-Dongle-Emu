#include "statemachine/ReadBlockStateMachine.h"

#include "statemachine/ProtocolClassifier.h"

#include <utility>

namespace rnetemu {

ReadBlockStateMachine::ReadBlockStateMachine(std::shared_ptr<SessionState> session,
                                             ReplayMatcher matcher,
                                             ReplayConsumer consumer)
    : ThreadStateMachine("ReadBlockStateMachine"),
      session_(std::move(session)),
      matcher_(std::move(matcher)),
      consumer_(std::move(consumer))
{
}

ReadBlockProtocolPhase ReadBlockStateMachine::phase() const noexcept
{
    return phase_.load(std::memory_order_acquire);
}

std::uint64_t ReadBlockStateMachine::ackCount() const noexcept
{
    return ackCount_.load(std::memory_order_acquire);
}

void ReadBlockStateMachine::reset() noexcept
{
    phase_.store(ReadBlockProtocolPhase::Idle, std::memory_order_release);
    ackCount_.store(0, std::memory_order_release);
}

bool ReadBlockStateMachine::matches(const ProtocolEvent& event)
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
    if (p == ReadBlockProtocolPhase::Idle && isReadBlockStart(event.message))
        return matcher_ && matcher_(event.message);

    // Continuation ACKs and TC_END are protocol-control frames. They must be
    // consumed by the ReadBlock state machine even when the static replay has
    // no direct response for them. In particular, captured 80/8C TC_END frames
    // normally have no CAN reply and are therefore removed from ReplayMap by
    // loadReplayMap(). Gating TC_END on matcher_ leaves the state machine in
    // Transfer forever and blocks every following ReadBlock.
    if (p == ReadBlockProtocolPhase::Transfer &&
        isReadBlockContinuation(event.message))
        return true;

    if (p == ReadBlockProtocolPhase::Transfer &&
        isReadBlockFinalize(event.message))
        return true;

    return false;
}

void ReadBlockStateMachine::consume(const ProtocolEvent& event)
{
    if (event.type != ProtocolEventType::CanRx) {
        reset();
        return;
    }

    if (isReadBlockStart(event.message)) {
        session_->markConnected(event.generation);
        if (!session_->beginActivity(ProtocolActivity::ReadBlock, event.generation))
            return;
        ackCount_.store(0, std::memory_order_release);
        phase_.store(ReadBlockProtocolPhase::Transfer, std::memory_order_release);
        if (consumer_)
            consumer_(event.message, event.generation, name());
        return;
    }

    if (phase() == ReadBlockProtocolPhase::Transfer &&
        isReadBlockContinuation(event.message)) {
        // A read can contain any number of 1E3F window ACKs. The old V50 code
        // incorrectly assumed the first ACK meant "await final".
        ackCount_.fetch_add(1, std::memory_order_acq_rel);
        // A window ACK can have a replay-backed response (next window / CRC),
        // but state progression itself must not depend on that lookup.
        if (matcher_ && matcher_(event.message) && consumer_)
            consumer_(event.message, event.generation, name());
        return;
    }

    if (phase() == ReadBlockProtocolPhase::Transfer &&
        isReadBlockFinalize(event.message)) {
        // TC_END often has intentionally no direct response in the trace.
        // Consume it anyway so the activity is released for the next block.
        if (matcher_ && matcher_(event.message) && consumer_)
            consumer_(event.message, event.generation, name());
        session_->endActivity(ProtocolActivity::ReadBlock, event.generation);
        phase_.store(ReadBlockProtocolPhase::Complete, std::memory_order_release);
        reset();
    }
}

} // namespace rnetemu
