#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <utility>

// A bounded, lock-free queue.
//   - MANY threads may call try_push()  (the clients)
//   - ONE  thread  may call try_pop()   (the engine)
//
// No mutex. No allocation after construction. The whole ring lives inside
// the object, the same way MemPool pre-allocates its slots up front.
//
// N must be a power of two so that (pos & MASK) replaces a modulo.
template <class T, std::size_t N>
class LFQueue
{
    static_assert(N >= 2, "capacity must be at least 2");
    static_assert((N & (N - 1)) == 0, "capacity must be a power of two");

    // 64 bytes is the cache line on x86-64 and Apple silicon.
    // Keeping head_ and tail_ on separate lines stops producers and the
    // consumer from fighting over the same line (false sharing).
    static constexpr std::size_t kCacheLine = 64;
    static constexpr std::size_t MASK = N - 1;

    // One parking spot in the ring.
    //
    // seq is the traffic light. Its value tells you whose turn it is:
    //   seq == pos      -> empty, a producer may claim it
    //   seq == pos + 1  -> full,  the consumer may take it
    // Anything else means the slot belongs to a future lap around the ring.
    struct Slot
    {
        std::atomic<std::size_t> seq;
        alignas(T) std::byte storage[sizeof(T)];

        T *ptr() noexcept { return reinterpret_cast<T *>(storage); }
        void destroy() noexcept { std::destroy_at(ptr()); }
    };

public:
    LFQueue()
    {
        // Slot i starts life expecting ticket i.
        for (std::size_t i = 0; i < N; ++i)
            slots_[i].seq.store(i, std::memory_order_relaxed);

        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    ~LFQueue()
    {
        // Destroy whatever is still sitting in the queue.
        // Same "is this slot full?" test that try_pop uses.
        std::size_t pos = head_.load(std::memory_order_relaxed);
        for (;;)
        {
            Slot &slot = slots_[pos & MASK];
            if (slot.seq.load(std::memory_order_acquire) != pos + 1)
                break;
            slot.destroy();
            ++pos;
        }
    }

    LFQueue(const LFQueue &) = delete;            // not copyable
    LFQueue &operator=(const LFQueue &) = delete; // not copyable

    // --- Producer side: safe to call from many threads ---
    bool try_push(const T &item) { return emplace(item); }
    bool try_push(T &&item) { return emplace(std::move(item)); }

    // --- Consumer side: call from ONE thread only ---
    bool try_pop(T &out)
    {
        // Only this thread moves head_, so a plain load is enough.
        const std::size_t pos = head_.load(std::memory_order_relaxed);
        Slot &slot = slots_[pos & MASK];

        // Not "pos + 1" means either nothing is here, or a producer claimed
        // the slot but has not finished writing yet. Either way: nothing to take.
        if (slot.seq.load(std::memory_order_acquire) != pos + 1)
            return false;

        out = std::move(*slot.ptr());
        slot.destroy();

        head_.store(pos + 1, std::memory_order_relaxed);

        // Hand the slot back. It is now waiting for ticket pos + N,
        // which is the next lap around the ring.
        slot.seq.store(pos + N, std::memory_order_release);
        return true;
    }

    // --- Observers (approximate under concurrency) ---
    bool empty() const noexcept { return size() == 0; }

    std::size_t size() const noexcept
    {
        // Read head first. If tail moves in between we over-report, which is
        // safer than underflowing an unsigned subtraction.
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        return tail >= head ? tail - head : 0;
    }

    static constexpr std::size_t capacity() noexcept { return N; }

private:
    // One code path for both the copy and the move try_push.
    template <class U>
    bool emplace(U &&item)
    {
        std::size_t pos = tail_.load(std::memory_order_relaxed);
        Slot *slot = nullptr;

        for (;;)
        {
            slot = &slots_[pos & MASK];
            const std::size_t seq = slot->seq.load(std::memory_order_acquire);
            const auto diff = static_cast<std::ptrdiff_t>(seq) -
                              static_cast<std::ptrdiff_t>(pos);

            if (diff == 0)
            {
                // The slot is free and it is our turn. Take the ticket.
                if (tail_.compare_exchange_weak(pos, pos + 1,
                                                std::memory_order_relaxed,
                                                std::memory_order_relaxed))
                    break;
                // CAS failed: another producer grabbed it first.
                // pos was updated for us, so just go round again.
            }
            else if (diff < 0)
            {
                return false; // the consumer has not caught up: queue is full
            }
            else
            {
                // Stale read of tail_. Refresh and retry.
                pos = tail_.load(std::memory_order_relaxed);
            }
        }

        // We own this slot exclusively now. Nobody can see it until the
        // release store below, so the write itself needs no synchronisation.
        std::construct_at(slot->ptr(), std::forward<U>(item));

        // Green light for the consumer.
        slot->seq.store(pos + 1, std::memory_order_release);
        return true;
    }

    alignas(kCacheLine) std::atomic<std::size_t> head_{0}; // consumer only
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0}; // producers race here
    alignas(kCacheLine) Slot slots_[N];
};
