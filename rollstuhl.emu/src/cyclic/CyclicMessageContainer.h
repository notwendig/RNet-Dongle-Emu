#pragma once

#include "cyclic/CyclicMessageThread.h"

#include <memory>
#include <vector>

namespace rnetemu {

class CyclicMessageContainer {
public:
    CyclicMessageContainer() = default;
    ~CyclicMessageContainer();

    CyclicMessageContainer(const CyclicMessageContainer&) = delete;
    CyclicMessageContainer& operator=(const CyclicMessageContainer&) = delete;

    CyclicMessageThread* add(std::unique_ptr<CyclicMessageThread> message);
    void startAll();
    void stopAll();
    void setEnabled(bool enabled);
    std::size_t size() const noexcept { return messages_.size(); }

private:
    std::vector<std::unique_ptr<CyclicMessageThread>> messages_;
};

} // namespace rnetemu
