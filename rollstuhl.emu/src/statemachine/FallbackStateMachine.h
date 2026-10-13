#pragma once

#include "session/SessionState.h"
#include "statemachine/ProtocolCallbacks.h"
#include "statemachine/ThreadStateMachine.h"

#include <memory>

namespace rnetemu {

class FallbackStateMachine final : public ThreadStateMachine {
public:
    FallbackStateMachine(std::shared_ptr<SessionState> session,
                         ReplayMatcher matcher,
                         ReplayConsumer consumer);

protected:
    bool matches(const ProtocolEvent& event) override;
    void consume(const ProtocolEvent& event) override;

private:
    std::shared_ptr<SessionState> session_;
    ReplayMatcher matcher_;
    ReplayConsumer consumer_;
};

} // namespace rnetemu
