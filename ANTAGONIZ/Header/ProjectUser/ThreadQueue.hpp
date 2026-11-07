#ifndef __THREAD_QUEUE__
#define __THREAD_QUEUE__

#include <mutex>
#include <queue>
#include <condition_variable>

template<typename T>
class ThreadQueue
{
public:
    void push(const T& value)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stop)
            {
                return;
            }
            queue.push(value);
        }
        cv.notify_one();
    }

    bool pop(T& value)
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&]() { return !queue.empty() || stop; });

        if (stop)
        {
            return false;
        }

        value = queue.front();
        queue.pop();
        return true;
    }

    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop = true;
            std::queue<T> empty;
            queue.swap(empty);
        }
        cv.notify_all();
    }

private:
    std::queue<T> queue;
    std::mutex mutex;
    std::condition_variable cv;
    bool stop = false;
};

#endif //!__THREAD_QUEUE__