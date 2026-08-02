#include "order_book.h"

OrderBook::OrderBook(std::size_t capacity)
    : pool_(capacity)
{
}

bool OrderBook::add(uint64_t order_id,
                    uint64_t client_id,
                    const std::string &ticker,
                    Side side,
                    OrderType type,
                    int64_t price,
                    uint64_t size,
                    uint64_t timestamp)
{
    if (index_.contains(order_id))
    {
        return false;
    }

    Order *order = pool_.allocate(order_id, client_id, ticker,
                                  side, type, price, size, timestamp);
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

bool OrderBook::cancel(uint64_t order_id)
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

bool OrderBook::update(uint64_t order_id, uint64_t new_size)
{
    auto it = index_.find(order_id);
    if (it == index_.end())
    {
        return false;
    }

    // nothing left to trade, so this is really a cancel
    if (new_size == 0)
    {
        return cancel(order_id);
    }

    Location &loc = it->second;
    Order *order = *loc.pos;

    // shrinking or staying the same keeps the order where it is
    if (new_size <= order->size())
    {
        order->set_size(new_size);
        return true;
    }

    // growing means giving up the queue spot and starting again at the back
    auto &book = (loc.side == Side::BUY) ? bids_ : asks_;
    PriceLevel &level = book.find(loc.price)->second;

    order->set_size(new_size);
    level.erase(loc.pos);
    level.push_back(order);

    // the old iterator is dead now. this line is what keeps cancel() safe.
    loc.pos = std::prev(level.end());

    return true;
}

std::string OrderBook::toString() const
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

std::optional<int64_t> OrderBook::best_bid() const
{
    if (bids_.empty())
    {
        return std::nullopt;
    }
    // bids sort low to high, so the highest buy is at the back
    return bids_.rbegin()->first;
}

std::optional<int64_t> OrderBook::best_ask() const
{
    if (asks_.empty())
    {
        return std::nullopt;
    }
    // asks sort low to high, so the lowest sell is at the front
    return asks_.begin()->first;
}

std::optional<int64_t> OrderBook::spread() const
{
    const auto bid = best_bid();
    const auto ask = best_ask();

    if (!bid.has_value() || !ask.has_value())
    {
        return std::nullopt;
    }

    return ask.value() - bid.value();
}

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