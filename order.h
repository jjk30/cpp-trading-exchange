#pragma once

#include <cstdint>
#include <string>

// which direction the order goes
enum class Side : uint8_t
{
    BUY,
    SELL
};

// how the order behaves when it hits the book
enum class OrderType : uint8_t
{
    LIMIT,
    MARKET
};

class Order
{
public:
    Order(uint64_t order_id,
          uint64_t client_id,
          std::string ticker,
          Side side,
          OrderType type,
          int64_t price,
          uint64_t size,
          uint64_t timestamp = 0) noexcept
        : order_id_(order_id),
          client_id_(client_id),
          ticker_(std::move(ticker)),
          side_(side),
          type_(type),
          price_(price),
          size_(size),
          timestamp_(timestamp)
    {
    }

    uint64_t order_id() const noexcept { return order_id_; }
    uint64_t client_id() const noexcept { return client_id_; }
    const std::string &ticker() const noexcept { return ticker_; }
    Side side() const noexcept { return side_; }
    OrderType type() const noexcept { return type_; }
    int64_t price() const noexcept { return price_; }
    uint64_t size() const noexcept { return size_; }
    uint64_t timestamp() const noexcept { return timestamp_; }

    // matching engine shrinks this when the order is partially filled
    void set_size(uint64_t size) noexcept { size_ = size; }

    std::string toString() const
    {
        std::string out;
        out += "Order{id=" + std::to_string(order_id_);
        out += ", client=" + std::to_string(client_id_);
        out += ", " + ticker_;
        out += ", " + std::string(side_ == Side::BUY ? "BUY" : "SELL");
        out += ", " + std::string(type_ == OrderType::LIMIT ? "LIMIT" : "MARKET");
        out += ", price=" + std::to_string(price_);
        out += ", size=" + std::to_string(size_);
        out += "}";
        return out;
    }

private:
    uint64_t order_id_;
    uint64_t client_id_;
    std::string ticker_;
    Side side_;
    OrderType type_;
    int64_t price_; // in ticks/cents, never a double
    uint64_t size_;
    uint64_t timestamp_;
};