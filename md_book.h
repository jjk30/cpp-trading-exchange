#pragma once

#include "md_message.h"

#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>

// A client's own copy of the book, rebuilt from nothing but the feed.
//
// This class never matches, never allocates from a pool, never decides
// anything. It only obeys messages. That is the point: if this ends up
// looking like the exchange's book, then the feed carries enough
// information for anyone to follow along.
class MDBook
{
public:
    // One resting order as seen from outside.
    // No client id, because the feed does not say who owns what. Nobody
    // outside the exchange gets to know that.
    struct Entry
    {
        uint64_t order_id;
        uint64_t size;
    };

    // Apply one message.
    // Returns false if the message was out of order, which means this book
    // can no longer be trusted.
    bool apply(const MDMessage &m)
    {
        // Every message is numbered. If the number is not the one we expect,
        // something went missing on the way here.
        //
        // We keep applying anyway so the caller can see how bad the damage is,
        // but the book is now a guess, not a copy. A real feed handler would
        // stop trading and ask for a fresh snapshot.
        const bool in_order = (m.seq == expected_seq_);
        if (!in_order)
        {
            ++gaps_;
        }

        // Next time we want the one after whatever just arrived.
        expected_seq_ = m.seq + 1;

        switch (m.type)
        {
        case MDType::Added:
            on_added(m);
            break;

        case MDType::Executed:
            on_executed(m);
            break;

        case MDType::Cancelled:
            on_cancelled(m);
            break;
        }

        return in_order;
    }

    // How many messages arrived out of order. Zero means a perfect copy.
    uint64_t gaps() const noexcept { return gaps_; }

    std::size_t order_count() const noexcept { return index_.size(); }
    std::size_t bid_levels() const noexcept { return bids_.size(); }
    std::size_t ask_levels() const noexcept { return asks_.size(); }

    std::optional<int64_t> best_bid() const
    {
        if (bids_.empty())
        {
            return std::nullopt;
        }

        // Sorted low to high, so the highest buyer is at the back.
        return bids_.rbegin()->first;
    }

    std::optional<int64_t> best_ask() const
    {
        if (asks_.empty())
        {
            return std::nullopt;
        }

        return asks_.begin()->first;
    }

    // Printed exactly the way OrderBook prints itself.
    // That is deliberate. It means a test can compare the two books with
    // one string comparison instead of walking both by hand.
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

private:
    using PriceLevel = std::list<Entry>;
    using Book = std::map<int64_t, PriceLevel>;

    // Where one order is sitting, so we can find it without searching.
    // Same trick the exchange uses, for the same reason.
    struct Location
    {
        Side side;
        int64_t price;
        PriceLevel::iterator pos;
    };

    void on_added(const MDMessage &m)
    {
        // The exchange should never announce the same id twice without
        // removing it first. If it does, trust the newer one.
        if (index_.contains(m.order_id))
        {
            remove(m.order_id);
        }

        Book &book = (m.side == Side::BUY) ? bids_ : asks_;
        PriceLevel &level = book[m.price];

        // Back of the line, because that is where the exchange put it.
        level.push_back(Entry{m.order_id, m.size});

        index_[m.order_id] = Location{m.side, m.price, std::prev(level.end())};
    }

    void on_executed(const MDMessage &m)
    {
        auto it = index_.find(m.order_id);

        // We were told about an order we never saw appear. That is a gap.
        if (it == index_.end())
        {
            return;
        }

        Entry &entry = *it->second.pos;

        // Never let size wrap around. A message claiming more size than the
        // order has means we already lost something.
        if (m.size >= entry.size)
        {
            remove(m.order_id);
            return;
        }

        entry.size -= m.size;
    }

    void on_cancelled(const MDMessage &m)
    {
        remove(m.order_id);
    }

    // Pull one order out and tidy up after it.
    void remove(uint64_t order_id)
    {
        auto it = index_.find(order_id);
        if (it == index_.end())
        {
            return;
        }

        const Location &loc = it->second;
        Book &book = (loc.side == Side::BUY) ? bids_ : asks_;

        auto level_it = book.find(loc.price);
        if (level_it != book.end())
        {
            level_it->second.erase(loc.pos);

            // An empty price is not a price. Drop it or the two books
            // will not print the same.
            if (level_it->second.empty())
            {
                book.erase(level_it);
            }
        }

        index_.erase(it);
    }

    static std::string level_to_string(int64_t price, const PriceLevel &level)
    {
        std::string out = "  " + std::to_string(price) + " :";
        for (const Entry &e : level)
        {
            out += " " + std::to_string(e.size);
        }
        out += "\n";
        return out;
    }

    Book bids_;
    Book asks_;

    std::unordered_map<uint64_t, Location> index_;

    // The exchange numbers from 1, so that is what we want first.
    uint64_t expected_seq_{1};

    uint64_t gaps_{0};
};
