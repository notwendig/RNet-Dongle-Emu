#include "statemachine/ConnectStateMachine.h"

#include "statemachine/ProtocolClassifier.h"

#include <utility>

namespace rnetemu {

ConnectStateMachine::ConnectStateMachine(std::shared_ptr<SessionState> session,
                                         ReplayMatcher,
                                         ReplayConsumer)
    : ThreadStateMachine("ConnectStateMachine"),
      session_(std::move(session))
{
}

ConnectProtocolPhase ConnectStateMachine::phase() const noexcept
{
    return phase_.load(std::memory_order_acquire);
}

void ConnectStateMachine::reset() noexcept
{
    phase_.store(ConnectProtocolPhase::Waiting, std::memory_order_release);
}

bool ConnectStateMachine::matches(const ProtocolEvent& event)
{
    if (event.type == ProtocolEventType::PowerOnBegin ||
        event.type == ProtocolEventType::PowerOffBegin ||
        event.type == ProtocolEventType::PowerOffComplete)
        return true;

    if (event.type != ProtocolEventType::CanRx || !session_ ||
        !event.powerOnAtDispatch || event.connectedAtDispatch ||
        !session_->generationCurrent(event.generation))
        return false;

    return isProgrammerProtocolRequest(event.message);
}

void ConnectStateMachine::consume(const ProtocolEvent& event)
{
    if (event.type != ProtocolEventType::CanRx) {
        reset();
        return;
    }

    // CONNECT is intentionally observational. It must never steal the first
    // POP request: all workers receive the same event and the concrete owner
    // (ReadBlock/WriteBlock/Fallback) remains responsible for the response.
    session_->markConnected(event.generation);
    if (session_->connected())
        phase_.store(ConnectProtocolPhase::Connected, std::memory_order_release);
}

} // namespace rnetemu
