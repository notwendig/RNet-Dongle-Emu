#pragma once

#include "cyclic/CyclicMessageThread.h"

namespace rnetemu {

class Cyclic0C140000 final : public CyclicMessageThread {
public:
    Cyclic0C140000(Sender sender, SentCallback sentCallback = {});

protected:
    CanMessage buildMessage() const override;
};

} // namespace rnetemu
