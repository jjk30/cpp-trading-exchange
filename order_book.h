#pragma once

#include "mem_pool.h"
#include "order.h"
#include "trade.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class OrderBook
{
public:
    // capacity is how many orders the pool can hold at once.
    // It is fixed at startup so we never ask the heap for memory later.
    explicit OrderBook(std::size_t capacity);

    // Send a new order in.
    // First it tries to trade against the other side of the book.
    // Whatever is left over sits in the book and waits, unless it is a
    // market order, because market orders never wait.
    // Returns every trade that happened. Empty vector means nothing traded.
    std::vector<Trade> add(uint64_t order_id,
                           uint64_t client_id,
                           const std::string &ticker,
                           Side side,
                           OrderType type,
                           int64_t price,
                           uint64_t size,
                           uint64_t timestamp = 0);

    // Pull an order out of the book.
    // Returns false if we have never seen that id.
    bool cancel(uint64_t order_id);

    // Change how big an order is.
    // Making it smaller keeps your place in the queue.
    // Making it bigger sends you to the back of the queue.
    // Setting it to 0 is the same as cancelling.
    bool update(uint64_t order_id, uint64_t new_size);

    // Print the whole book as text. Handy for debugging and for tests.
    std::string toString() const;

    // The highest price anyone is willing to buy at.
    // Returns nothing if there are no buy orders, because 0 is a real price
    // and we must not pretend an empty book has a price of zero.
    std::optional<int64_t> best_bid() const;

    // The lowest price anyone is willing to sell at.
    std::optional<int64_t> best_ask() const;

    // The gap between the two. Needs both sides to exist.
    std::optional<int64_t> spread() const;

    std::size_t order_count() const noexcept { return index_.size(); }
    std::size_t bid_levels() const noexcept { return bids_.size(); }
    std::size_t ask_levels() const noexcept { return asks_.size(); }

private:
    // All the orders sitting at one price, oldest at the front.
    // A list because we often pull an order out of the middle, and a list
    // does that instantly without moving anything else.
    using PriceLevel = std::list<Order *>;

    // One whole side of the book. Price on the left, its queue on the right.
    using Book = std::map<int64_t, PriceLevel>;

    // A note saying exactly where one order is sitting.
    // Without this, cancel() would have to search the entire book.
    // With it, cancel() jumps straight to the order.
    struct Location
    {
        Side side;
        int64_t price;

        // Points at the order's node in the list.
        // A list iterator stays valid when other nodes are added or removed,
        // which is the whole reason a list was chosen.
        PriceLevel::iterator pos;
    };

    // Eat into the other side of the book while the prices still cross.
    // Needs the client id so it can spot a client about to trade with itself.
    // remaining_size is passed by reference because this function shrinks it
    // as it fills, and add() needs to see what is left afterwards.
    void match(uint64_t incoming_id,
               uint64_t incoming_client,
               Side side,
               OrderType type,
               int64_t price,
               uint64_t &remaining_size,
               std::vector<Trade> &trades);

    // Can these two prices trade with each other?
    static bool crosses(Side side, int64_t incoming, int64_t resting);

    // Put whatever did not fill into the book so it can wait for someone.
    void rest(uint64_t order_id,
              uint64_t client_id,
              const std::string &ticker,
              Side side,
              OrderType type,
              int64_t price,
              uint64_t size,
              uint64_t timestamp);

    // Take a fully eaten resting order out of the book and give its
    // memory back to the pool.
    void remove_filled(Book &book, Book::iterator level_it, PriceLevel::iterator pos);

    static std::string level_to_string(int64_t price, const PriceLevel &level);

    // Where all Order objects actually live.
    // One block grabbed at startup, then reused forever.
    MemPool<Order> pool_;

    // Both sides sort low to high, so both are the exact same type.
    // That matters because code like (side == BUY ? bids_ : asks_) will not
    // compile if the two sides have different types.
    // Best ask is asks_.begin(), the cheapest seller.
    // Best bid is bids_.rbegin(), the priciest buyer, read from the back.
    Book bids_;
    Book asks_;

    // Order id to its Location.
    // This is the only reason cancel() is fast instead of a full scan.
    std::unordered_map<uint64_t, Location> index_;
};