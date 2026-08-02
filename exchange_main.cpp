// Runs the exchange.
//
//   client threads --push--> LFQueue --pop--> engine thread --> OrderBook
//                                                                   |
//   client's MDBook <--pop-- LFQueue <--push-- market data ---------+
//
// Nobody locks the book. Clients never wait for a match; they drop a request
// into the queue and go back to work. One engine thread owns the book
// outright, which also means matching happens in a single, deterministic,
// first-come-first-served order.
//
// Order entry is one MPSC queue: every client pushes, the engine pops.
// Market data is one SPSC queue per client: the engine pushes, that one
// client pops. Nobody shares a market data queue, so nobody contends.
//
// Ctrl+C stops it cleanly and then checks whether every client rebuilt the
// exact same book the exchange has.

#include "lf_queue.h"
#include "md_book.h"
#include "md_message.h"
#include "order.h"
#include "order_book.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace
{

    // ------------------------------------------------------------------
    // Tunables
    // ------------------------------------------------------------------
    constexpr std::size_t kQueueCapacity = 4096; // must be a power of two
    constexpr std::size_t kMDQueueCapacity = 65536;
    constexpr int kNumClients = 4;
    constexpr std::size_t kPoolSize = 100000;
    constexpr auto kClientThinkTime = 2ms;
    constexpr auto kHeartbeat = 1s;

    // ------------------------------------------------------------------
    // Shutdown flag. The signal handler is only allowed to touch
    // async-signal-safe state, so it does nothing but flip this.
    // ------------------------------------------------------------------
    std::atomic<bool> g_running{true};
    void handle_sigint(int) { g_running = false; }

    // ------------------------------------------------------------------
    // What a client sends to the engine.
    // ------------------------------------------------------------------
    struct Request
    {
        enum class Kind
        {
            ADD,
            CANCEL
        };

        Kind kind{Kind::ADD};
        uint64_t order_id{};
        uint64_t client_id{};
        std::string ticker; // "AAPL" is short enough to stay on the stack (SSO)
        Side side{};
        OrderType type{};
        int64_t price{};
        uint64_t size{};
    };

    using RequestQueue = LFQueue<Request, kQueueCapacity>;
    using MDQueue = LFQueue<MDMessage, kMDQueueCapacity>;

    // ------------------------------------------------------------------
    // Everything one subscriber needs.
    //
    // The queue is written only by the engine and read only by that client's
    // thread, so it is single producer, single consumer. The book is touched
    // only by that client's thread while running, and by main afterwards once
    // the thread has been joined, so it never needs a lock either.
    // ------------------------------------------------------------------
    struct ClientFeed
    {
        MDQueue queue;
        MDBook book;

        // Messages the engine could not deliver because this client fell
        // behind and its queue filled up.
        std::atomic<uint64_t> dropped{0};

        // Messages this client actually applied.
        uint64_t applied{0};
    };

    // ------------------------------------------------------------------
    // The only place in this file that touches the book's API.
    // add() returns the trades it caused, so we count them.
    // Everything the book announces lands in md.
    // ------------------------------------------------------------------
    std::size_t apply(OrderBook &book, const Request &r, std::vector<MDMessage> &md)
    {
        switch (r.kind)
        {
        case Request::Kind::ADD:
        {
            auto trades = book.add(r.order_id, r.client_id, r.ticker,
                                   r.side, r.type, r.price, r.size, 0, 0, &md);
            return trades.size();
        }

        case Request::Kind::CANCEL:
            book.cancel(r.order_id, &md);
            return 0;
        }
        return 0;
    }

    // ------------------------------------------------------------------
    // Send everything the book just said to every subscriber.
    //
    // try_push, never a blocking push. If one client is slow and its queue
    // is full, that client loses a message and will see the gap in its
    // sequence numbers. The engine does not wait. A matching engine that
    // stalls because one subscriber is slow is a broken exchange.
    // ------------------------------------------------------------------
    void publish(std::vector<std::unique_ptr<ClientFeed>> &feeds,
                 std::vector<MDMessage> &md)
    {
        for (const MDMessage &m : md)
        {
            for (auto &feed : feeds)
            {
                if (!feed->queue.try_push(MDMessage{m}))
                {
                    feed->dropped.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }

        // Reuse the same vector next time round instead of making a new one.
        // clear() keeps the memory it already grew into.
        md.clear();
    }

    // ------------------------------------------------------------------
    // Client: generates random orders and pushes them, and follows the
    // market data feed to build its own copy of the book.
    // Many of these run at once.
    // ------------------------------------------------------------------
    void run_client(RequestQueue &queue, ClientFeed &feed,
                    uint64_t client_id, std::stop_token stop)
    {
        std::mt19937 rng(static_cast<unsigned>(client_id) * 7919u + 13u);
        std::uniform_int_distribution<int> price_dist(99, 101);
        std::uniform_int_distribution<int> size_dist(1, 50);
        std::uniform_int_distribution<int> coin(0, 1);

        // Order ids are unique per client without any shared counter:
        // client 3's first order is 3000000, its second is 3000001, ...
        uint64_t next_id = client_id * 1000000;
        uint64_t sent = 0;
        uint64_t dropped = 0;

        MDMessage m;

        while (!stop.stop_requested() && g_running.load(std::memory_order_relaxed))
        {
            Request r;
            r.kind = Request::Kind::ADD;
            r.order_id = next_id++;
            r.client_id = client_id;
            r.ticker = "AAPL";
            r.side = coin(rng) ? Side::BUY : Side::SELL;
            r.type = OrderType::LIMIT;
            r.price = price_dist(rng);
            r.size = static_cast<uint64_t>(size_dist(rng));

            if (queue.try_push(std::move(r)))
                ++sent;
            else
                ++dropped; // queue full: a real venue would reject or retry

            // Keep up with the feed. Everything waiting, then back to work.
            while (feed.queue.try_pop(m))
            {
                feed.book.apply(m);
                ++feed.applied;
            }

            std::this_thread::sleep_for(kClientThinkTime);
        }

        std::cout << "[client " << client_id << "] sent " << sent
                  << ", dropped " << dropped << '\n';
    }

    // ------------------------------------------------------------------
    // Engine: the ONLY thread that touches the book.
    // ------------------------------------------------------------------
    void run_engine(RequestQueue &queue, OrderBook &book,
                    std::vector<std::unique_ptr<ClientFeed>> &feeds,
                    std::stop_token stop)
    {
        Request r;
        uint64_t processed = 0;
        uint64_t trades = 0;
        uint64_t published = 0;
        auto last_beat = std::chrono::steady_clock::now();

        // One vector, reused forever. Declared out here so we are not
        // allocating a fresh one on every single order.
        std::vector<MDMessage> md;
        md.reserve(64);

        while (!stop.stop_requested())
        {
            if (queue.try_pop(r))
            {
                trades += apply(book, r, md);
                published += md.size();
                publish(feeds, md);
                ++processed;
            }
            else
            {
                // Nothing to do. A production engine would spin here;
                // sleeping keeps a laptop fan quiet.
                std::this_thread::sleep_for(100us);
            }

            // Show signs of life once a second.
            auto now = std::chrono::steady_clock::now();
            if (now - last_beat >= kHeartbeat)
            {
                last_beat = now;
                std::cout << "  ... " << processed << " orders, "
                          << trades << " trades, "
                          << book.order_count() << " resting\n";
            }
        }

        // Drain whatever the clients left behind before we quit.
        while (queue.try_pop(r))
        {
            trades += apply(book, r, md);
            published += md.size();
            publish(feeds, md);
            ++processed;
        }

        std::cout << "\n[engine] " << processed << " requests, "
                  << trades << " trades, "
                  << published << " market data messages\n";
        std::cout << "[book]   " << book.order_count() << " resting orders across "
                  << book.bid_levels() << " bid levels and "
                  << book.ask_levels() << " ask levels\n";

        if (auto b = book.best_bid())
            std::cout << "[book]   best bid " << *b << '\n';
        if (auto a = book.best_ask())
            std::cout << "[book]   best ask " << *a << '\n';
        if (auto s = book.spread())
            std::cout << "[book]   spread   " << *s << '\n';
    }

} // namespace

int main()
{
    std::signal(SIGINT, handle_sigint); // catch Ctrl+C

    OrderBook book(kPoolSize);
    RequestQueue queue;

    // unique_ptr because a queue holds atomics, so a ClientFeed can be
    // neither copied nor moved, and a vector needs to do one or the other
    // when it grows. Holding pointers sidesteps that entirely.
    std::vector<std::unique_ptr<ClientFeed>> feeds;
    feeds.reserve(kNumClients);
    for (int i = 0; i < kNumClients; ++i)
        feeds.push_back(std::make_unique<ClientFeed>());

    std::cout << "Exchange running. " << kNumClients
              << " clients, queue capacity " << RequestQueue::capacity()
              << ", market data queue capacity " << MDQueue::capacity()
              << ". Ctrl+C to stop.\n";

    std::jthread engine([&](std::stop_token st)
                        { run_engine(queue, book, feeds, st); });

    std::vector<std::jthread> clients;
    clients.reserve(kNumClients);
    for (int i = 0; i < kNumClients; ++i)
        clients.emplace_back([&queue, &feeds, i](std::stop_token st)
                             { run_client(queue, *feeds[i],
                                          static_cast<uint64_t>(i), st); });

    while (g_running.load(std::memory_order_relaxed))
        std::this_thread::sleep_for(50ms);

    std::cout << "\nSIGINT received - shutting down gracefully...\n";

    // Order matters: stop the clients first so no new work arrives,
    // join them, and only then let the engine drain and stop.
    for (auto &c : clients)
        c.request_stop();
    for (auto &c : clients)
        c.join();

    engine.request_stop();
    engine.join();

    // The clients stopped reading before the engine stopped writing, so
    // there is still market data sitting in their queues. Apply it now.
    // Both threads are joined, so touching these books here is safe.
    MDMessage m;
    for (auto &feed : feeds)
    {
        while (feed->queue.try_pop(m))
        {
            feed->book.apply(m);
            ++feed->applied;
        }
    }

    // The whole point of market data: did every client end up with the
    // same book the exchange has, having never once looked at it?
    std::cout << "\n=== market data check ===\n";
    const std::string truth = book.toString();

    bool all_matched = true;
    for (std::size_t i = 0; i < feeds.size(); ++i)
    {
        ClientFeed &feed = *feeds[i];
        const bool matched = (feed.book.toString() == truth);
        const uint64_t lost = feed.dropped.load(std::memory_order_relaxed);

        std::cout << "[client " << i << "] applied " << feed.applied
                  << ", dropped " << lost
                  << ", gaps " << feed.book.gaps()
                  << ", orders " << feed.book.order_count()
                  << " vs " << book.order_count()
                  << (matched ? "  MATCH" : "  MISMATCH") << '\n';

        if (!matched)
            all_matched = false;
    }

    std::cout << (all_matched
                      ? "\nEvery client rebuilt the book exactly from the feed.\n"
                      : "\nAt least one client's book drifted. Check drops above.\n");

    return 0;
}
