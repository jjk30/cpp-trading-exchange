#pragma once

#include "mem_pool.h"
#include "order.h"

#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <unordered_map>

class OrderBook
{
public:
    explicit OrderBook(size_t capacity)
        : pool_(capacity)
    {
    }

    // put an order in the book. returns false if the id already exists.
    bool add(uint64_t order_id,
             uint64_t client_id,
             const std::string &ticker,
             Side side,
             OrderType type,
             int64_t price,
             uint64_t size)
    {
        if (index_.contains(order_id))
        {
            return false;
        }

        Order *order = pool_.allocate(order_id, client_id, ticker,
                                      side, type, price, size);
        if (order == nullptr)
        {
            return false;
        }

        // pick which side of the book this order belongs to
        auto &book = (side == Side::BUY) ? bids_ : asks_;

        // find or create the queue at this price, then join the back of it
        PriceLevel &level = book[price];
        level.push_back(order);

        // remember where it went, so cancel() can find it in one jump
        index_[order_id] = Location{side, price, std::prev(level.end())};

        return true;
    }

    // remove an order by id. returns false if the id isn't in the book.
    bool cancel(uint64_t order_id)
    {
        auto it = index_.find(order_id);
        if (it == index_.end())
        {
            return false;
        }

        const Location &loc = it->second;
        auto &book = (loc.side == Side::BUY) ? bids_ : asks_;

        auto level_it = book.find(loc.price);
        PriceLevel &level = level_it->second;

        pool_.deallocate(*loc.pos);
        level.erase(loc.pos);

        // an empty price level shouldn't stay in the book
        if (level.empty())
        {
            book.erase(level_it);
        }

        index_.erase(it);
        return true;
    }

    std::string toString() const
    {
        std::string out = "=== ASKS (low to high) ===\n";
        for (const auto &[price, level] : asks_)
        {
            out += level_to_string(price, level);
        }

        out += "=== BIDS (high to low) ===\n";
        for (auto it = bids_.rbegin(); it != bids_.rend(); ++it)
        {
            out += level_to_string(it->first, it->second);
        }

        return out;
    }

    size_t order_count() const noexcept { return index_.size(); }
    size_t bid_levels() const noexcept { return bids_.size(); }
    size_t ask_levels() const noexcept { return asks_.size(); }

private:
    using PriceLevel = std::list<Order *>;

    // where a single order lives, so cancel() doesn't have to search
    struct Location
    {
        Side side;
        int64_t price;
        PriceLevel::iterator pos;
    };

    static std::string level_to_string(int64_t price, const PriceLevel &level)
    {
        std::string out = "  " + std::to_string(price) + " :";
        for (const Order *o : level)
        {
            out += " " + std::to_string(o->size());
        }
        out += "\n";
        return out;
    }

    MemPool<Order> pool_;

    // both sorted low -> high, so they are the same type
    // best ask = asks_.begin()   (lowest sell)
    // best bid = bids_.rbegin()  (highest buy, read backwards)
    std::map<int64_t, PriceLevel> bids_;
    std::map<int64_t, PriceLevel> asks_;

    std::unordered_map<uint64_t, Location> index_;
};