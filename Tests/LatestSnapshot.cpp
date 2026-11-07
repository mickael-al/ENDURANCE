#include "Miscellaneous/LatestSnapshot.hpp"

#include <atomic>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    struct Payload
    {
        std::uint64_t sequence = 0;
        std::uint64_t checksum = 0;
        std::vector<std::uint64_t> values;
    };

    std::uint64_t valueAt(std::uint64_t sequence, std::size_t index)
    {
        std::uint64_t value = sequence * 0x9e3779b97f4a7c15ull + index;
        value ^= value >> 30;
        value *= 0xbf58476d1ce4e5b9ull;
        value ^= value >> 27;
        value *= 0x94d049bb133111ebull;
        return value ^ (value >> 31);
    }

    void fill(Payload& payload, std::uint64_t sequence)
    {
        payload.sequence = sequence;
        payload.checksum = 0;
        for (std::size_t index = 0; index < payload.values.size(); ++index)
        {
            const auto value = valueAt(sequence, index);
            payload.values[index] = value;
            payload.checksum ^= value;
        }
    }

    void check(const Payload& payload)
    {
        const auto sequence = payload.sequence;
        std::uint64_t checksum = 0;
        for (std::size_t index = 0; index < payload.values.size(); ++index)
        {
            const auto expected = valueAt(sequence, index);
            require(payload.values[index] == expected, "Torn snapshot payload");
            checksum ^= expected;
        }
        require(payload.checksum == checksum, "Torn snapshot checksum");
        require(payload.sequence == sequence, "Reader changed while being read");
    }

    void initialize(Ge::LatestSnapshot<Payload>& mailbox, std::size_t size)
    {
        unsigned initialized = 0;
        mailbox.initializeAll([&](Payload& payload)
        {
            payload.values.resize(size);
            fill(payload, 0);
            ++initialized;
        });
        require(initialized == 3, "Initializer must visit every independent slot");
    }

    void sequentialAndDrops()
    {
        Ge::LatestSnapshot<Payload> mailbox;
        initialize(mailbox, 1024);
        require(!mailbox.tryConsume(), "Empty mailbox must retain initial reader");
        check(mailbox.reader());
        fill(mailbox.writer(), 1);
        mailbox.publish();
        require(mailbox.tryConsume(), "First publication was lost");
        require(mailbox.reader().sequence == 1, "Wrong first snapshot");
        const Payload* held = &mailbox.reader();

        // No consumer operations while newer generations replace one another.
        for (std::uint64_t sequence = 2; sequence <= 100001; ++sequence)
        {
            fill(mailbox.writer(), sequence);
            mailbox.publish();
        }
        check(*held);
        require(held->sequence == 1, "Unread publications modified active reader");
        require(mailbox.tryConsume(), "Latest publication was lost");
        check(mailbox.reader());
        require(mailbox.reader().sequence == 100001,
                "Mailbox queued stale generations instead of retaining latest");
        require(!mailbox.tryConsume(), "A publication must be consumed only once");
    }

    void heldReaderDuringPublications()
    {
        Ge::LatestSnapshot<Payload> mailbox;
        initialize(mailbox, 512);
        fill(mailbox.writer(), 7);
        mailbox.publish();
        require(mailbox.tryConsume(), "Held-reader setup failed");
        const Payload& held = mailbox.reader();
        std::atomic<bool> finished{false};
        std::thread producer([&]
        {
            for (std::uint64_t sequence = 8; sequence <= 100007; ++sequence)
            {
                fill(mailbox.writer(), sequence);
                mailbox.publish();
            }
            finished.store(true, std::memory_order_release);
        });
        std::exception_ptr failure;
        try
        {
            do
            {
                check(held);
                require(held.sequence == 7, "Producer acquired the active reader");
                std::this_thread::yield();
            } while (!finished.load(std::memory_order_acquire));
        }
        catch (...) { failure = std::current_exception(); }
        producer.join();
        if (failure) std::rethrow_exception(failure);
        require(mailbox.tryConsume(), "Held-reader latest publication was lost");
        check(mailbox.reader());
        require(mailbox.reader().sequence == 100007, "Held-reader final generation is stale");
    }

    void concurrentStress(bool slowerProducer)
    {
        constexpr std::uint64_t publicationCount = 150000;
        Ge::LatestSnapshot<Payload> mailbox;
        initialize(mailbox, 512);
        std::atomic<bool> start{false};
        std::atomic<bool> finished{false};
        std::thread producer([&]
        {
            while (!start.load(std::memory_order_acquire))
                std::this_thread::yield();
            for (std::uint64_t sequence = 1; sequence <= publicationCount; ++sequence)
            {
                fill(mailbox.writer(), sequence);
                mailbox.publish();
                if (slowerProducer) std::this_thread::yield();
            }
            finished.store(true, std::memory_order_release);
        });

        std::uint64_t previousSequence = 0;
        std::uint64_t consumed = 0;
        std::exception_ptr failure;
        start.store(true, std::memory_order_release);
        try
        {
            for (;;)
            {
                if (mailbox.tryConsume())
                {
                    const auto& snapshot = mailbox.reader();
                    check(snapshot);
                    require(snapshot.sequence > previousSequence,
                            "Snapshots must be strictly increasing, without duplicates");
                    previousSequence = snapshot.sequence;
                    ++consumed;
                    // Read the same slot more than once while producer publishes.
                    if (!slowerProducer)
                    {
                        check(snapshot);
                        check(snapshot);
                    }
                }
                else
                    std::this_thread::yield();

                if (finished.load(std::memory_order_acquire) &&
                    previousSequence == publicationCount)
                    break;
            }
        }
        catch (...) { failure = std::current_exception(); }
        producer.join();
        if (failure) std::rethrow_exception(failure);
        require(consumed != 0, "Stress test never consumed a snapshot");
        require(!mailbox.tryConsume(), "Stress test duplicated the last publication");
        check(mailbox.reader());
        std::cout << "  " << (slowerProducer ? "slower" : "faster")
                  << " producer: " << publicationCount << " published, "
                  << consumed << " consumed, " << publicationCount - consumed
                  << " superseded\n";
    }
}

int main()
{
    static_assert(!std::is_copy_constructible<Ge::LatestSnapshot<Payload>>::value,
                  "A live mailbox cannot be copied");
    static_assert(!std::is_move_constructible<Ge::LatestSnapshot<Payload>>::value,
                  "A live mailbox cannot be moved");
    try
    {
        sequentialAndDrops();
        heldReaderDuringPublications();
        concurrentStress(false);
        concurrentStress(true);
        std::cout << "LatestSnapshot: 4 test groups passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "LatestSnapshot: " << error.what() << '\n';
        return 1;
    }
}
