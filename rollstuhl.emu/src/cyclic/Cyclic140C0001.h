#pragma once

#include "cyclic/CyclicMessageThread.h"

namespace rnetemu {

class Cyclic140C0001 final : public CyclicMessageThread {
public:
    Cyclic140C0001(Sender sender, SentCallback sentCallback = {});

protected:
    CanMessage buildMessage() const override;
};

} // namespace rnetemu
