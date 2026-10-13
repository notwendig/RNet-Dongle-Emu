#pragma once

#include "statemachine/ProtocolEvent.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace rnetemu {

class ThreadStateMachine {
public:
    explicit ThreadStateMachine(std::string name);
    virtual ~ThreadStateMachine();

    ThreadStateMachine(const ThreadStateMachine&) = delete;
    ThreadStateMachine& operator=(const ThreadStateMachine&) = delete;

    void start();
    void stop();

    // Every event is copied into every state-machine queue. Every worker wakes
    // and independently decides consume/discard.
    void submit(const ProtocolEvent& event);

    const std::string& name() const noexcept { return name_; }
    std::uint64_t consumedCount() const noexcept;
    std::uint64_t discardedCount() const noexcept;

protected:
    virtual bool matches(const ProtocolEvent& event) = 0;
    virtual void consume(const ProtocolEvent& event) = 0;
    virtual void discard(const ProtocolEvent& event);

private:
    void run();

    const std::string name_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<ProtocolEvent> queue_;
    std::thread thread_;
    bool running_ = false;
    std::uint64_t consumed_ = 0;
    std::uint64_t discarded_ = 0;
};

} // namespace rnetemu
