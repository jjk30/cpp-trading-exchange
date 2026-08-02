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

    TEST(OrderBookBest, EmptyBookHasNoBest)
    {
        OrderBook book(10);

        EXPECT_FALSE(book.best_bid().has_value());
        EXPECT_FALSE(book.best_ask().has_value());
        EXPECT_FALSE(book.spread().has_value());
    }

    TEST(OrderBookBest, BestBidIsHighestBuy)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 9998, 5);
        book.add(2, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10001, 5);
        book.add(3, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);

        ASSERT_TRUE(book.best_bid().has_value());
        EXPECT_EQ(book.best_bid().value(), 10001);
    }

    TEST(OrderBookBest, BestAskIsLowestSell)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10005, 5);
        book.add(2, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 5);
        book.add(3, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10003, 5);

        ASSERT_TRUE(book.best_ask().has_value());
        EXPECT_EQ(book.best_ask().value(), 10002);
    }

    TEST(OrderBookBest, SpreadNeedsBothSides)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10001, 5);
        EXPECT_FALSE(book.spread().has_value());

        book.add(2, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 5);
        ASSERT_TRUE(book.spread().has_value());
        EXPECT_EQ(book.spread().value(), 1);
    }

    TEST(OrderBookBest, CancellingBestMovesToNextLevel)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10001, 5);
        book.add(2, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);

        ASSERT_TRUE(book.best_bid().has_value());
        EXPECT_EQ(book.best_bid().value(), 10001);

        EXPECT_TRUE(book.cancel(1));

        ASSERT_TRUE(book.best_bid().has_value());
        EXPECT_EQ(book.best_bid().value(), 10000);
    }

    TEST(OrderBookUpdate, UnknownIdFails)
    {
        OrderBook book(10);

        EXPECT_FALSE(book.update(999, 5));
    }

    TEST(OrderBookUpdate, DownSizeKeepsQueuePosition)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);
        book.add(2, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 7);
        book.add(3, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 9);

        EXPECT_TRUE(book.update(2, 3));

        // still second in line, just smaller
        EXPECT_NE(book.toString().find("10000 : 5 3 9"), std::string::npos);
    }

    TEST(OrderBookUpdate, SameSizeKeepsQueuePosition)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);
        book.add(2, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 7);

        EXPECT_TRUE(book.update(1, 5));

        EXPECT_NE(book.toString().find("10000 : 5 7"), std::string::npos);
    }

    TEST(OrderBookUpdate, UpSizeGoesToBack)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);
        book.add(2, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 7);
        book.add(3, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 9);

        EXPECT_TRUE(book.update(2, 20));

        // lost its spot, now last
        EXPECT_NE(book.toString().find("10000 : 5 9 20"), std::string::npos);
        EXPECT_EQ(book.order_count(), 3u);
    }

    TEST(OrderBookUpdate, ZeroSizeCancels)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);

        EXPECT_TRUE(book.update(1, 0));
        EXPECT_EQ(book.order_count(), 0u);
        EXPECT_EQ(book.bid_levels(), 0u);
    }

    TEST(OrderBookUpdate, CancelStillWorksAfterUpSize)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);
        book.add(2, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 7);

        EXPECT_TRUE(book.update(1, 50));

        // if the saved iterator wasn't refreshed, this walks into a dead node
        EXPECT_TRUE(book.cancel(1));
        EXPECT_EQ(book.order_count(), 1u);
        EXPECT_TRUE(book.cancel(2));
        EXPECT_EQ(book.bid_levels(), 0u);
    }

    TEST(OrderBookUpdate, WorksOnAskSideToo)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 4);
        book.add(2, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 6);

        EXPECT_TRUE(book.update(1, 30));

        EXPECT_NE(book.toString().find("10002 : 6 30"), std::string::npos);
    }

} // namespace