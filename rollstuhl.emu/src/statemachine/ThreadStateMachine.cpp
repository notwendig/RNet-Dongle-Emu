#include "statemachine/ThreadStateMachine.h"

#include <utility>

namespace rnetemu {

ThreadStateMachine::ThreadStateMachine(std::string name)
    : name_(std::move(name))
{
}

ThreadStateMachine::~ThreadStateMachine()
{
    stop();
}

void ThreadStateMachine::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_)
        return;
    running_ = true;
    thread_ = std::thread(&ThreadStateMachine::run, this);
}

void ThreadStateMachine::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_)
            return;
        running_ = false;
        queue_.clear();
    }
    condition_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void ThreadStateMachine::submit(const ProtocolEvent& event)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_)
            return;
        queue_.push_back(event);
    }
    condition_.notify_one();
}

std::uint64_t ThreadStateMachine::consumedCount() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return consumed_;
}

std::uint64_t ThreadStateMachine::discardedCount() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return discarded_;
}

void ThreadStateMachine::discard(const ProtocolEvent&)
{
}

void ThreadStateMachine::run()
{
    for (;;) {
        ProtocolEvent event;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return !running_ || !queue_.empty(); });
            if (!running_)
                return;
            event = queue_.front();
            queue_.pop_front();
        }

        const bool accepted = matches(event);
        if (accepted)
            consume(event);
        else
            discard(event);

        std::lock_guard<std::mutex> lock(mutex_);
        if (accepted)
            ++consumed_;
        else
            ++discarded_;
    }
}

} // namespace rnetemu
