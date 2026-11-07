#ifndef GE_LATEST_SNAPSHOT_HPP
#define GE_LATEST_SNAPSHOT_HPP

#include <array>
#include <atomic>
#include <cstdint>
#include <utility>

namespace Ge
{

// A single-producer/single-consumer mailbox. Intermediate publications may be
// discarded: the consumer acquires the latest complete snapshot, not a queue.
// Exactly one thread owns the producer role and one owns the consumer role.
// The payload must not contain references to mutable storage shared by slots.
template<class T>
class LatestSnapshot
{
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "LatestSnapshot requires a lock-free 32-bit atomic");

public:
    LatestSnapshot() = default;
    LatestSnapshot(const LatestSnapshot&) = delete;
    LatestSnapshot& operator=(const LatestSnapshot&) = delete;
    LatestSnapshot(LatestSnapshot&&) = delete;
    LatestSnapshot& operator=(LatestSnapshot&&) = delete;

    // Set sizes/capacities or initial values before either participant starts.
    // The initializer runs once on each independent payload. Never call this
    // concurrently with writer(), reader(), publish(), or tryConsume().
    template<class F>
    void initializeAll(F&& initialize)
    {
        for (auto& slot : m_slots)
            initialize(slot.value);
    }

    // Producer only. This reference expires at publish(): the old writer slot
    // may immediately be acquired and read by the consumer.
    T& writer() noexcept { return m_slots[m_writer].value; }

    // Producer only. Release publishes all payload writes; acquire permits the
    // returned middle slot to be reused after the consumer released it. A newer
    // publication replaces an unread one without waiting or copying a payload.
    void publish() noexcept
    {
        const auto previous = m_middle.exchange(m_writer | dirtyBit,
                                                 std::memory_order_acq_rel);
        m_writer = previous & indexMask;
    }

    // Consumer only. If no publication is pending, the current reader remains
    // unchanged. Only this consumer clears dirtyBit, so a dirty load guarantees
    // the following exchange acquires a published slot, even if the producer
    // publishes again between the two operations.
    bool tryConsume() noexcept
    {
        if ((m_middle.load(std::memory_order_acquire) & dirtyBit) == 0)
            return false;
        const auto previous = m_middle.exchange(m_reader,
                                                 std::memory_order_acq_rel);
        m_reader = previous & indexMask;
        return true;
    }

    // Consumer only. Stable, including any vector storage owned by this slot,
    // until the next successful tryConsume(). Do not retain references/pointers
    // past that call: the released slot may then be written by the producer.
    const T& reader() const noexcept { return m_slots[m_reader].value; }

private:
    static constexpr std::uint32_t indexMask = 3u;
    static constexpr std::uint32_t dirtyBit = 4u;

    // Slot and role padding avoids false sharing of their small mutable fields
    // on the usual 64-byte cache-line platforms; correctness does not depend on
    // that cache-line size.
    struct alignas(64) Slot { T value{}; };
    std::array<Slot, 3> m_slots{};
    alignas(64) std::uint32_t m_writer = 0;
    alignas(64) std::uint32_t m_reader = 1;
    alignas(64) std::atomic<std::uint32_t> m_middle{2};

    // Ownership invariant: writer, reader, and middle are three distinct slots.
    // An atomic exchange transfers one slot in each direction. Release/acquire
    // makes payload access happen-before its next owner, including returning a
    // reader to the producer. No thread accesses a slot while it is in middle.
};

} // namespace Ge

#endif
