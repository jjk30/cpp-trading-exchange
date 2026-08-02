#include "order.h"

#include <gtest/gtest.h>

namespace
{

    TEST(OrderTest, StoresAllFields)
    {
        Order o(567890, 1234, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 100);

        EXPECT_EQ(o.order_id(), 567890u);
        EXPECT_EQ(o.client_id(), 1234u);
        EXPECT_EQ(o.ticker(), "AAPL");
        EXPECT_EQ(o.side(), Side::BUY);
        EXPECT_EQ(o.type(), OrderType::LIMIT);
        EXPECT_EQ(o.price(), 10000);
        EXPECT_EQ(o.size(), 100u);
    }

    TEST(OrderTest, SetSizeShrinksOrder)
    {
        Order o(1, 1, "AAPL", Side::SELL, OrderType::LIMIT, 10001, 100);

        o.set_size(40);

        EXPECT_EQ(o.size(), 40u);
    }

    TEST(OrderTest, ToStringHasKeyFields)
    {
        Order o(42, 7, "MSFT", Side::SELL, OrderType::MARKET, 20050, 50);

        const std::string s = o.toString();

        EXPECT_NE(s.find("id=42"), std::string::npos);
        EXPECT_NE(s.find("MSFT"), std::string::npos);
        EXPECT_NE(s.find("SELL"), std::string::npos);
        EXPECT_NE(s.find("MARKET"), std::string::npos);
        EXPECT_NE(s.find("price=20050"), std::string::npos);
    }

} // namespace