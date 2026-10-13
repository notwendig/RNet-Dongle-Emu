#include "cyclic/Cyclic1C0C0000.h"

#include <chrono>
#include <utility>

namespace rnetemu {

Cyclic1C0C0000::Cyclic1C0C0000(Sender sender, SentCallback sentCallback)
    : CyclicMessageThread("0x1C0C0000u", std::chrono::milliseconds(1000),
                          std::move(sender), std::move(sentCallback))
{
}

CanMessage Cyclic1C0C0000::buildMessage() const
{
    return extendedMessage(0x1C0C0000u, {0x28});
}

} // namespace rnetemu
