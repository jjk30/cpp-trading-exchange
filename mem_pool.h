#pragma once
// stops this file getting pasted in twice

#include <cstddef> // size_t, byte
#include <memory>  // construct_at, destroy_at
#include <utility> // forward
#include <vector>

// templates must live in a header
template <typename T>
class MemPool final
{
public:
    // explicit: stops a plain number turning into a MemPool by accident
    // size_t: a count of boxes can never be negative
    explicit MemPool(std::size_t num_elems)
    {
        slots_.resize(num_elems); // makes the boxes now, so none are made later

        free_indices_.reserve(num_elems); // sets room aside, list is still empty

        // put every box number on the free list
        for (std::size_t i = 0; i < num_elems; ++i)
        {
            free_indices_.push_back(i);
        }

        // size 0 is fine, allocate just returns nullptr every time
    }

    ~MemPool()
    {
        // if a box is still in use, kill the object inside it
        for (Slot &slot : slots_)
        {
            if (!slot.is_free)
            {
                std::destroy_at(object_ptr(slot));
            }
        }
    }

    // takes any number of arguments, of any type
    // noexcept: promise to never throw
    template <typename... Args>
    T *allocate(Args &&...args) noexcept
    {
        // pool is full, so hand back nothing
        if (free_indices_.empty())
        {
            return nullptr;
        }

        // take the box freed most recently, it is likely still in cache
        const std::size_t idx = free_indices_.back();
        free_indices_.pop_back();

        Slot &slot = slots_[idx];
        slot.is_free = false; // taken

        // build a T in bytes we already own, no heap call
        // forward passes the arguments through unchanged
        return std::construct_at(object_ptr(slot), std::forward<Args>(args)...);
    }

    void deallocate(const T *elem) noexcept
    {
        // given nothing, so do nothing
        if (elem == nullptr)
        {
            return;
        }

        // storage is first in Slot, so object address == Slot address
        // boxes sit in a row, so subtracting box 0 gives the box number
        const Slot *slot_ptr = reinterpret_cast<const Slot *>(elem);
        const std::ptrdiff_t offset = slot_ptr - slots_.data();

        // pointer is not from this pool, so leave it alone
        if (offset < 0 || static_cast<std::size_t>(offset) >= slots_.size())
        {
            return;
        }

        const std::size_t idx = static_cast<std::size_t>(offset);
        Slot &slot = slots_[idx];

        // already free, freeing twice would hand one box to two callers
        if (slot.is_free)
        {
            return;
        }

        std::destroy_at(object_ptr(slot)); // kill the object
        slot.is_free = true;               // box is open again
        free_indices_.push_back(idx);      // allocate can hand it out again
    }

    // read only, useful for tests
    std::size_t capacity() const noexcept { return slots_.size(); }
    std::size_t available() const noexcept { return free_indices_.size(); }
    std::size_t in_use() const noexcept { return slots_.size() - free_indices_.size(); }

    // no copying, two pools would share the same boxes
    MemPool() = delete ("a MemPool owns its backing storage and free list");
    MemPool(const MemPool &) = delete ("a MemPool owns its backing storage and free list");
    MemPool(const MemPool &&) = delete ("a MemPool owns its backing storage and free list");
    MemPool &operator=(const MemPool &) = delete ("a MemPool owns its backing storage and free list");
    MemPool &operator=(const MemPool &&) = delete ("a MemPool owns its backing storage and free list");

private:
    struct Slot
    {
        // raw space, right size and right spot for one T
        // must stay first, deallocate depends on it
        alignas(T) std::byte storage[sizeof(T)];

        bool is_free = true; // every box starts open
    }; // struct needs this semicolon

    // relabels raw bytes as a T, written once instead of three times
    static T *object_ptr(Slot &slot) noexcept
    {
        return reinterpret_cast<T *>(slot.storage);
    }

    std::vector<Slot> slots_; // all boxes in one row, good for cache

    std::vector<std::size_t> free_indices_; // numbers of the open boxes
};