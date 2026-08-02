#include "order_book.h"

#include <gtest/gtest.h>

namespace
{

    TEST(OrderBookTest, AddsOrder)
    {
        OrderBook book(100);

        book.add(1, 10, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);

        EXPECT_EQ(book.order_count(), 1u);
        EXPECT_EQ(book.bid_levels(), 1u);
        EXPECT_EQ(book.ask_levels(), 0u);
    }

    TEST(OrderBookTest, RejectsDuplicateId)
    {
        OrderBook book(100);

        book.add(1, 10, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);
        book.add(1, 10, "AAPL", Side::BUY, OrderType::LIMIT, 10001, 7);

        EXPECT_EQ(book.order_count(), 1u);
    }

    TEST(OrderBookTest, RejectsZeroSize)
    {
        OrderBook book(100);

        book.add(1, 10, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 0);

        EXPECT_EQ(book.order_count(), 0u);
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

        book.add(2, 200, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 5);
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

    TEST(OrderBookMatch, NoTradeWhenPricesDontCross)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10005, 5);
        const auto trades = book.add(2, 200, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);

        EXPECT_TRUE(trades.empty());
        EXPECT_EQ(book.order_count(), 2u);
    }

    TEST(OrderBookMatch, ExactFillClearsBothSides)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 5);
        const auto trades = book.add(2, 200, "AAPL", Side::BUY, OrderType::LIMIT, 10002, 5);

        ASSERT_EQ(trades.size(), 1u);
        EXPECT_EQ(trades[0].buy_order_id, 2u);
        EXPECT_EQ(trades[0].sell_order_id, 1u);
        EXPECT_EQ(trades[0].size, 5u);

        EXPECT_EQ(book.order_count(), 0u);
        EXPECT_EQ(book.ask_levels(), 0u);
        EXPECT_EQ(book.bid_levels(), 0u);
    }

    TEST(OrderBookMatch, TradeHappensAtRestingPrice)
    {
        OrderBook book(10);

        // seller asked 10000, buyer offered 10005.
        // the seller waited, so the seller's price wins.
        book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10000, 5);
        const auto trades = book.add(2, 200, "AAPL", Side::BUY, OrderType::LIMIT, 10005, 5);

        ASSERT_EQ(trades.size(), 1u);
        EXPECT_EQ(trades[0].price, 10000);
    }

    TEST(OrderBookMatch, SmallIncomingLeavesRestingBehind)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 10);
        const auto trades = book.add(2, 200, "AAPL", Side::BUY, OrderType::LIMIT, 10002, 4);

        ASSERT_EQ(trades.size(), 1u);
        EXPECT_EQ(trades[0].size, 4u);

        EXPECT_EQ(book.order_count(), 1u);
        EXPECT_NE(book.toString().find("10002 : 6"), std::string::npos);
    }

    TEST(OrderBookMatch, BigIncomingRestsWithLeftover)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 3);
        const auto trades = book.add(2, 200, "AAPL", Side::BUY, OrderType::LIMIT, 10002, 10);

        ASSERT_EQ(trades.size(), 1u);
        EXPECT_EQ(trades[0].size, 3u);

        EXPECT_EQ(book.ask_levels(), 0u);
        EXPECT_EQ(book.order_count(), 1u);
        ASSERT_TRUE(book.best_bid().has_value());
        EXPECT_EQ(book.best_bid().value(), 10002);
        EXPECT_NE(book.toString().find("10002 : 7"), std::string::npos);
    }

    TEST(OrderBookMatch, OldestRestingOrderFillsFirst)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 5);
        book.add(2, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 5);

        const auto trades = book.add(3, 200, "AAPL", Side::BUY, OrderType::LIMIT, 10002, 5);

        ASSERT_EQ(trades.size(), 1u);
        EXPECT_EQ(trades[0].sell_order_id, 1u);
    }

    TEST(OrderBookMatch, EatsCheapestAskFirst)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10003, 2);
        book.add(2, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10001, 2);
        book.add(3, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 2);

        const auto trades = book.add(4, 200, "AAPL", Side::BUY, OrderType::LIMIT, 10002, 4);

        ASSERT_EQ(trades.size(), 2u);
        EXPECT_EQ(trades[0].price, 10001);
        EXPECT_EQ(trades[1].price, 10002);

        // 10003 was too expensive, so it survives
        EXPECT_EQ(book.ask_levels(), 1u);
        EXPECT_EQ(book.best_ask().value(), 10003);
    }

    TEST(OrderBookMatch, SellCrossesIntoBids)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);
        const auto trades = book.add(2, 200, "AAPL", Side::SELL, OrderType::LIMIT, 9999, 5);

        ASSERT_EQ(trades.size(), 1u);
        EXPECT_EQ(trades[0].buy_order_id, 1u);
        EXPECT_EQ(trades[0].sell_order_id, 2u);
        EXPECT_EQ(trades[0].price, 10000);
        EXPECT_EQ(book.order_count(), 0u);
    }

    TEST(OrderBookMatch, MarketOrderIgnoresPrice)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10001, 2);
        book.add(2, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10009, 3);

        // price 0 is meaningless for a market order. it takes whatever is there.
        const auto trades = book.add(3, 200, "AAPL", Side::BUY, OrderType::MARKET, 0, 5);

        ASSERT_EQ(trades.size(), 2u);
        EXPECT_EQ(trades[0].price, 10001);
        EXPECT_EQ(trades[1].price, 10009);
        EXPECT_EQ(book.order_count(), 0u);
    }

    TEST(OrderBookMatch, MarketOrderNeverRests)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 3);

        // wants 10 but only 3 exist. the other 7 are thrown away, not booked.
        const auto trades = book.add(2, 200, "AAPL", Side::BUY, OrderType::MARKET, 0, 10);

        ASSERT_EQ(trades.size(), 1u);
        EXPECT_EQ(trades[0].size, 3u);
        EXPECT_EQ(book.order_count(), 0u);
        EXPECT_EQ(book.bid_levels(), 0u);
    }

    TEST(OrderBookMatch, MarketOrderOnEmptyBookDoesNothing)
    {
        OrderBook book(10);

        const auto trades = book.add(1, 100, "AAPL", Side::BUY, OrderType::MARKET, 0, 5);

        EXPECT_TRUE(trades.empty());
        EXPECT_EQ(book.order_count(), 0u);
    }

    TEST(OrderBookMatch, CancelStillWorksAfterPartialFill)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 10);
        book.add(2, 200, "AAPL", Side::BUY, OrderType::LIMIT, 10002, 4);

        // order 1 is still there with 6 left, so cancelling it must work
        EXPECT_TRUE(book.cancel(1));
        EXPECT_EQ(book.order_count(), 0u);
    }

    TEST(OrderBookMatch, FilledOrderIsForgotten)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 5);
        book.add(2, 200, "AAPL", Side::BUY, OrderType::LIMIT, 10002, 5);

        // order 1 traded away completely, so the book should not know it
        EXPECT_FALSE(book.cancel(1));
        EXPECT_FALSE(book.update(1, 3));
    }

    TEST(OrderBookSelfTrade, EqualSizesKillEachOther)
    {
        OrderBook book(10);

        // same client on both sides
        book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 5);
        const auto trades = book.add(2, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10002, 5);

        // no trade, but both orders are gone
        EXPECT_TRUE(trades.empty());
        EXPECT_EQ(book.order_count(), 0u);
    }

    TEST(OrderBookSelfTrade, BiggerRestingOrderKeepsRemainder)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 10);
        const auto trades = book.add(2, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10002, 4);

        EXPECT_TRUE(trades.empty());

        // 4 killed off both, so 6 of the sell order survives
        EXPECT_EQ(book.order_count(), 1u);
        EXPECT_NE(book.toString().find("10002 : 6"), std::string::npos);
    }

    TEST(OrderBookSelfTrade, BiggerIncomingOrderKeepsRemainder)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 3);
        const auto trades = book.add(2, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10002, 10);

        EXPECT_TRUE(trades.empty());

        // seller wiped out, buyer's leftover 7 rests
        EXPECT_EQ(book.ask_levels(), 0u);
        EXPECT_EQ(book.order_count(), 1u);
        EXPECT_NE(book.toString().find("10002 : 7"), std::string::npos);
    }

    TEST(OrderBookSelfTrade, DifferentClientsStillTrade)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 5);
        const auto trades = book.add(2, 200, "AAPL", Side::BUY, OrderType::LIMIT, 10002, 5);

        ASSERT_EQ(trades.size(), 1u);
        EXPECT_EQ(book.order_count(), 0u);
    }

    TEST(OrderBookSelfTrade, SkipsOwnOrderThenTradesWithNext)
    {
        OrderBook book(10);

        // my own order is first in line, someone else is behind it
        book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 4);
        book.add(2, 200, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 6);

        const auto trades = book.add(3, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10002, 10);

        // 4 vanished against my own order, the other 6 traded for real
        ASSERT_EQ(trades.size(), 1u);
        EXPECT_EQ(trades[0].sell_order_id, 2u);
        EXPECT_EQ(trades[0].size, 6u);
        EXPECT_EQ(book.order_count(), 0u);
    }

    TEST(OrderBookSelfTrade, PriceTimePriorityIsUnchanged)
    {
        OrderBook book(10);

        // someone else is first in line, my own order is behind them
        book.add(1, 200, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 3);
        book.add(2, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 3);

        const auto trades = book.add(3, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10002, 6);

        // the front of the queue still goes first, self trade or not
        ASSERT_EQ(trades.size(), 1u);
        EXPECT_EQ(trades[0].sell_order_id, 1u);
        EXPECT_EQ(book.order_count(), 0u);
    }

    TEST(OrderBookSelfTrade, WorksForMarketOrdersToo)
    {
        OrderBook book(10);

        book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 5);
        const auto trades = book.add(2, 100, "AAPL", Side::BUY, OrderType::MARKET, 0, 5);

        EXPECT_TRUE(trades.empty());
        EXPECT_EQ(book.order_count(), 0u);
    }

}
TEST(OrderBookDisconnect, UnknownClientRemovesNothing)
{
    OrderBook book(10);

    book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);

    EXPECT_EQ(book.disconnect(999), 0u);
    EXPECT_EQ(book.order_count(), 1u);
}

