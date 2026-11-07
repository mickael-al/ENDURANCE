#ifndef ENDURANCE_PHYSICS_WORKER_LOOP_HPP
#define ENDURANCE_PHYSICS_WORKER_LOOP_HPP

#include "CommandQueue.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>

namespace Ge
{
    // Scheduling uses integer steady-clock deadlines. Bullet receives the same
    // float step every time, never a rounded elapsed duration or a zero step.
    class PhysicsTickClock
    {
    public:
        using Clock = std::chrono::steady_clock;
        static constexpr float StepSeconds = 1.0f / 60.0f;
        static constexpr unsigned MaximumCatchUpSteps = 4;

        explicit PhysicsTickClock(Clock::time_point now)
            : m_next(now + period()) {}

        static Clock::duration period() noexcept
        {
            return std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / 60.0));
        }

        Clock::time_point deadline() const noexcept { return m_next; }

        unsigned consume(Clock::time_point now) noexcept
        {
            if (now < m_next) return 0;
            const auto due = (now - m_next) / period() + 1;
            // Preserve the fractional phase, but drop old work after a long
            // debugger pause or stall instead of spinning through a backlog.
            m_next += period() * due;
            return static_cast<unsigned>((std::min)(due,
                static_cast<Clock::duration::rep>(MaximumCatchUpSteps)));
        }

    private:
        Clock::time_point m_next;
    };

    template<class ProcessCommands, class Step>
    void runPhysicsWorkerLoop(CommandQueue& queue, const std::atomic<bool>& running,
        ProcessCommands&& processCommands, Step&& step)
    {
        PhysicsTickClock ticks(PhysicsTickClock::Clock::now());
        while (running.load(std::memory_order_acquire))
        {
            // Commands wake immediately (including requests awaited by a
            // future). An idle worker sleeps until the next simulation tick.
            queue.waitUntil(ticks.deadline(), running);
            if (!running.load(std::memory_order_acquire)) break;
            processCommands();
            const unsigned due = ticks.consume(PhysicsTickClock::Clock::now());
            for (unsigned i = 0; i < due && running.load(std::memory_order_acquire); ++i)
                step(PhysicsTickClock::StepSeconds);
        }
    }
}
#endif
