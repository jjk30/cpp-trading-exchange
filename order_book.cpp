#include "order_book.h"

#include <algorithm>

OrderBook::OrderBook(std::size_t capacity)
    : pool_(capacity)
{
}

// Put one message on the caller's pile.
// Nobody asked for market data, so do nothing and do not burn a number.
// Burning one would leave a hole in the feed and make clients think
// they missed something.
void OrderBook::emit(std::vector<MDMessage> *md, MDType type, uint64_t order_id,
                     Side side, int64_t price, uint64_t size)
{
    if (md == nullptr)
    {
        return;
    }

    MDMessage m;
    m.seq = ++md_seq_;
    m.type = type;
    m.order_id = order_id;
    m.side = side;
    m.price = price;
    m.size = size;

    md->push_back(m);
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

// An order is leaving the book, so its owner should stop claiming it.
void OrderBook::forget_client_order(uint64_t client_id, uint64_t order_id)
{
    auto it = client_orders_.find(client_id);
    if (it == client_orders_.end())
    {
        return;
    }

    it->second.erase(order_id);

    // No orders left for this client, so drop the whole entry.
    // Otherwise the map keeps growing with empty sets forever.
    if (it->second.empty())
    {
        client_orders_.erase(it);
    }
}

// An order is leaving the book, so its deadline no longer matters.
void OrderBook::forget_expiry(const Order *order)
{
    // No deadline means it was never in the expiry map.
    if (!order->has_deadline())
    {
        return;
    }

    auto it = expiry_.find(order->expires_at());
    if (it == expiry_.end())
    {
        return;
    }

    it->second.erase(order->order_id());

    // Nobody dies at this moment any more, so drop the moment itself.
    if (it->second.empty())
    {
        expiry_.erase(it);
    }
}

// A resting order got completely used up. Clean up everything pointing at it.
void OrderBook::remove_filled(Book &book,
                              Book::iterator level_it,
                              PriceLevel::iterator pos)
{
    Order *filled = *pos;

    // Read what we need off the object while it is still alive.
    const uint64_t order_id = filled->order_id();
    const uint64_t client_id = filled->client_id();

    index_.erase(order_id);
    forget_client_order(client_id, order_id);
    forget_expiry(filled);

    // Hand the slot back so a future order can use it.
    pool_.deallocate(filled);

    // Unlink the node from the queue.
    level_it->second.erase(pos);
}

void OrderBook::match(uint64_t incoming_id,
                      uint64_t incoming_client,
                      Side side,
                      OrderType type,
                      int64_t price,
                      uint64_t &remaining_size,
                      std::vector<Trade> &trades,
                      std::vector<MDMessage> *md)
{
    // A buyer eats the sell side. A seller eats the buy side.
    Book &other = (side == Side::BUY) ? asks_ : bids_;

    // Everything we announce here is about the resting order, which sits on
    // the opposite side from whoever is coming in.
    const Side resting_side = (side == Side::BUY) ? Side::SELL : Side::BUY;

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

            // You can only touch as much as the smaller side is offering.
            const uint64_t amount = std::min(remaining_size, resting->size());

            // Grab the id now. If this order gets fully eaten below, the
            // memory goes back to the pool and reading it after that is a bug.
            const uint64_t resting_id = resting->order_id();

            // Same client on both sides. Letting this trade would be one
            // person buying from themselves, which is not a real trade and
            // in most markets is not allowed.
            // So instead of trading, we kill the overlapping amount off both.
            // Whichever order was bigger keeps its remainder and stays in play.
            const bool self_trade = (resting->client_id() == incoming_client);

            if (!self_trade)
            {
                // Write down what happened.
                // Whichever side is buying goes in the buy slot.
                trades.push_back(Trade{
                    (side == Side::BUY) ? incoming_id : resting_id,
                    (side == Side::BUY) ? resting_id : incoming_id,
                    resting_price,
                    amount});
            }

            // Tell the world the resting order just got smaller.
            // We send this in the self trade case too. No money changed hands,
            // but the book still shrank, and a client copying our book has to
            // shrink with it or it will drift out of step.
            emit(md, MDType::Executed, resting_id, resting_side,
                 resting_price, amount);

            // Both paths shrink both orders by the same amount.
            // The only difference is whether a Trade was recorded.
            remaining_size -= amount;
            resting->set_size(resting->size() - amount);

            // Nothing left of the resting order, so it leaves the book.
            // If something is left, then our incoming order must be finished,
            // and the outer loop will stop on its own.
            // No extra message needed. The client subtracts the same amount,
            // hits zero, and drops the order on its own.
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
                     uint64_t timestamp,
                     uint64_t ttl,
                     std::vector<MDMessage> *md)
{
    // Build the Order inside the pool. No call to new anywhere.
    Order *order = pool_.allocate(order_id, client_id, ticker,
                                  side, type, price, size, timestamp, ttl);

    // Pool is full. Drop the order rather than crash.
    // Say nothing, because nothing went into the book.
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

    // And write down who owns it, so disconnect() can find it later.
    client_orders_[client_id].insert(order_id);

    // Only orders with a real deadline go in the expiry map.
    // Keeping the others out means purge_expired never even looks at them.
    if (order->has_deadline())
    {
        expiry_[order->expires_at()].insert(order_id);
    }

    // It is really in the book now, so it is safe to announce.
    emit(md, MDType::Added, order_id, side, price, size);
}

std::vector<Trade> OrderBook::add(uint64_t order_id,
                                  uint64_t client_id,
                                  const std::string &ticker,
                                  Side side,
                                  OrderType type,
                                  int64_t price,
                                  uint64_t size,
                                  uint64_t timestamp,
                                  uint64_t ttl,
                                  std::vector<MDMessage> *md)
{
    std::vector<Trade> trades;

    // Reject an id we already know about, and reject an empty order.
    // Nothing traded, so hand back an empty list.
    if (index_.contains(order_id) || size == 0)
    {
        return trades;
    }

    // Try to trade first. Only what survives this goes into the book.
    // The order of messages matters. Every execution goes out before the
    // add does, because that is the order things actually happened in.
    uint64_t remaining = size;
    match(order_id, client_id, side, type, price, remaining, trades, md);

    // A market order says fill me now at any price. It never waits.
    // So anything it could not fill is simply thrown away.
    if (remaining > 0 && type == OrderType::LIMIT)
    {
        rest(order_id, client_id, ticker, side, type, price,
             remaining, timestamp, ttl, md);
    }

    return trades;
}

bool OrderBook::cancel(uint64_t order_id, std::vector<MDMessage> *md)
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

    Order *order = *loc.pos;

    // Copy these out before the entry disappears from the index.
    const Side side = loc.side;
    const int64_t price = loc.price;

    // Read what we need off the object before the memory goes back.
    forget_client_order(order->client_id(), order_id);
    forget_expiry(order);

    pool_.deallocate(order);
    level.erase(loc.pos);

    // An empty price level should not stay in the book.
    if (level.empty())
    {
        book.erase(level_it);
    }

    index_.erase(it);

    // Size is 0 because there is nothing left. The id alone tells a client
    // what to throw away.
    emit(md, MDType::Cancelled, order_id, side, price, 0);

    return true;
}

