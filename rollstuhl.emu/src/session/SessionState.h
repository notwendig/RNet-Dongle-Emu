#pragma once

#include <atomic>
#include <cstdint>

namespace rnetemu {

enum class PowerPhase : std::uint8_t {
    Off,
    Starting,
    On,
    Stopping
};

enum class ProtocolActivity : std::uint8_t {
    None,
    Connect,
    ReadBlock,
    WriteBlock,
    Other
};

class SessionState {
public:
    SessionState() = default;

    std::uint64_t beginPowerOn() noexcept;
    void completePowerOn(std::uint64_t generation) noexcept;
    std::uint64_t beginPowerOff() noexcept;
    void completePowerOff(std::uint64_t generation) noexcept;

    std::uint64_t generation() const noexcept;
    bool generationCurrent(std::uint64_t generation) const noexcept;

    PowerPhase powerPhase() const noexcept;
    bool powerOn() const noexcept;
    bool txAllowed() const noexcept;

    bool connected() const noexcept;
    void markConnected(std::uint64_t generation) noexcept;
    void resetConnection() noexcept;

    ProtocolActivity activity() const noexcept;
    bool beginActivity(ProtocolActivity activity, std::uint64_t generation) noexcept;
    void endActivity(ProtocolActivity activity, std::uint64_t generation) noexcept;
    void abortActivity() noexcept;

private:
    std::atomic<std::uint64_t> generation_{1};
    std::atomic<PowerPhase> powerPhase_{PowerPhase::Off};
    std::atomic<bool> connected_{false};
    std::atomic<ProtocolActivity> activity_{ProtocolActivity::None};
};

} // namespace rnetemu
