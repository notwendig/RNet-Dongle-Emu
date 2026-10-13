#include "statemachine/PowerOnStateMachine.h"

#include "statemachine/ProtocolClassifier.h"

namespace rnetemu {

PowerOnStateMachine::PowerOnStateMachine()
    : ThreadStateMachine("PowerOnStateMachine")
{
}

bool PowerOnStateMachine::matches(const ProtocolEvent& event)
{
    if (event.type == ProtocolEventType::PowerOnBegin ||
        event.type == ProtocolEventType::PowerOnComplete)
        return true;

    // 00C is a connection-test/wake frame, not an unconditional ON command.
    // MainWindow only turns it into PowerOnBegin while the emulated chair is OFF.
    return event.type == ProtocolEventType::CanRx &&
           isCanConnectionTest(event.message);
}

void PowerOnStateMachine::consume(const ProtocolEvent&)
{
    // SessionState is changed synchronously by StateMachineContainer. This
    // worker records/owns the Power-On protocol phase without racing the gate.
}

} // namespace rnetemu
