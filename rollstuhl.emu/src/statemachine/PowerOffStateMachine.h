#pragma once

#include "statemachine/ThreadStateMachine.h"

#include <atomic>

namespace rnetemu {

enum class PowerOffProtocolPhase : unsigned char {
    Idle,
    PreludeObserved,
    Barrier,
    Sleeping,
    SleepingAck,
    Complete
};

class PowerOffStateMachine final : public ThreadStateMachine {
public:
    PowerOffStateMachine();
    PowerOffProtocolPhase phase() const noexcept;

protected:
    bool matches(const ProtocolEvent& event) override;
    void consume(const ProtocolEvent& event) override;

private:
    std::atomic<PowerOffProtocolPhase> phase_{PowerOffProtocolPhase::Idle};
};

} // namespace rnetemu