TEST(OrderBookDisconnect, RemovesAllOrdersForOneClient)
{
    OrderBook book(10);

    book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);
    book.add(2, 100, "AAPL", Side::BUY, OrderType::LIMIT, 9999, 5);
    book.add(3, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10005, 5);

    EXPECT_EQ(book.disconnect(100), 3u);
    EXPECT_EQ(book.order_count(), 0u);
    EXPECT_EQ(book.bid_levels(), 0u);
    EXPECT_EQ(book.ask_levels(), 0u);
}

TEST(OrderBookDisconnect, LeavesOtherClientsAlone)
{
    OrderBook book(10);

    book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);
    book.add(2, 200, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 7);
    book.add(3, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 9);

    EXPECT_EQ(book.disconnect(100), 2u);

    // client 200's order is still sitting there
    EXPECT_EQ(book.order_count(), 1u);
    EXPECT_NE(book.toString().find("10000 : 7"), std::string::npos);
}

TEST(OrderBookDisconnect, TwiceIsHarmless)
{
    OrderBook book(10);

    book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);

    EXPECT_EQ(book.disconnect(100), 1u);
    EXPECT_EQ(book.disconnect(100), 0u);
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(OrderBookDisconnect, CountTracksAddAndCancel)
{
    OrderBook book(10);

    EXPECT_EQ(book.orders_for_client(100), 0u);

    book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);
    book.add(2, 100, "AAPL", Side::BUY, OrderType::LIMIT, 9999, 5);
    EXPECT_EQ(book.orders_for_client(100), 2u);

    book.cancel(1);
    EXPECT_EQ(book.orders_for_client(100), 1u);
}

