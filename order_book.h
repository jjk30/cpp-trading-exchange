#pragma once

#include "mem_pool.h"
#include "order.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <unordered_map>

class OrderBook
{
public:
    explicit OrderBook(std::size_t capacity);

    // put an order in the book. returns false if the id already exists.
    bool add(uint64_t order_id,
             uint64_t client_id,
             const std::string &ticker,
             Side side,
             OrderType type,
             int64_t price,
             uint64_t size,
             uint64_t timestamp = 0);

    // remove an order by id. returns false if the id isn't in the book.
    bool cancel(uint64_t order_id);

    std::string toString() const;

    std::size_t order_count() const noexcept { return index_.size(); }
    std::size_t bid_levels() const noexcept { return bids_.size(); }
    std::size_t ask_levels() const noexcept { return asks_.size(); }

private:
    using PriceLevel = std::list<Order *>;

    // where a single order lives, so cancel() doesn't have to search
    struct Location
    {
        Side side;
        int64_t price;
        PriceLevel::iterator pos;
    };

    static std::string level_to_string(int64_t price, const PriceLevel &level);

    MemPool<Order> pool_;

    // both sorted low -> high, so they are the same type
    // best ask = asks_.begin()   (lowest sell)
    // best bid = bids_.rbegin()  (highest buy, read backwards)
    std::map<int64_t, PriceLevel> bids_;
    std::map<int64_t, PriceLevel> asks_;

    std::unordered_map<uint64_t, Location> index_;
};