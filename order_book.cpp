#include "order_book.h"

#include <algorithm>

OrderBook::OrderBook(std::size_t capacity)
    : pool_(capacity)
{
}

// Would these two prices agree on a trade?
bool OrderBook::crosses(Side side, int64_t incoming, int64_t resting)
{
    if (side == Side::BUY)
    {
        // I am buying. I am happy if the seller wants my price or less.
        return incoming >= resting;
    }

    // I am selling. I am happy if the buyer offers my price or more.
    return incoming <= resting;
}

// A resting order got completely eaten. Clean up everything that points at it.
void OrderBook::remove_filled(Book &book,
                              Book::iterator level_it,
                              PriceLevel::iterator pos)
{
    Order *filled = *pos;

    // Forget the id first, while we can still read it off the object.
    index_.erase(filled->order_id());

    // Hand the slot back so a future order can use it.
    pool_.deallocate(filled);

    // Unlink the node from the queue.
    level_it->second.erase(pos);
}

void OrderBook::match(uint64_t incoming_id,
                      Side side,
                      OrderType type,
                      int64_t price,
                      uint64_t &remaining_size,
                      std::vector<Trade> &trades)
{
    // A buyer eats the sell side. A seller eats the buy side.
    Book &other = (side == Side::BUY) ? asks_ : bids_;

    // Keep going until we run out of order to fill or run out of book to eat.
    while (remaining_size > 0 && !other.empty())
    {
        // Grab the best price on the other side.
        // Asks sort low to high, so the cheapest seller is at the front.
        // Bids sort low to high too, so the priciest buyer is at the back.
        auto level_it = (side == Side::BUY) ? other.begin() : std::prev(other.end());
        const int64_t resting_price = level_it->first;

        // A limit order has a price it will not go past. Check it.
        // A market order has no such limit, so we skip this check entirely.
        if (type == OrderType::LIMIT && !crosses(side, price, resting_price))
        {
            break;
        }

        PriceLevel &level = level_it->second;

        // Work through this one price, front to back.
        // Front means the person who got here first, so they get filled first.
        while (remaining_size > 0 && !level.empty())
        {
            auto pos = level.begin();
            Order *resting = *pos;

            // You can only trade as much as the smaller side is offering.
            const uint64_t traded = std::min(remaining_size, resting->size());

            // Write down what happened.
            // Whichever side is buying goes in the buy slot.
            trades.push_back(Trade{
                (side == Side::BUY) ? incoming_id : resting->order_id(),
                (side == Side::BUY) ? resting->order_id() : incoming_id,
                resting_price,
                traded});

            // Shrink both sides by the amount that just traded.
            remaining_size -= traded;
            resting->set_size(resting->size() - traded);

            // Nothing left of the resting order, so it leaves the book.
            // If something is left, then our incoming order must be finished,
            // and the outer loop will stop on its own.
            if (resting->size() == 0)
            {
                remove_filled(other, level_it, pos);
            }
        }

        // Everyone at this price is gone, so the price itself goes too.
        // An empty price level would show up in toString() and slow down
        // the search for the best price.
        if (level.empty())
        {
            other.erase(level_it);
        }
    }
}

// Nobody wanted to trade with this order, or only part of it traded.
// Park the rest in the book so it can wait.
void OrderBook::rest(uint64_t order_id,
                     uint64_t client_id,
                     const std::string &ticker,
                     Side side,
                     OrderType type,
                     int64_t price,
                     uint64_t size,
                     uint64_t timestamp)
{
    // Build the Order inside the pool. No call to new anywhere.
    Order *order = pool_.allocate(order_id, client_id, ticker,
                                  side, type, price, size, timestamp);

    // Pool is full. Drop the order rather than crash.
    if (order == nullptr)
    {
        return;
    }

    Book &book = (side == Side::BUY) ? bids_ : asks_;

    // book[price] makes the queue if this price is new, or finds it if not.
    PriceLevel &level = book[price];

    // Join the back of the line. Everyone already there was here first.
    level.push_back(order);

    // Write down where it went. std::prev(end()) is the node we just added.
    index_[order_id] = Location{side, price, std::prev(level.end())};
}

