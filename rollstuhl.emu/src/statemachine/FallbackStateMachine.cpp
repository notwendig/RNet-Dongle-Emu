#include "statemachine/FallbackStateMachine.h"

#include "statemachine/ProtocolClassifier.h"

#include <utility>

namespace rnetemu {

FallbackStateMachine::FallbackStateMachine(std::shared_ptr<SessionState> session,
                                           ReplayMatcher matcher,
                                           ReplayConsumer consumer)
    : ThreadStateMachine("FallbackStateMachine"),
      session_(std::move(session)),
      matcher_(std::move(matcher)),
      consumer_(std::move(consumer))
{
}

bool FallbackStateMachine::matches(const ProtocolEvent& event)
{
    if (event.type != ProtocolEventType::CanRx || !session_ ||
        !event.powerOnAtDispatch ||
        !session_->generationCurrent(event.generation) ||
        isNamedTransactionRequest(event.message))
        return false;

    return matcher_ && matcher_(event.message);
}

void FallbackStateMachine::consume(const ProtocolEvent& event)
{
    // The first valid programmer request may arrive before CONNECT's worker
    // has processed its copy of the same fan-out event. Do not drop that first
    // response merely because connectedAtDispatch was still false.
    session_->markConnected(event.generation);
    if (consumer_)
        consumer_(event.message, event.generation, name());
}

} // namespace rnetemu
