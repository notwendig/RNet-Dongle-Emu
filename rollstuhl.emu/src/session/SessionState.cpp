#include "session/SessionState.h"

namespace rnetemu {

std::uint64_t SessionState::beginPowerOn() noexcept
{
    const auto generation = generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
    connected_.store(false, std::memory_order_release);
    activity_.store(ProtocolActivity::None, std::memory_order_release);
    powerPhase_.store(PowerPhase::Starting, std::memory_order_release);
    return generation;
}

void SessionState::completePowerOn(std::uint64_t generation) noexcept
{
    if (!generationCurrent(generation))
        return;
    powerPhase_.store(PowerPhase::On, std::memory_order_release);
}

std::uint64_t SessionState::beginPowerOff() noexcept
{
    // The first verified 0x002 RTR is the cancellation barrier. This generation
    // change invalidates any answer prepared earlier by ReadBlock/WriteBlock.
    const auto generation = generation_.fetch_add(1, std::memory_order_acq_rel) + 1;
    powerPhase_.store(PowerPhase::Stopping, std::memory_order_release);
    connected_.store(false, std::memory_order_release);
    activity_.store(ProtocolActivity::None, std::memory_order_release);
    return generation;
}

void SessionState::completePowerOff(std::uint64_t generation) noexcept
{
    if (!generationCurrent(generation))
        return;
    powerPhase_.store(PowerPhase::Off, std::memory_order_release);
    connected_.store(false, std::memory_order_release);
    activity_.store(ProtocolActivity::None, std::memory_order_release);
}

std::uint64_t SessionState::generation() const noexcept
{
    return generation_.load(std::memory_order_acquire);
}

bool SessionState::generationCurrent(std::uint64_t generation) const noexcept
{
    return generation_.load(std::memory_order_acquire) == generation;
}

PowerPhase SessionState::powerPhase() const noexcept
{
    return powerPhase_.load(std::memory_order_acquire);
}

bool SessionState::powerOn() const noexcept
{
    return powerPhase() == PowerPhase::On;
}

bool SessionState::txAllowed() const noexcept
{
    return powerOn();
}

bool SessionState::connected() const noexcept
{
    return connected_.load(std::memory_order_acquire);
}

void SessionState::markConnected(std::uint64_t generation) noexcept
{
    if (!generationCurrent(generation) || !powerOn())
        return;
    connected_.store(true, std::memory_order_release);
}

void SessionState::resetConnection() noexcept
{
    connected_.store(false, std::memory_order_release);
}

ProtocolActivity SessionState::activity() const noexcept
{
    return activity_.load(std::memory_order_acquire);
}

bool SessionState::beginActivity(ProtocolActivity activity,
                                 std::uint64_t generation) noexcept
{
    if (!generationCurrent(generation) || !powerOn() ||
        activity == ProtocolActivity::None)
        return false;

    ProtocolActivity expected = ProtocolActivity::None;
    return activity_.compare_exchange_strong(expected, activity,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire);
}

void SessionState::endActivity(ProtocolActivity activity,
                               std::uint64_t generation) noexcept
{
    if (!generationCurrent(generation))
        return;
    ProtocolActivity expected = activity;
    activity_.compare_exchange_strong(expected, ProtocolActivity::None,
                                      std::memory_order_acq_rel,
                                      std::memory_order_acquire);
}

void SessionState::abortActivity() noexcept
{
    activity_.store(ProtocolActivity::None, std::memory_order_release);
}

} // namespace rnetemu
