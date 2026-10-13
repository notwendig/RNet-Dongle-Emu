#pragma once

#include "cyclic/CyclicMessageThread.h"

namespace rnetemu {

class Cyclic14300000 final : public CyclicMessageThread {
public:
    Cyclic14300000(Sender sender, SentCallback sentCallback = {});

protected:
    CanMessage buildMessage() const override;
};

} // namespace rnetemu
