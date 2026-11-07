#include "CommandQueue.hpp"

namespace Ge
{

    void CommandQueue::push(Command* command)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(command);
        cv_.notify_one();
    }

    Command* CommandQueue::pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty(); });
        Command* command = queue_.front();
        queue_.pop();
        return command;
    }

    Command* CommandQueue::tryPop()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return nullptr;
        Command* command = queue_.front();
        queue_.pop();
        return command;
    }

    void CommandQueue::waitUntil(std::chrono::steady_clock::time_point deadline,
        const std::atomic<bool>& running)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_until(lock, deadline, [this, &running] {
            return !queue_.empty() || !running.load(std::memory_order_acquire);
        });
    }

    void CommandQueue::wake()
    {
        // Pair the externally changed stop flag with the wait mutex so a stop
        // notification cannot be lost between the predicate and entering wait.
        std::lock_guard<std::mutex> lock(mutex_);
        cv_.notify_all();
    }

    bool CommandQueue::empty() 
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
}