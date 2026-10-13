#pragma once

#include "cyclic/CyclicMessageThread.h"

namespace rnetemu {

class Cyclic1C0C0000 final : public CyclicMessageThread {
public:
    Cyclic1C0C0000(Sender sender, SentCallback sentCallback = {});

protected:
    CanMessage buildMessage() const override;
};

} // namespace rnetemu
