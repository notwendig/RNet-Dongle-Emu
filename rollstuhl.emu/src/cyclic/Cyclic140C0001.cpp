#include "cyclic/Cyclic140C0001.h"

#include <chrono>
#include <utility>

namespace rnetemu {

Cyclic140C0001::Cyclic140C0001(Sender sender, SentCallback sentCallback)
    : CyclicMessageThread("0x140C0001u", std::chrono::milliseconds(500),
                          std::move(sender), std::move(sentCallback))
{
}

CanMessage Cyclic140C0001::buildMessage() const
{
    return extendedMessage(0x140C0001u, {0x00, 0x00});
}

} // namespace rnetemu
