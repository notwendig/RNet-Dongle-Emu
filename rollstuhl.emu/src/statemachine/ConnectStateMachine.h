#pragma once

#include "session/SessionState.h"
#include "statemachine/ProtocolCallbacks.h"
#include "statemachine/ThreadStateMachine.h"

#include <atomic>
#include <memory>

namespace rnetemu {

enum class ConnectProtocolPhase : unsigned char {
    Waiting,
    Connected
};

class ConnectStateMachine final : public ThreadStateMachine {
public:
    // Matcher/consumer remain in the signature so main.cpp does not need a
    // compatibility-only rewrite. CONNECT itself never consumes replay data;
    // the same RX frame is still handled by the concrete transaction/fallback.
    ConnectStateMachine(std::shared_ptr<SessionState> session,
                        ReplayMatcher matcher,
                        ReplayConsumer consumer);

    ConnectProtocolPhase phase() const noexcept;

protected:
    bool matches(const ProtocolEvent& event) override;
    void consume(const ProtocolEvent& event) override;

private:
    void reset() noexcept;

    std::shared_ptr<SessionState> session_;
    std::atomic<ConnectProtocolPhase> phase_{ConnectProtocolPhase::Waiting};
};

} // namespace rnetemu
