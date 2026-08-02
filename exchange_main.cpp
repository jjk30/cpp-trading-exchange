// Runs the exchange.
//
//   client threads --push--> LFQueue --pop--> engine thread --> OrderBook
//
// Nobody locks the book. Clients never wait for a match; they drop a request
// into the queue and go back to work. One engine thread owns the book
// outright, which also means matching happens in a single, deterministic,
// first-come-first-served order.
//
// Ctrl+C stops it cleanly.

#include "lf_queue.h"
#include "order.h"
#include "order_book.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <iostream>
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

    // ------------------------------------------------------------------
    // The only place in this file that touches the book's API.
    // add() returns the trades it caused, so we count them.
    // ------------------------------------------------------------------
    std::size_t apply(OrderBook &book, const Request &r)
    {
        switch (r.kind)
        {
        case Request::Kind::ADD:
        {
            auto trades = book.add(r.order_id, r.client_id, r.ticker,
                                   r.side, r.type, r.price, r.size);
            return trades.size();
        }

        case Request::Kind::CANCEL:
            book.cancel(r.order_id);
            return 0;
        }
        return 0;
    }

    // ------------------------------------------------------------------
    // Client: generates random orders and pushes them.
    // Many of these run at once.
    // ------------------------------------------------------------------
    void run_client(RequestQueue &queue, uint64_t client_id, std::stop_token stop)
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

            std::this_thread::sleep_for(kClientThinkTime);
        }

        std::cout << "[client " << client_id << "] sent " << sent
                  << ", dropped " << dropped << '\n';
    }

    // ------------------------------------------------------------------
    // Engine: the ONLY thread that touches the book.
    // ------------------------------------------------------------------
    void run_engine(RequestQueue &queue, OrderBook &book, std::stop_token stop)
    {
        Request r;
        uint64_t processed = 0;
        uint64_t trades = 0;
        auto last_beat = std::chrono::steady_clock::now();

        while (!stop.stop_requested())
        {
            if (queue.try_pop(r))
            {
                trades += apply(book, r);
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
            trades += apply(book, r);
            ++processed;
        }

        std::cout << "\n[engine] " << processed << " requests, "
                  << trades << " trades\n";
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

    std::cout << "Exchange running. " << kNumClients
              << " clients, queue capacity " << RequestQueue::capacity()
              << ". Ctrl+C to stop.\n";

    std::jthread engine([&](std::stop_token st)
                        { run_engine(queue, book, st); });

    std::vector<std::jthread> clients;
    clients.reserve(kNumClients);
    for (int i = 0; i < kNumClients; ++i)
        clients.emplace_back([&queue, i](std::stop_token st)
                             { run_client(queue, static_cast<uint64_t>(i), st); });

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

    return 0;
}
