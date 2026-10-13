#include "cyclic/Cyclic02000300.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>

namespace rnetemu {

Cyclic02000300::Cyclic02000300(Sender sender, SentCallback sentCallback)
    : CyclicMessageThread("0x02000300u", std::chrono::milliseconds(10),
                          std::move(sender), std::move(sentCallback))
{
}

void Cyclic02000300::setPosition(int x, int y) noexcept
{
    x_.store(std::clamp(x, -100, 100), std::memory_order_relaxed);
    y_.store(std::clamp(y, -100, 100), std::memory_order_relaxed);
}

CanMessage Cyclic02000300::buildMessage() const
{
    CanMessage message = extendedMessage(0x02000300u, {0x00, 0x00});
    const int x = x_.load(std::memory_order_relaxed);
    const int guiY = y_.load(std::memory_order_relaxed);
    const int canY = -guiY; // Qt +Y is down, R-Net JSM +Y is forward/up.
    message.data[0] = static_cast<std::uint8_t>(static_cast<std::int8_t>(x));
    message.data[1] = static_cast<std::uint8_t>(static_cast<std::int8_t>(canY));
    return message;
}

} // namespace rnetemu
