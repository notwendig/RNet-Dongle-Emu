#pragma once

#include "session/SessionState.h"
#include "statemachine/ProtocolCallbacks.h"
#include "statemachine/ThreadStateMachine.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace rnetemu {

enum class ReadBlockProtocolPhase : unsigned char {
    Idle,
    Transfer,
    Complete
};

class ReadBlockStateMachine final : public ThreadStateMachine {
public:
    ReadBlockStateMachine(std::shared_ptr<SessionState> session,
                          ReplayMatcher matcher,
                          ReplayConsumer consumer);

    ReadBlockProtocolPhase phase() const noexcept;
    std::uint64_t ackCount() const noexcept;

protected:
    bool matches(const ProtocolEvent& event) override;
    void consume(const ProtocolEvent& event) override;

private:
    void reset() noexcept;

    std::shared_ptr<SessionState> session_;
    ReplayMatcher matcher_;
    ReplayConsumer consumer_;
    std::atomic<ReadBlockProtocolPhase> phase_{ReadBlockProtocolPhase::Idle};
    std::atomic<std::uint64_t> ackCount_{0};
};

} // namespace rnetemu