std::vector<Trade> OrderBook::add(uint64_t order_id,
                                  uint64_t client_id,
                                  const std::string &ticker,
                                  Side side,
                                  OrderType type,
                                  int64_t price,
                                  uint64_t size,
                                  uint64_t timestamp)
{
    std::vector<Trade> trades;

    // Reject an id we already know about, and reject an empty order.
    // Nothing traded, so hand back an empty list.
    if (index_.contains(order_id) || size == 0)
    {
        return trades;
    }

    // Try to trade first. Only what survives this goes into the book.
    uint64_t remaining = size;
    match(order_id, side, type, price, remaining, trades);

    // A market order says fill me now at any price. It never waits.
    // So anything it could not fill is simply thrown away.
    if (remaining > 0 && type == OrderType::LIMIT)
    {
        rest(order_id, client_id, ticker, side, type, price, remaining, timestamp);
    }

    return trades;
}

bool OrderBook::cancel(uint64_t order_id)
{
    // One hash lookup tells us exactly where the order is.
    auto it = index_.find(order_id);
    if (it == index_.end())
    {
        return false;
    }

    const Location &loc = it->second;
    Book &book = (loc.side == Side::BUY) ? bids_ : asks_;

    auto level_it = book.find(loc.price);
    PriceLevel &level = level_it->second;

    // Give the memory back first, then unlink the node.
    // Doing it the other way round would lose the pointer.
    pool_.deallocate(*loc.pos);
    level.erase(loc.pos);

    // An empty price level should not stay in the book.
    if (level.empty())
    {
        book.erase(level_it);
    }

    index_.erase(it);
    return true;
}

bool OrderBook::update(uint64_t order_id, uint64_t new_size)
{
    auto it = index_.find(order_id);
    if (it == index_.end())
    {
        return false;
    }

    // Asking for zero means you want out. That is just a cancel.
    if (new_size == 0)
    {
        return cancel(order_id);
    }

    Location &loc = it->second;
    Order *order = *loc.pos;

    // Shrinking is free. You are asking for less, so you keep your place.
    if (new_size <= order->size())
    {
        order->set_size(new_size);
        return true;
    }

    // Growing costs you your place in the queue.
    // Otherwise you could jump ahead of people with size you never waited for.
    Book &book = (loc.side == Side::BUY) ? bids_ : asks_;
    PriceLevel &level = book.find(loc.price)->second;

    order->set_size(new_size);
    level.erase(loc.pos);
    level.push_back(order);

    // The old iterator points at a node that no longer exists.
    // Forget this line and a later cancel() reads freed memory.
    loc.pos = std::prev(level.end());

    return true;
}

std::string OrderBook::toString() const
{
    // Asks printed cheapest first, which is how a real book is shown.
    std::string out = "=== ASKS (low to high) ===\n";
    for (const auto &[price, level] : asks_)
    {
        out += level_to_string(price, level);
    }

    // Bids printed priciest first, so we walk the map backwards.
    out += "=== BIDS (high to low) ===\n";
    for (auto it = bids_.rbegin(); it != bids_.rend(); ++it)
    {
        out += level_to_string(it->first, it->second);
    }

    return out;
}

std::optional<int64_t> OrderBook::best_bid() const
{
    // No buyers at all, so there is no best price. Say nothing rather than 0.
    if (bids_.empty())
    {
        return std::nullopt;
    }

    // Sorted low to high, so the highest buyer is the last one.
    return bids_.rbegin()->first;
}

std::optional<int64_t> OrderBook::best_ask() const
{
    if (asks_.empty())
    {
        return std::nullopt;
    }

    // Sorted low to high, so the cheapest seller is the first one.
    return asks_.begin()->first;
}

std::optional<int64_t> OrderBook::spread() const
{
    const auto bid = best_bid();
    const auto ask = best_ask();

    // A gap needs two edges. One missing side means no answer.
    if (!bid.has_value() || !ask.has_value())
    {
        return std::nullopt;
    }

    return ask.value() - bid.value();
}

// Print one price and the sizes waiting at it, in queue order.
std::string OrderBook::level_to_string(int64_t price, const PriceLevel &level)
{
    std::string out = "  " + std::to_string(price) + " :";
    for (const Order *o : level)
    {
        out += " " + std::to_string(o->size());
    }
    out += "\n";
    return out;
}