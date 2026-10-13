#include "cyclic/Cyclic0C140100.h"

#include <chrono>
#include <utility>

namespace rnetemu {

Cyclic0C140100::Cyclic0C140100(Sender sender, SentCallback sentCallback)
    : CyclicMessageThread("0x0C140100u", std::chrono::milliseconds(1000),
                          std::move(sender), std::move(sentCallback))
{
}

CanMessage Cyclic0C140100::buildMessage() const
{
    return extendedMessage(0x0C140100u, {0x02});
}

} // namespace rnetemu
