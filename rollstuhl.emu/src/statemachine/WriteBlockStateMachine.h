#pragma once

#include "session/SessionState.h"
#include "statemachine/ProtocolCallbacks.h"
#include "statemachine/ThreadStateMachine.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace rnetemu {

enum class WriteBlockProtocolPhase : unsigned char {
    Idle,
    Armed,
    Receiving,
    AwaitEnd,
    Complete
};

class WriteBlockStateMachine final : public ThreadStateMachine {
public:
    WriteBlockStateMachine(std::shared_ptr<SessionState> session,
                           ReplayMatcher matcher,
                           ReplayConsumer consumer);

    WriteBlockProtocolPhase phase() const noexcept;
    std::uint32_t expectedSize() const noexcept;
    std::uint32_t receivedBytes() const noexcept;

protected:
    bool matches(const ProtocolEvent& event) override;
    void consume(const ProtocolEvent& event) override;

private:
    void reset() noexcept;
    static std::uint32_t u32le(const CanMessage& message, unsigned offset) noexcept;

    std::shared_ptr<SessionState> session_;
    ReplayMatcher matcher_;
    ReplayConsumer consumer_;
    std::atomic<WriteBlockProtocolPhase> phase_{WriteBlockProtocolPhase::Idle};
    std::atomic<std::uint32_t> expectedSize_{0};
    std::atomic<std::uint32_t> receivedBytes_{0};
};

} // namespace rnetemu
