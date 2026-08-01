#include "mem_pool.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>

namespace
{

    // counts how many times it was built and killed
    struct Tracker
    {
        static int constructed;
        static int destroyed;
        static int copied;
        static int moved;

        int value = 0;

        explicit Tracker(int v) noexcept : value(v) { ++constructed; }
        Tracker(const Tracker &other) noexcept : value(other.value) { ++copied; }
        Tracker(Tracker &&other) noexcept : value(other.value) { ++moved; }
        ~Tracker() { ++destroyed; }

        static void reset()
        {
            constructed = 0;
            destroyed = 0;
            copied = 0;
            moved = 0;
        }
    };

    int Tracker::constructed = 0;
    int Tracker::destroyed = 0;
    int Tracker::copied = 0;
    int Tracker::moved = 0;

    // needs two arguments, checks they get passed through
    struct Pair
    {
        int a;
        std::string b;
        Pair(int a_in, std::string b_in) noexcept : a(a_in), b(std::move(b_in)) {}
    };

    // fresh counters before every Tracker test
    class MemPoolTrackerTest : public ::testing::Test
    {
    protected:
        void SetUp() override { Tracker::reset(); }
    };

    // ---------- construction ----------

    TEST(MemPoolBasics, StartsWithEverythingFree)
    {
        MemPool<int> pool(4);
        EXPECT_EQ(pool.capacity(), 4u);
        EXPECT_EQ(pool.available(), 4u);
        EXPECT_EQ(pool.in_use(), 0u);
    }

    TEST(MemPoolBasics, ZeroSizedPoolAlwaysFails)
    {
        MemPool<int> pool(0);
        EXPECT_EQ(pool.capacity(), 0u);
        EXPECT_EQ(pool.allocate(1), nullptr);
    }

    // ---------- allocate ----------

    TEST(MemPoolAllocate, GivesBackAUsableObject)
    {
        MemPool<int> pool(2);
        int *p = pool.allocate(42);

        ASSERT_NE(p, nullptr);
        EXPECT_EQ(*p, 42);
        EXPECT_EQ(pool.in_use(), 1u);
        EXPECT_EQ(pool.available(), 1u);
    }

    TEST(MemPoolAllocate, EveryObjectGetsItsOwnBox)
    {
        MemPool<int> pool(3);
        int *a = pool.allocate(1);
        int *b = pool.allocate(2);
        int *c = pool.allocate(3);

        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);
        ASSERT_NE(c, nullptr);

        EXPECT_NE(a, b);
        EXPECT_NE(b, c);
        EXPECT_NE(a, c);

