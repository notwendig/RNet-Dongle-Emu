#pragma once

#include "cyclic/CyclicMessageThread.h"

namespace rnetemu {

class Cyclic1C300004 final : public CyclicMessageThread {
public:
    Cyclic1C300004(Sender sender, SentCallback sentCallback = {});

protected:
    CanMessage buildMessage() const override;
};

} // namespace rnetemu
