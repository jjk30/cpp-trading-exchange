#include "lf_queue.h"

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

    // Counts how many objects are alive, so we can prove the queue destroys
    // everything it is still holding when it goes out of scope.
    struct Tracked
    {
        static inline std::atomic<int> alive{0};

        int value{0};

        Tracked() { ++alive; }
        explicit Tracked(int v) : value(v) { ++alive; }
        Tracked(const Tracked &o) : value(o.value) { ++alive; }
        Tracked(Tracked &&o) noexcept : value(o.value) { ++alive; }
        Tracked &operator=(const Tracked &) = default;
        Tracked &operator=(Tracked &&) noexcept = default;
        ~Tracked() { --alive; }
    };

} // namespace

TEST(LFQueue, StartsEmpty)
{
    LFQueue<int, 8> q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
    EXPECT_EQ(q.capacity(), 8u);
}

TEST(LFQueue, CapacityIsCompileTime)
{
    static_assert(LFQueue<int, 16>::capacity() == 16);
    SUCCEED();
}

TEST(LFQueue, PopOnEmptyFails)
{
    LFQueue<int, 4> q;
    int out = -1;
    EXPECT_FALSE(q.try_pop(out));
    EXPECT_EQ(out, -1); // out must be left alone
}

TEST(LFQueue, PushThenPop)
{
    LFQueue<int, 4> q;
    EXPECT_TRUE(q.try_push(42));
    EXPECT_FALSE(q.empty());
    EXPECT_EQ(q.size(), 1u);

    int out = 0;
    EXPECT_TRUE(q.try_pop(out));
    EXPECT_EQ(out, 42);
    EXPECT_TRUE(q.empty());
}

TEST(LFQueue, IsFifo)
{
    LFQueue<int, 8> q;
    for (int i = 0; i < 5; ++i)
        ASSERT_TRUE(q.try_push(i));

    for (int i = 0; i < 5; ++i)
    {
        int out = -1;
        ASSERT_TRUE(q.try_pop(out));
        EXPECT_EQ(out, i);
    }
}

TEST(LFQueue, FillsToCapacityThenRefuses)
{
    LFQueue<int, 4> q;
    for (int i = 0; i < 4; ++i)
        ASSERT_TRUE(q.try_push(i));

    EXPECT_EQ(q.size(), 4u);
    EXPECT_FALSE(q.try_push(99)); // full, and it says so instead of blocking
}

TEST(LFQueue, WrapsAroundTheRing)
{
    LFQueue<int, 4> q;
    // Three laps through a four-slot ring.
    for (int i = 0; i < 12; ++i)
    {
        ASSERT_TRUE(q.try_push(i));
        int out = -1;
        ASSERT_TRUE(q.try_pop(out));
        EXPECT_EQ(out, i);
    }
    EXPECT_TRUE(q.empty());
}

TEST(LFQueue, HoldsMoveOnlyTypes)
{
    LFQueue<std::unique_ptr<int>, 4> q;
    ASSERT_TRUE(q.try_push(std::make_unique<int>(7)));

    std::unique_ptr<int> out;
    ASSERT_TRUE(q.try_pop(out));
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(*out, 7);
}

TEST(LFQueue, DestructorDestroysLeftoverItems)
{
    Tracked::alive = 0;
    {
        LFQueue<Tracked, 8> q;
        for (int i = 0; i < 5; ++i)
            ASSERT_TRUE(q.try_push(Tracked{i}));

        Tracked out;
        ASSERT_TRUE(q.try_pop(out)); // drain one, leave four behind
        EXPECT_EQ(out.value, 0);
    }
    EXPECT_EQ(Tracked::alive.load(), 0); // nothing leaked
}

// The real test: four producer threads, one consumer thread.
// Every item must arrive exactly once, and each producer's own items
// must arrive in the order that producer sent them.
TEST(LFQueue, ManyProducersOneConsumer)
{
    constexpr int kProducers = 4;
    constexpr int kPerProducer = 5000;
    constexpr std::size_t kTotal = kProducers * kPerProducer;

    // Encode producer id and sequence number into one pair.
    LFQueue<std::pair<int, int>, 64> q;

    std::atomic<bool> done{false};
    std::vector<std::pair<int, int>> received;
    received.reserve(kTotal);

    std::thread consumer([&]
                         {
        std::pair<int, int> item;
        while (received.size() < kTotal)
        {
            if (q.try_pop(item))
                received.push_back(item);
            else if (done.load(std::memory_order_acquire) && q.empty())
                break;
        } });

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p)
    {
        producers.emplace_back([&q, p]
                               {
            for (int i = 0; i < kPerProducer; ++i)
            {
                // Spin until there is room. A real client would back off.
                while (!q.try_push(std::pair<int, int>{p, i}))
                    std::this_thread::yield();
            } });
    }

    for (auto &t : producers)
        t.join();
    done.store(true, std::memory_order_release);
    consumer.join();

    ASSERT_EQ(received.size(), kTotal);

    // No duplicates, no losses, and per-producer order preserved.
    std::unordered_map<int, int> next_expected;
    for (const auto &[producer, seq] : received)
    {
        EXPECT_EQ(seq, next_expected[producer])
            << "producer " << producer << " arrived out of order";
        ++next_expected[producer];
    }

    for (int p = 0; p < kProducers; ++p)
        EXPECT_EQ(next_expected[p], kPerProducer);
}
