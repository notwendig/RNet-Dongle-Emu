#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>

namespace rnetemu {

struct CanMessage {
    std::uint32_t id = 0;
    bool extended = false;
    bool remote = false;
    std::uint8_t dlc = 0;
    std::array<std::uint8_t, 8> data{};
};

inline CanMessage extendedMessage(std::uint32_t id,
                                  std::initializer_list<std::uint8_t> bytes)
{
    CanMessage message;
    message.id = id;
    message.extended = true;
    message.dlc = static_cast<std::uint8_t>(std::min<std::size_t>(8, bytes.size()));
    std::copy_n(bytes.begin(), message.dlc, message.data.begin());
    return message;
}

} // namespace rnetemu
