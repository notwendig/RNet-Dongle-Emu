#include "cyclic/Cyclic1C300004.h"

#include <chrono>
#include <utility>

namespace rnetemu {

Cyclic1C300004::Cyclic1C300004(Sender sender, SentCallback sentCallback)
    : CyclicMessageThread("0x1C300004u", std::chrono::milliseconds(1000),
                          std::move(sender), std::move(sentCallback))
{
}

CanMessage Cyclic1C300004::buildMessage() const
{
    return extendedMessage(0x1C300004u, {0xB7, 0xAC, 0x13, 0x00, 0x2F, 0xD2, 0x06, 0x00});
}

} // namespace rnetemu