bool OrderBook::update(uint64_t order_id, uint64_t new_size,
                       std::vector<MDMessage> *md)
{
    auto it = index_.find(order_id);
    if (it == index_.end())
    {
        return false;
    }

    // Asking for zero means you want out. That is just a cancel.
    if (new_size == 0)
    {
        return cancel(order_id, md);
    }

    Location &loc = it->second;
    Order *order = *loc.pos;

    // Shrinking is free. You are asking for less, so you keep your place.
    if (new_size <= order->size())
    {
        // Tell the world it got smaller by exactly this much.
        // Executed is the right message here even though nothing traded,
        // because it is the one message that shrinks an order without
        // moving it, which is exactly what just happened.
        const uint64_t reduced_by = order->size() - new_size;

        order->set_size(new_size);

        if (reduced_by > 0)
        {
            emit(md, MDType::Executed, order_id, loc.side, loc.price, reduced_by);
        }

        return true;
    }

    // Growing costs you your place in the queue.
    // Otherwise you could jump ahead of people with size you never waited for.
    // The deadline does not move, because the order was placed when it was
    // placed. Resizing is not a fresh order.
    Book &book = (loc.side == Side::BUY) ? bids_ : asks_;
    PriceLevel &level = book.find(loc.price)->second;

    order->set_size(new_size);
    level.erase(loc.pos);
    level.push_back(order);

    // The old iterator points at a node that no longer exists.
    // Forget this line and a later cancel() reads freed memory.
    loc.pos = std::prev(level.end());

    // Two messages, because two things happened. It left its old spot and
    // joined the back of the line. A client that replays these ends up with
    // the order in the same place we just put it.
    emit(md, MDType::Cancelled, order_id, loc.side, loc.price, 0);
    emit(md, MDType::Added, order_id, loc.side, loc.price, new_size);

    return true;
}

std::size_t OrderBook::disconnect(uint64_t client_id, std::vector<MDMessage> *md)
{
    auto it = client_orders_.find(client_id);
    if (it == client_orders_.end())
    {
        return 0;
    }

    // Copy the ids out first.
    // cancel() erases from this very set, and changing a container while
    // looping over it is how you get a crash.
    const std::vector<uint64_t> ids(it->second.begin(), it->second.end());

    std::size_t removed = 0;
    for (const uint64_t id : ids)
    {
        // cancel() does the announcing for us, one message per order.
        if (cancel(id, md))
        {
            ++removed;
        }
    }

    return removed;
}

std::size_t OrderBook::purge_expired(uint64_t now, std::vector<MDMessage> *md)
{
    // Gather first, delete after.
    // cancel() reaches into expiry_, so we must not be looping over it
    // while that happens.
    std::vector<uint64_t> dead;

    // expiry_ is sorted by deadline, so we can stop at the first one that
    // is still alive. Everything past it is even further in the future.
    for (const auto &[deadline, ids] : expiry_)
    {
        if (deadline > now)
        {
            break;
        }

        dead.insert(dead.end(), ids.begin(), ids.end());
    }

    std::size_t removed = 0;
    for (const uint64_t id : dead)
    {
        // An expired order looks exactly like a cancelled one from outside.
        // The client does not need to know why it went away.
        if (cancel(id, md))
        {
            ++removed;
        }
    }

    return removed;
}

std::optional<uint64_t> OrderBook::next_expiry() const
{
    // Nobody in the book has a deadline.
    if (expiry_.empty())
    {
        return std::nullopt;
    }

    // Sorted, so the front is the soonest.
    return expiry_.begin()->first;
}

std::size_t OrderBook::orders_for_client(uint64_t client_id) const
{
    auto it = client_orders_.find(client_id);
    if (it == client_orders_.end())
    {
        return 0;
    }

    return it->second.size();
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