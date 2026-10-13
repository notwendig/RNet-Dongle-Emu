#pragma once

#include "threading/CanMessage.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace rnetemu {

enum class SendResult {
    Sent,
    WouldBlock,
    Error
};

class CyclicMessageThread {
public:
    using Sender = std::function<SendResult(const CanMessage&)>;
    using SentCallback = std::function<void(const CanMessage&)>;

    CyclicMessageThread(std::string name,
                        std::chrono::milliseconds interval,
                        Sender sender,
                        SentCallback sentCallback = {});
    virtual ~CyclicMessageThread();

    CyclicMessageThread(const CyclicMessageThread&) = delete;
    CyclicMessageThread& operator=(const CyclicMessageThread&) = delete;

    void start();
    void stop();
    void setEnabled(bool enabled);
    bool enabled() const noexcept;

    const std::string& name() const noexcept { return name_; }
    std::chrono::milliseconds interval() const noexcept { return interval_; }

protected:
    virtual CanMessage buildMessage() const = 0;

private:
    void run();

    const std::string name_;
    const std::chrono::milliseconds interval_;
    Sender sender_;
    SentCallback sentCallback_;

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::thread thread_;
    bool running_ = false;
    bool enabled_ = false;
};

} // namespace rnetemu
