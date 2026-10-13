#pragma once

#include "threading/CanMessage.h"

#include <cstdint>

namespace rnetemu {

enum class ProtocolEventType {
    CanRx,
    PowerOnBegin,
    PowerOnComplete,
    PowerOffBegin,
    PowerOffComplete
};

struct ProtocolEvent {
    ProtocolEventType type = ProtocolEventType::CanRx;
    CanMessage message{};
    std::uint64_t generation = 0;
    std::uint64_t sequence = 0;
    bool powerOnAtDispatch = false;
    bool connectedAtDispatch = false;
};

} // namespace rnetemu
