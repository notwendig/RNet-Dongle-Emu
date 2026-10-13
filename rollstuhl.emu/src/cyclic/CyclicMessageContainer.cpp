#include "cyclic/CyclicMessageContainer.h"

namespace rnetemu {

CyclicMessageContainer::~CyclicMessageContainer()
{
    stopAll();
}

CyclicMessageThread* CyclicMessageContainer::add(std::unique_ptr<CyclicMessageThread> message)
{
    if (!message)
        return nullptr;
    auto* raw = message.get();
    messages_.push_back(std::move(message));
    return raw;
}

void CyclicMessageContainer::startAll()
{
    for (auto& message : messages_)
        message->start();
}

void CyclicMessageContainer::stopAll()
{
    for (auto& message : messages_)
        message->stop();
}

void CyclicMessageContainer::setEnabled(bool enabled)
{
    for (auto& message : messages_)
        message->setEnabled(enabled);
}

} // namespace rnetemu
