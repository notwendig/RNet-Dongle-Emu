#pragma once

#include "cyclic/CyclicMessageThread.h"

#include <atomic>

namespace rnetemu {

class Cyclic02000300 final : public CyclicMessageThread {
public:
    Cyclic02000300(Sender sender, SentCallback sentCallback = {});
    void setPosition(int x, int y) noexcept;

protected:
    CanMessage buildMessage() const override;

private:
    std::atomic<int> x_{0};
    std::atomic<int> y_{0};
};

} // namespace rnetemu
