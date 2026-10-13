#include "cyclic/Cyclic0C140000.h"

#include <chrono>
#include <utility>

namespace rnetemu {

Cyclic0C140000::Cyclic0C140000(Sender sender, SentCallback sentCallback)
    : CyclicMessageThread("0x0C140000u", std::chrono::milliseconds(1000),
                          std::move(sender), std::move(sentCallback))
{
}

CanMessage Cyclic0C140000::buildMessage() const
{
    return extendedMessage(0x0C140000u, {0xC0});
}

} // namespace rnetemu
