#include "md_book.h"
#include "order_book.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace
{

// Drive the real book, catch what it announces, and rebuild it from that.
// If the two ever disagree, the feed is missing something.
class FeedFixture : public ::testing::Test
{
protected:
    OrderBook exchange{1024};
    MDBook client;
    std::vector<MDMessage> feed;

    // Hand the client everything the exchange has said since last time.
    void drain()
    {
        for (const MDMessage &m : feed)
        {
            client.apply(m);
        }
        feed.clear();
    }

    // The whole point of the exercise.
    void expect_match()
    {
        drain();
        EXPECT_EQ(exchange.toString(), client.toString());
        EXPECT_EQ(exchange.order_count(), client.order_count());
        EXPECT_EQ(exchange.bid_levels(), client.bid_levels());
        EXPECT_EQ(exchange.ask_levels(), client.ask_levels());
        EXPECT_EQ(exchange.best_bid(), client.best_bid());
        EXPECT_EQ(exchange.best_ask(), client.best_ask());
        EXPECT_EQ(0u, client.gaps());
    }
};

TEST_F(FeedFixture, EmptyBooksMatch)
{
    expect_match();
}

TEST_F(FeedFixture, SingleRestingOrder)
{
    exchange.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 190, 500, 0, 0, &feed);
    expect_match();
}

TEST_F(FeedFixture, BothSidesNoCross)
{
    exchange.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 190, 500, 0, 0, &feed);
    exchange.add(2, 101, "AAPL", Side::SELL, OrderType::LIMIT, 191, 300, 0, 0, &feed);
    expect_match();
}

TEST_F(FeedFixture, ManyOrdersAtSamePrice)
{
    // Three in a line at one price. The client must keep them in order.
    exchange.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 190, 100, 0, 0, &feed);
    exchange.add(2, 101, "AAPL", Side::BUY, OrderType::LIMIT, 190, 200, 0, 0, &feed);
    exchange.add(3, 102, "AAPL", Side::BUY, OrderType::LIMIT, 190, 300, 0, 0, &feed);
    expect_match();
}

TEST_F(FeedFixture, PartialFillShrinksRestingOrder)
{
    exchange.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 190, 500, 0, 0, &feed);
    exchange.add(2, 101, "AAPL", Side::SELL, OrderType::LIMIT, 190, 200, 0, 0, &feed);

    // 300 should be left resting on the bid.
    expect_match();
}

TEST_F(FeedFixture, FullFillEmptiesTheBook)
{
    exchange.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 190, 500, 0, 0, &feed);
    exchange.add(2, 101, "AAPL", Side::SELL, OrderType::LIMIT, 190, 500, 0, 0, &feed);
    expect_match();
}

TEST_F(FeedFixture, SweepsSeveralPriceLevels)
{
    exchange.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 191, 100, 0, 0, &feed);
    exchange.add(2, 101, "AAPL", Side::SELL, OrderType::LIMIT, 192, 100, 0, 0, &feed);
    exchange.add(3, 102, "AAPL", Side::SELL, OrderType::LIMIT, 193, 100, 0, 0, &feed);

    // One big buy that eats through all three and rests with what is left.
    exchange.add(4, 103, "AAPL", Side::BUY, OrderType::LIMIT, 193, 350, 0, 0, &feed);
    expect_match();
}

TEST_F(FeedFixture, MarketOrderLeavesNothingBehind)
{
    exchange.add(1, 100, "AAPL", Side::SELL, OrderType::LIMIT, 191, 100, 0, 0, &feed);

    // Asks for more than exists. The leftover is thrown away, not rested.
    exchange.add(2, 101, "AAPL", Side::BUY, OrderType::MARKET, 0, 500, 0, 0, &feed);
    expect_match();
}

TEST_F(FeedFixture, CancelRemovesOrder)
{
    exchange.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 190, 500, 0, 0, &feed);
    exchange.add(2, 101, "AAPL", Side::BUY, OrderType::LIMIT, 189, 200, 0, 0, &feed);
    exchange.cancel(1, &feed);
    expect_match();
}

TEST_F(FeedFixture, CancelLastOrderAtPriceRemovesLevel)
{
    exchange.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 190, 500, 0, 0, &feed);
    exchange.cancel(1, &feed);
    expect_match();
}

TEST_F(FeedFixture, ShrinkKeepsQueuePosition)
{
    exchange.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 190, 500, 0, 0, &feed);
    exchange.add(2, 101, "AAPL", Side::BUY, OrderType::LIMIT, 190, 200, 0, 0, &feed);

    // Order 1 gets smaller but stays at the front.
    exchange.update(1, 300, &feed);
    expect_match();
}

