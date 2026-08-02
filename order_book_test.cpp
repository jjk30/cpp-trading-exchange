#include "order_book.h"

#include <gtest/gtest.h>

namespace
{

    TEST(OrderBookTest, AddsOrder)
    {
        OrderBook book(100);

        EXPECT_TRUE(book.add(1, 10, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5));

        EXPECT_EQ(book.order_count(), 1u);
        EXPECT_EQ(book.bid_levels(), 1u);
        EXPECT_EQ(book.ask_levels(), 0u);
    }

    TEST(OrderBookTest, RejectsDuplicateId)
    {
        OrderBook book(100);

        EXPECT_TRUE(book.add(1, 10, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5));
        EXPECT_FALSE(book.add(1, 10, "AAPL", Side::BUY, OrderType::LIMIT, 10001, 7));

        EXPECT_EQ(book.order_count(), 1u);
    }

    TEST(OrderBookTest, SamePriceSharesOneLevel)
    {
        OrderBook book(100);

        book.add(1, 10, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);
        book.add(2, 11, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 7);

        EXPECT_EQ(book.order_count(), 2u);
        EXPECT_EQ(book.bid_levels(), 1u);
    }

    TEST(OrderBookTest, CancelRemovesOrder)
    {
        OrderBook book(100);

        book.add(1, 10, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);

        EXPECT_TRUE(book.cancel(1));
        EXPECT_EQ(book.order_count(), 0u);
        EXPECT_EQ(book.bid_levels(), 0u);
    }

    TEST(OrderBookTest, CancelUnknownIdFails)
    {
        OrderBook book(100);

        EXPECT_FALSE(book.cancel(999));
    }

    TEST(OrderBookTest, CancelMiddleOfQueueKeepsOthers)
    {
        OrderBook book(100);

        book.add(1, 10, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);
        book.add(2, 11, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 7);
        book.add(3, 12, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 9);

        EXPECT_TRUE(book.cancel(2));

        EXPECT_EQ(book.order_count(), 2u);
        EXPECT_EQ(book.bid_levels(), 1u);
        EXPECT_TRUE(book.cancel(1));
        EXPECT_TRUE(book.cancel(3));
    }

    TEST(OrderBookTest, BidsAndAsksAreSeparate)
    {
        OrderBook book(100);

        book.add(1, 10, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);
        book.add(2, 11, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 3);

        EXPECT_EQ(book.bid_levels(), 1u);
        EXPECT_EQ(book.ask_levels(), 1u);
    }

    TEST(OrderBookTest, ToStringShowsBothSides)
    {
        OrderBook book(100);

        book.add(1, 10, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);
        book.add(2, 11, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 3);

        const std::string s = book.toString();

        EXPECT_NE(s.find("ASKS"), std::string::npos);
        EXPECT_NE(s.find("BIDS"), std::string::npos);
        EXPECT_NE(s.find("10000"), std::string::npos);
        EXPECT_NE(s.find("10002"), std::string::npos);
    }

}