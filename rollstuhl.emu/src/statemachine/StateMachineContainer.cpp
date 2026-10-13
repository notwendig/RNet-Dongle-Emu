#include "statemachine/StateMachineContainer.h"

#include <stdexcept>

namespace rnetemu {

StateMachineContainer::StateMachineContainer(std::shared_ptr<SessionState> session)
    : session_(std::move(session))
{
    if (!session_)
        throw std::invalid_argument("StateMachineContainer requires SessionState");
}

StateMachineContainer::~StateMachineContainer()
{
    stopAll();
}

ThreadStateMachine* StateMachineContainer::add(std::unique_ptr<ThreadStateMachine> stateMachine)
{
    if (!stateMachine)
        return nullptr;
    auto* raw = stateMachine.get();
    stateMachines_.push_back(std::move(stateMachine));
    return raw;
}

void StateMachineContainer::startAll()
{
    for (auto& stateMachine : stateMachines_)
        stateMachine->start();
}

void StateMachineContainer::stopAll()
{
    for (auto& stateMachine : stateMachines_)
        stateMachine->stop();
}

ProtocolEvent StateMachineContainer::makeEvent(ProtocolEventType type,
                                                const CanMessage& message)
{
    ProtocolEvent event;
    event.type = type;
    event.message = message;
    event.generation = session_->generation();
    event.sequence = sequence_.fetch_add(1, std::memory_order_relaxed) + 1;
    event.powerOnAtDispatch = session_->powerOn();
    event.connectedAtDispatch = session_->connected();
    return event;
}

void StateMachineContainer::broadcast(const ProtocolEvent& event)
{
    for (auto& stateMachine : stateMachines_)
        stateMachine->submit(event);
}

void StateMachineContainer::dispatch(const CanMessage& message)
{
    broadcast(makeEvent(ProtocolEventType::CanRx, message));
}

std::uint64_t StateMachineContainer::powerOnBegin()
{
    const auto generation = session_->beginPowerOn();
    powerTransitionGeneration_.store(generation, std::memory_order_release);
    broadcast(makeEvent(ProtocolEventType::PowerOnBegin));
    return generation;
}

void StateMachineContainer::powerOnComplete()
{
    const auto generation = powerTransitionGeneration_.load(std::memory_order_acquire);
    session_->completePowerOn(generation);
    broadcast(makeEvent(ProtocolEventType::PowerOnComplete));
}

std::uint64_t StateMachineContainer::powerOffBegin()
{
    const auto generation = session_->beginPowerOff();
    powerTransitionGeneration_.store(generation, std::memory_order_release);
    broadcast(makeEvent(ProtocolEventType::PowerOffBegin));
    return generation;
}

void StateMachineContainer::powerOffComplete()
{
    const auto generation = powerTransitionGeneration_.load(std::memory_order_acquire);
    session_->completePowerOff(generation);
    broadcast(makeEvent(ProtocolEventType::PowerOffComplete));
}

} // namespace rnetemu
