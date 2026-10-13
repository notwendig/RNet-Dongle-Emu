#include "statemachine/PowerOffStateMachine.h"

#include "statemachine/ProtocolClassifier.h"

namespace rnetemu {

PowerOffStateMachine::PowerOffStateMachine()
    : ThreadStateMachine("PowerOffStateMachine")
{
}

PowerOffProtocolPhase PowerOffStateMachine::phase() const noexcept
{
    return phase_.load(std::memory_order_acquire);
}

bool PowerOffStateMachine::matches(const ProtocolEvent& event)
{
    if (event.type == ProtocolEventType::PowerOffBegin ||
        event.type == ProtocolEventType::PowerOffComplete ||
        event.type == ProtocolEventType::PowerOnBegin)
        return true;

    if (event.type != ProtocolEventType::CanRx)
        return false;

    return isPowerOffPrelude(event.message) ||
           isPowerOffRequest(event.message) ||
           isSleepingAck(event.message) || isSleepAllRtr(event.message);
}

void PowerOffStateMachine::consume(const ProtocolEvent& event)
{
    if (event.type == ProtocolEventType::PowerOnBegin) {
        phase_.store(PowerOffProtocolPhase::Idle, std::memory_order_release);
        return;
    }
    if (event.type == ProtocolEventType::PowerOffBegin) {
        phase_.store(PowerOffProtocolPhase::Barrier, std::memory_order_release);
        return;
    }
    if (event.type == ProtocolEventType::PowerOffComplete) {
        phase_.store(PowerOffProtocolPhase::Complete, std::memory_order_release);
        return;
    }

    if (isPowerOffRequest(event.message)) {
        phase_.store(PowerOffProtocolPhase::Barrier, std::memory_order_release);
    } else if (isSleepingAck(event.message) || isSleepAllRtr(event.message)) {
        phase_.store(PowerOffProtocolPhase::SleepingAck, std::memory_order_release);
    } else if (isPowerOffPrelude(event.message)) {
        if (event.message.id == 0x004u)
            phase_.store(PowerOffProtocolPhase::Sleeping, std::memory_order_release);
        else
            phase_.store(PowerOffProtocolPhase::PreludeObserved, std::memory_order_release);
    }
}

} // namespace rnetemu