TEST_F(FeedFixture, GrowSendsOrderToBackOfQueue)
{
    exchange.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 190, 100, 0, 0, &feed);
    exchange.add(2, 101, "AAPL", Side::BUY, OrderType::LIMIT, 190, 200, 0, 0, &feed);

    // Order 1 grows, so it loses its place. The client must move it too.
    exchange.update(1, 400, &feed);
    expect_match();
}

TEST_F(FeedFixture, UpdateToZeroIsACancel)
{
    exchange.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 190, 500, 0, 0, &feed);
    exchange.update(1, 0, &feed);
    expect_match();
}

TEST_F(FeedFixture, DisconnectRemovesEveryOrderForClient)
{
    exchange.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 190, 100, 0, 0, &feed);
    exchange.add(2, 100, "AAPL", Side::BUY, OrderType::LIMIT, 189, 100, 0, 0, &feed);
    exchange.add(3, 101, "AAPL", Side::SELL, OrderType::LIMIT, 195, 100, 0, 0, &feed);

    exchange.disconnect(100, &feed);
    expect_match();
}

TEST_F(FeedFixture, ExpiredOrdersLeaveBothBooks)
{
    exchange.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 190, 100, 10, 5, &feed);
    exchange.add(2, 101, "AAPL", Side::BUY, OrderType::LIMIT, 189, 100, 10, 0, &feed);

    // Order 1 dies at 15. Order 2 has no deadline and stays.
    exchange.purge_expired(20, &feed);
    expect_match();
}

TEST_F(FeedFixture, SelfTradeStillShrinksBothBooks)
{
    // Same client on both sides. No trade happens, but size still vanishes,
    // and the client copy has to vanish with it.
    exchange.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 190, 500, 0, 0, &feed);
    exchange.add(2, 100, "AAPL", Side::SELL, OrderType::LIMIT, 190, 200, 0, 0, &feed);
    expect_match();
}

TEST_F(FeedFixture, LongMessySequence)
{
    // The real test. Everything at once, checked only at the very end.
    exchange.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 190, 500, 0, 0, &feed);
    exchange.add(2, 101, "AAPL", Side::BUY, OrderType::LIMIT, 189, 300, 0, 0, &feed);
    exchange.add(3, 102, "AAPL", Side::SELL, OrderType::LIMIT, 192, 400, 0, 0, &feed);
    exchange.add(4, 103, "AAPL", Side::SELL, OrderType::LIMIT, 193, 200, 0, 0, &feed);

    exchange.add(5, 104, "AAPL", Side::SELL, OrderType::LIMIT, 190, 200, 0, 0, &feed);
    exchange.update(2, 150, &feed);
    exchange.cancel(4, &feed);

    exchange.add(6, 105, "AAPL", Side::BUY, OrderType::LIMIT, 189, 100, 0, 0, &feed);
    exchange.update(6, 600, &feed);

    exchange.add(7, 106, "AAPL", Side::BUY, OrderType::LIMIT, 192, 1000, 0, 0, &feed);
    exchange.add(8, 107, "AAPL", Side::SELL, OrderType::LIMIT, 194, 250, 20, 5, &feed);

    exchange.purge_expired(30, &feed);
    exchange.disconnect(101, &feed);

    expect_match();
}

TEST_F(FeedFixture, SequenceNumbersHaveNoHoles)
{
    exchange.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 190, 500, 0, 0, &feed);
    exchange.add(2, 101, "AAPL", Side::SELL, OrderType::LIMIT, 190, 200, 0, 0, &feed);
    exchange.cancel(1, &feed);

    // First message is 1, and every one after is exactly one more.
    ASSERT_FALSE(feed.empty());
    for (std::size_t i = 0; i < feed.size(); ++i)
    {
        EXPECT_EQ(i + 1, feed[i].seq);
    }
}

TEST_F(FeedFixture, ClientNoticesADroppedMessage)
{
    exchange.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 190, 100, 0, 0, &feed);
    exchange.add(2, 101, "AAPL", Side::BUY, OrderType::LIMIT, 189, 100, 0, 0, &feed);

    ASSERT_EQ(2u, feed.size());

    // Throw one away on purpose, the way a real network would.
    client.apply(feed[1]);

    // The client must not pretend everything is fine.
    EXPECT_EQ(1u, client.gaps());
}

TEST_F(FeedFixture, NullFeedPointerCostsNothing)
{
    // Every existing caller passes nothing. That must still work, and must
    // not burn sequence numbers that a real subscriber would then miss.
    exchange.add(1, 100, "AAPL", Side::BUY, OrderType::LIMIT, 190, 500);
    exchange.add(2, 101, "AAPL", Side::BUY, OrderType::LIMIT, 189, 200, 0, 0, &feed);

    ASSERT_EQ(1u, feed.size());
    EXPECT_EQ(1u, feed[0].seq);
}

}  // namespace