TEST(OrderBookDisconnect, FilledOrderStopsCountingAgainstClient)
{
    OrderBook book(10);

    book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 5);
    EXPECT_EQ(book.orders_for_client(100), 1u);

    // someone eats the whole thing, so client 100 has nothing left
    book.add(2, 200, "AAPL", Side::BUY, OrderType::LIMIT, 10002, 5);

    EXPECT_EQ(book.orders_for_client(100), 0u);
    EXPECT_EQ(book.disconnect(100), 0u);
}

TEST(OrderBookDisconnect, PartiallyFilledOrderStillBelongsToClient)
{
    OrderBook book(10);

    book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 10);
    book.add(2, 200, "AAPL", Side::BUY, OrderType::LIMIT, 10002, 4);

    // 6 still resting, so it must still come out on disconnect
    EXPECT_EQ(book.orders_for_client(100), 1u);
    EXPECT_EQ(book.disconnect(100), 1u);
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(OrderBookDisconnect, MarketOrderNeverCountsAgainstClient)
{
    OrderBook book(10);

    book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 3);

    // market order takes 3 and throws away the other 7, never resting
    book.add(2, 200, "AAPL", Side::BUY, OrderType::MARKET, 0, 10);

    EXPECT_EQ(book.orders_for_client(200), 0u);
}

TEST(OrderBookDisconnect, SurvivesUpSize)
{
    OrderBook book(10);

    book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5);
    book.add(2, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 7);

    // up-size moves the list node, but ownership should not change
    EXPECT_TRUE(book.update(1, 50));

    EXPECT_EQ(book.orders_for_client(100), 2u);
    EXPECT_EQ(book.disconnect(100), 2u);
    EXPECT_EQ(book.order_count(), 0u);
}
TEST(OrderBookTTL, NoTtlMeansNeverExpires)
{
    OrderBook book(10);

    book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5, 0, 0);

    EXPECT_EQ(book.purge_expired(999999), 0u);
    EXPECT_EQ(book.order_count(), 1u);
    EXPECT_FALSE(book.next_expiry().has_value());
}

