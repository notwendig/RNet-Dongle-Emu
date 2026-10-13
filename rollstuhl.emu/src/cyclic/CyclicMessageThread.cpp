#include "cyclic/CyclicMessageThread.h"

#include <utility>

namespace rnetemu {

CyclicMessageThread::CyclicMessageThread(std::string name,
                                         std::chrono::milliseconds interval,
                                         Sender sender,
                                         SentCallback sentCallback)
    : name_(std::move(name)),
      interval_(interval),
      sender_(std::move(sender)),
      sentCallback_(std::move(sentCallback))
{
}

CyclicMessageThread::~CyclicMessageThread()
{
    stop();
}

void CyclicMessageThread::start()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_)
        return;
    running_ = true;
    thread_ = std::thread(&CyclicMessageThread::run, this);
}

void CyclicMessageThread::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_)
            return;
        running_ = false;
        enabled_ = false;
    }
    condition_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void CyclicMessageThread::setEnabled(bool enabled)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_ = enabled;
    }
    condition_.notify_all();
}

bool CyclicMessageThread::enabled() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return enabled_;
}

void CyclicMessageThread::run()
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (running_) {
        condition_.wait(lock, [this] { return !running_ || enabled_; });
        if (!running_)
            break;

        auto nextDue = std::chrono::steady_clock::now();
        while (running_ && enabled_) {
            lock.unlock();
            const CanMessage message = buildMessage();
            const SendResult result = sender_ ? sender_(message) : SendResult::Error;
            if (result == SendResult::Sent && sentCallback_)
                sentCallback_(message);
            lock.lock();

            if (!running_ || !enabled_)
                continue;

            nextDue += interval_;
            const auto now = std::chrono::steady_clock::now();
            if (nextDue <= now)
                nextDue = now + interval_;

            condition_.wait_until(lock, nextDue,
                                  [this] { return !running_ || !enabled_; });
        }
    }
}

} // namespace rnetemu
