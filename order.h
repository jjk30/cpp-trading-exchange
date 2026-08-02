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
          uint64_t timestamp = 0,
          uint64_t ttl = 0) noexcept
        : order_id_(order_id),
          client_id_(client_id),
          ticker_(std::move(ticker)),
          side_(side),
          type_(type),
          price_(price),
          size_(size),
          timestamp_(timestamp),
          ttl_(ttl)
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
    uint64_t ttl() const noexcept { return ttl_; }

    // matching engine shrinks this when the order is partially filled
    void set_size(uint64_t size) noexcept { size_ = size; }

    // A ttl of 0 means this order has no deadline and waits forever.
    bool has_deadline() const noexcept { return ttl_ > 0; }

    // The moment this order goes stale.
    // Only meaningful when has_deadline() is true.
    uint64_t expires_at() const noexcept { return timestamp_ + ttl_; }

    // Is this order past its deadline at time now?
    // Uses >= so an order with ttl 100 placed at 0 is dead at exactly 100,
    // not one tick later. Pick one and be consistent.
    bool is_expired(uint64_t now) const noexcept
    {
        return has_deadline() && now >= expires_at();
    }

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

    // How long this order is allowed to sit before it gets pulled.
    // 0 means no limit, which is the normal case.
    uint64_t ttl_;
};