TEST(OrderBookTTL, ExpiresExactlyOnDeadline)
{
    OrderBook book(10);

    // placed at 1000, lives 100, so it dies at 1100
    book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5, 1000, 100);

    ASSERT_TRUE(book.next_expiry().has_value());
    EXPECT_EQ(book.next_expiry().value(), 1100u);

    EXPECT_EQ(book.purge_expired(1099), 0u);
    EXPECT_EQ(book.order_count(), 1u);

    EXPECT_EQ(book.purge_expired(1100), 1u);
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(OrderBookTTL, OnlyTakesTheOnesPastTheirTime)
{
    OrderBook book(10);

    book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5, 0, 100);
    book.add(2, 100, "AAPL", Side::BUY, OrderType::LIMIT, 9999, 5, 0, 200);
    book.add(3, 100, "AAPL", Side::BUY, OrderType::LIMIT, 9998, 5, 0, 300);

    EXPECT_EQ(book.purge_expired(200), 2u);
    EXPECT_EQ(book.order_count(), 1u);

    // only the 300 one is left
    ASSERT_TRUE(book.next_expiry().has_value());
    EXPECT_EQ(book.next_expiry().value(), 300u);
}

TEST(OrderBookTTL, ExpiredAndForeverCanShareTheBook)
{
    OrderBook book(10);

    book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5, 0, 50);
    book.add(2, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 7, 0, 0);

    EXPECT_EQ(book.purge_expired(100), 1u);

    // the one with no deadline is untouched
    EXPECT_EQ(book.order_count(), 1u);
    EXPECT_NE(book.toString().find("10000 : 7"), std::string::npos);
}

TEST(OrderBookTTL, SeveralOrdersDyingAtTheSameMoment)
{
    OrderBook book(10);

    book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5, 0, 100);
    book.add(2, 200, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5, 50, 50);

    // both land on 100 exactly
    ASSERT_TRUE(book.next_expiry().has_value());
    EXPECT_EQ(book.next_expiry().value(), 100u);

    EXPECT_EQ(book.purge_expired(100), 2u);
    EXPECT_EQ(book.order_count(), 0u);
    EXPECT_FALSE(book.next_expiry().has_value());
}

TEST(OrderBookTTL, CancelClearsTheDeadline)
{
    OrderBook book(10);

    book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5, 0, 100);
    EXPECT_TRUE(book.cancel(1));

    // gone by hand, so there is nothing left to expire
    EXPECT_FALSE(book.next_expiry().has_value());
    EXPECT_EQ(book.purge_expired(999), 0u);
}

TEST(OrderBookTTL, FilledOrderClearsTheDeadline)
{
    OrderBook book(10);

    book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 5, 0, 100);
    book.add(2, 200, "AAPL", Side::BUY, OrderType::LIMIT, 10002, 5);

    // traded away, so its deadline must go with it
    EXPECT_FALSE(book.next_expiry().has_value());
    EXPECT_EQ(book.purge_expired(999), 0u);
}

TEST(OrderBookTTL, UpSizeDoesNotResetTheClock)
{
    OrderBook book(10);

    book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5, 0, 100);
    EXPECT_TRUE(book.update(1, 50));

    // you lose your queue spot, but you do not get a fresh lifetime
    ASSERT_TRUE(book.next_expiry().has_value());
    EXPECT_EQ(book.next_expiry().value(), 100u);
    EXPECT_EQ(book.purge_expired(100), 1u);
}

TEST(OrderBookTTL, PurgingTwiceIsHarmless)
{
    OrderBook book(10);

    book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5, 0, 100);

    EXPECT_EQ(book.purge_expired(200), 1u);
    EXPECT_EQ(book.purge_expired(200), 0u);
    EXPECT_EQ(book.order_count(), 0u);
}

TEST(OrderBookTTL, DisconnectClearsDeadlinesToo)
{
    OrderBook book(10);

    book.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 10000, 5, 0, 100);
    book.add(2, 100, "AAPL", Side::BUY, OrderType::LIMIT, 9999, 5, 0, 200);

    EXPECT_EQ(book.disconnect(100), 2u);
    EXPECT_FALSE(book.next_expiry().has_value());
}

TEST(OrderBookTTL, PartiallyFilledOrderKeepsItsDeadline)
{
    OrderBook book(10);

    book.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 10002, 10, 0, 100);
    book.add(2, 200, "AAPL", Side::BUY, OrderType::LIMIT, 10002, 4);

    // 6 still resting, and it is still on the same clock
    ASSERT_TRUE(book.next_expiry().has_value());
    EXPECT_EQ(book.next_expiry().value(), 100u);
    EXPECT_EQ(book.purge_expired(100), 1u);
    EXPECT_EQ(book.order_count(), 0u);
}