        EXPECT_EQ(*a, 1);
        EXPECT_EQ(*b, 2);
        EXPECT_EQ(*c, 3);
    }

    TEST(MemPoolAllocate, ReturnsNullWhenFull)
    {
        MemPool<int> pool(2);
        ASSERT_NE(pool.allocate(1), nullptr);
        ASSERT_NE(pool.allocate(2), nullptr);

        // pool is full now
        EXPECT_EQ(pool.allocate(3), nullptr);
        EXPECT_EQ(pool.in_use(), 2u);
    }

    TEST(MemPoolAllocate, PassesThroughSeveralArguments)
    {
        MemPool<Pair> pool(1);
        Pair *p = pool.allocate(7, std::string("hello"));

        ASSERT_NE(p, nullptr);
        EXPECT_EQ(p->a, 7);
        EXPECT_EQ(p->b, "hello");

        pool.deallocate(p);
    }

    TEST_F(MemPoolTrackerTest, TemporariesAreMovedNotCopied)
    {
        MemPool<Tracker> pool(1);
        Tracker temp(5);                             // 1 construction
        Tracker *p = pool.allocate(std::move(temp)); // should move, not copy

        ASSERT_NE(p, nullptr);
        EXPECT_EQ(Tracker::moved, 1);
        EXPECT_EQ(Tracker::copied, 0);

        pool.deallocate(p);
    }

    // ---------- deallocate ----------

    TEST(MemPoolDeallocate, BoxComesBackToTheFreeList)
    {
        MemPool<int> pool(2);
        int *p = pool.allocate(10);
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(pool.in_use(), 1u);

        pool.deallocate(p);
        EXPECT_EQ(pool.in_use(), 0u);
        EXPECT_EQ(pool.available(), 2u);
    }

    TEST(MemPoolDeallocate, FreedBoxIsHandedOutAgain)
    {
        MemPool<int> pool(1);
        int *first = pool.allocate(1);
        ASSERT_NE(first, nullptr);

        pool.deallocate(first);

        int *second = pool.allocate(2);
        ASSERT_NE(second, nullptr);
        EXPECT_EQ(first, second); // same box, reused
        EXPECT_EQ(*second, 2);
    }

    TEST(MemPoolDeallocate, EmptyPoolRecoversAfterFreeing)
    {
        MemPool<int> pool(2);
        int *a = pool.allocate(1);
        int *b = pool.allocate(2);
        ASSERT_EQ(pool.allocate(3), nullptr);

        pool.deallocate(a);
        EXPECT_NE(pool.allocate(3), nullptr);

        pool.deallocate(b);
    }

    TEST_F(MemPoolTrackerTest, DeallocateRunsTheDestructor)
    {
        MemPool<Tracker> pool(1);
        Tracker *p = pool.allocate(1);
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(Tracker::destroyed, 0);

        pool.deallocate(p);
        EXPECT_EQ(Tracker::destroyed, 1);
    }

    // ---------- edge cases ----------

    TEST(MemPoolEdgeCases, NullPointerIsIgnored)
    {
        MemPool<int> pool(2);
        pool.deallocate(nullptr); // must not crash
        EXPECT_EQ(pool.available(), 2u);
    }

    TEST(MemPoolEdgeCases, ForeignPointerIsIgnored)
    {
        MemPool<int> pool(2);
        int outsider = 99;

        pool.deallocate(&outsider); // not one of our boxes
        EXPECT_EQ(pool.available(), 2u);
        EXPECT_EQ(outsider, 99); // untouched
    }

    TEST(MemPoolEdgeCases, DoubleFreeIsIgnored)
    {
        MemPool<int> pool(2);
        int *p = pool.allocate(1);
        ASSERT_NE(p, nullptr);

        pool.deallocate(p);
        pool.deallocate(p); // second time must do nothing

        // if the guard was missing, available would be 3 here
        EXPECT_EQ(pool.available(), 2u);
        EXPECT_EQ(pool.capacity(), 2u);
    }

    TEST_F(MemPoolTrackerTest, DoubleFreeDoesNotDestroyTwice)
    {
        MemPool<Tracker> pool(1);
        Tracker *p = pool.allocate(1);
        ASSERT_NE(p, nullptr);

        pool.deallocate(p);
        pool.deallocate(p);

        EXPECT_EQ(Tracker::destroyed, 1);
    }

    // ---------- destructor ----------

    TEST_F(MemPoolTrackerTest, PoolCleansUpObjectsYouForgot)
    {
        {
            MemPool<Tracker> pool(3);
            pool.allocate(1);
            pool.allocate(2);
            pool.allocate(3);
            EXPECT_EQ(Tracker::constructed, 3);
            EXPECT_EQ(Tracker::destroyed, 0);
        } // pool dies here

        EXPECT_EQ(Tracker::destroyed, 3);
    }

    TEST_F(MemPoolTrackerTest, PoolDoesNotDestroyFreeBoxesTwice)
    {
        {
            MemPool<Tracker> pool(2);
            Tracker *a = pool.allocate(1);
            pool.allocate(2);
            pool.deallocate(a); // one destroyed here
            EXPECT_EQ(Tracker::destroyed, 1);
        } // the other destroyed here

        EXPECT_EQ(Tracker::destroyed, 2);
    }

    // ---------- alignment ----------

    TEST(MemPoolAlignment, ObjectsAreProperlyAligned)
    {
        struct alignas(64) Wide
        {
            char data[64];
        };

        MemPool<Wide> pool(4);
        for (int i = 0; i < 4; ++i)
        {
            Wide *p = pool.allocate();
            ASSERT_NE(p, nullptr);
            EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) % alignof(Wide), 0u);
        }
    }

    // ---------- stress ----------

    TEST(MemPoolStress, ManyRoundsOfUseAndReturn)
    {
        MemPool<int> pool(8);

        for (int round = 0; round < 1000; ++round)
        {
            int *taken[8];
            for (int i = 0; i < 8; ++i)
            {
                taken[i] = pool.allocate(i);
                ASSERT_NE(taken[i], nullptr);
            }

            EXPECT_EQ(pool.in_use(), 8u);
            EXPECT_EQ(pool.allocate(0), nullptr);

            for (int i = 0; i < 8; ++i)
            {
                pool.deallocate(taken[i]);
            }

            EXPECT_EQ(pool.available(), 8u);
        }
    }

} // namespace