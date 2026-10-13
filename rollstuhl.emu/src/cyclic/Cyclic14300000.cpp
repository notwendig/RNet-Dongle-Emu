#include "cyclic/Cyclic14300000.h"

#include <chrono>
#include <utility>

namespace rnetemu {

Cyclic14300000::Cyclic14300000(Sender sender, SentCallback sentCallback)
    : CyclicMessageThread("0x14300000u", std::chrono::milliseconds(200),
                          std::move(sender), std::move(sentCallback))
{
}

CanMessage Cyclic14300000::buildMessage() const
{
    return extendedMessage(0x14300000u, {0x00, 0x00});
}

} // namespace rnetemu
