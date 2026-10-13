#pragma once

#include "statemachine/ThreadStateMachine.h"

namespace rnetemu {

class PowerOnStateMachine final : public ThreadStateMachine {
public:
    PowerOnStateMachine();

protected:
    bool matches(const ProtocolEvent& event) override;
    void consume(const ProtocolEvent& event) override;
};

} // namespace rnetemu
