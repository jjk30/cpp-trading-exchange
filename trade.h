#pragma once

#include <cstdint>
#include <string>

// A Trade is the record of one fill.
// It gets made when a new order eats into an order already sitting in the book.
// The book itself does not store these. It hands them back to the caller.
struct Trade
{
    uint64_t buy_order_id;
    uint64_t sell_order_id;

    // The price the resting order asked for, not the price the new order offered.
    // The one who waited gets their price. That is their reward for waiting.
    int64_t price;

    // The smaller of the two sizes.
    // You can only trade as much as the smaller side has.
    uint64_t size;

    std::string toString() const
    {
        std::string out = "Trade{buy=" + std::to_string(buy_order_id);
        out += ", sell=" + std::to_string(sell_order_id);
        out += ", price=" + std::to_string(price);
        out += ", size=" + std::to_string(size);
        out += "}";
        return out;
    }
};