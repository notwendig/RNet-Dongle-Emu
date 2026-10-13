#pragma once

#include "session/SessionState.h"
#include "statemachine/ThreadStateMachine.h"

#include <atomic>
#include <memory>
#include <vector>

namespace rnetemu {

class StateMachineContainer {
public:
    explicit StateMachineContainer(std::shared_ptr<SessionState> session);
    ~StateMachineContainer();

    StateMachineContainer(const StateMachineContainer&) = delete;
    StateMachineContainer& operator=(const StateMachineContainer&) = delete;

    ThreadStateMachine* add(std::unique_ptr<ThreadStateMachine> stateMachine);
    void startAll();
    void stopAll();

    void dispatch(const CanMessage& message);

    // Power changes are synchronous cancellation barriers first, then they are
    // broadcast so every state machine also wakes for the transition event.
    std::uint64_t powerOnBegin();
    void powerOnComplete();
    std::uint64_t powerOffBegin();
    void powerOffComplete();

    std::shared_ptr<SessionState> session() const noexcept { return session_; }
    std::size_t size() const noexcept { return stateMachines_.size(); }

private:
    ProtocolEvent makeEvent(ProtocolEventType type, const CanMessage& message = {});
    void broadcast(const ProtocolEvent& event);

    std::shared_ptr<SessionState> session_;
    std::vector<std::unique_ptr<ThreadStateMachine>> stateMachines_;
    std::atomic<std::uint64_t> sequence_{0};
    std::atomic<std::uint64_t> powerTransitionGeneration_{1};
};

} // namespace rnetemu
