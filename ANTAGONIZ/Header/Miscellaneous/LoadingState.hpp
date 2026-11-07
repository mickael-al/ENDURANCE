#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace Ge
{
    // A loader opens its ticket before launching a worker. Completion covers
    // the resource's main-thread finalization as well as its CPU preparation.
    // Copies share one completion; destroying the last copy also cancels it.
    class LoadingState
    {
    private:
        inline static std::atomic<std::size_t> s_pending{0};
        inline static std::atomic<std::uint64_t> s_revision{0};
        inline static std::atomic<std::uint64_t> s_failures{0};
        inline static std::atomic<std::uint64_t> s_sceneFailureBaseline{0};

        struct State
        {
            std::atomic<bool> completed{false};

            State() noexcept
            {
                s_pending.fetch_add(1, std::memory_order_acq_rel);
                s_revision.fetch_add(1, std::memory_order_release);
            }

            void complete(bool failed = false) noexcept
            {
                if (!completed.exchange(true, std::memory_order_acq_rel))
                {
                    if (failed) s_failures.fetch_add(1, std::memory_order_release);
                    // An acquire read of zero pending work also observes the
                    // revision announcing this completion.
                    s_revision.fetch_add(1, std::memory_order_release);
                    s_pending.fetch_sub(1, std::memory_order_acq_rel);
                }
            }

            ~State() { complete(); }
        };

    public:
        class Ticket
        {
        public:
            Ticket() = default;

            void complete() const noexcept
            {
                if (m_state) m_state->complete();
            }

            void fail() const noexcept
            {
                if (m_state) m_state->complete(true);
            }

            explicit operator bool() const noexcept { return bool(m_state); }

        private:
            friend class LoadingState;
            explicit Ticket(std::shared_ptr<State> state) : m_state(std::move(state)) {}
            std::shared_ptr<State> m_state;
        };

        static Ticket begin() { return Ticket(std::make_shared<State>()); }
        static std::size_t pending() noexcept
        {
            return s_pending.load(std::memory_order_acquire);
        }
        static std::uint64_t revision() noexcept
        {
            return s_revision.load(std::memory_order_acquire);
        }
        static std::uint64_t failures() noexcept
        {
            return s_failures.load(std::memory_order_acquire);
        }
        // Scene scripts can start in any object order. Capture before queuing
        // them so an early loader failure belongs to the initial scene too.
        static void beginScene() noexcept
        {
            s_sceneFailureBaseline.store(failures(), std::memory_order_release);
        }
        static std::uint64_t sceneFailureBaseline() noexcept
        {
            return s_sceneFailureBaseline.load(std::memory_order_acquire);
        }
    };
}
