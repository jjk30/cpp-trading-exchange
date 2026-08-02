# cpp-trading-exchange

A trading exchange written from scratch in C++26, built over eight weeks for the Build Fellowship.

Client threads send orders. One engine thread owns the book and matches them. Nothing locks the book. Clients never read the book either, they rebuild their own copy from a market data feed.

## What is in here

| File | What it does |
|---|---|
| `mem_pool.h` | Fixed size memory pool. One block at startup, so no order ever hits the heap. |
| `order.h` | One order: id, client, ticker, side, type, price, size. |
| `trade.h` | What comes out when two orders match. |
| `order_book.h` / `.cpp` | The book. Add, cancel, update, disconnect, TTL expiry, matching. |
| `lf_queue.h` | Lock free queue. Clients push orders in, the engine pushes market data out. |
| `md_message.h` | One market data event. Added, executed, or cancelled, with a sequence number. |
| `md_book.h` | A client's own copy of the book, rebuilt from the feed alone. |
| `exchange_main.cpp` | Runs it. Client threads, engine thread, market data feed. |
| `*_test.cpp` | The tests. Five targets. |

## Run it

Needs Bazel 8.5.1. Version 9 does not work with googletest yet, which is why `.bazelversion` pins it.

```bash
git clone https://github.com/jjk30/cpp-trading-exchange.git
cd cpp-trading-exchange

bazel test //...           # 5 test targets
bazel run //:exchange_main # Ctrl+C to stop
```

## Step 1: Keep orders off the heap

**What.** `MemPool<T>`, a fixed size memory pool.

**How.** Ask the OS for one block at startup and never ask again. Free slots are a stack of indices, so handing one out or taking it back is a single push or pop. Objects are built in place with `std::construct_at`, torn down with `std::destroy_at`.

**Got me.** Adding an order never calls `new`. No allocator, no lock inside malloc, no pause in the middle of a match. Slots sit next to each other, so the cache helps.

19 tests, including a 1000 round stress loop.

Files: `mem_pool.h`

## Step 2: Build the order book

**What.** `Order` and `OrderBook` with add, cancel, toString.

**How.** Three containers, because the book has two jobs that pull in different directions.

| Container | Job |
|---|---|
| `std::map` for bids | best buy price in O(1) |
| `std::map` for asks | best sell price in O(1) |
| `std::unordered_map` by order id | cancel by id in O(1) |

Orders at the same price wait in a `std::list`, first in first filled. The id index stores an iterator pointing straight at the order, so cancel is one jump, not a scan.

**Got me.** Both hot paths are constant time. List and not vector matters: erasing from the middle of a list is O(1) and does not invalidate the iterators either side of it, which is the only reason the stored iterator stays valid.

11 tests.

Files: `order.h`, `order_book.h`

## Step 3: Make it behave like a real venue

**What.** Everything a book needs beyond add and cancel, plus a matching engine.

**How.**

| Feature | Behaviour |
|---|---|
| `update` | Smaller keeps queue position. Larger goes to the back, same as a real exchange. |
| `disconnect` | Pulls every order for one client. Needed a second index, client id to order ids. |
| TTL | Orders carry an expiry and get purged. |
| Self trade prevention | A client never matches against itself. |
| Matching | Price first, then time. |

Then split the class into `order_book.h` and `order_book.cpp`, and measured coverage.

**Got me.** 83 tests and coverage well past the 80 percent bar.

| Metric | Value |
|---|---|
| Lines | 96.60 percent |
| Functions | 100 percent (22 of 22) |
| Branches | 94.90 percent |

The uncovered lines are defensive guards for states the class invariants already prevent. Deleting a safety check to win a coverage point makes the code worse.

Mac note: `bazel coverage` finishes green but writes `LF:0` for every file. Real numbers came from building with clang directly and running `llvm-cov` on the profile.

Files: `order_book.cpp`, `trade.h`

## Step 4: Let many clients in at once

**What.** A lock free queue and a `main` that runs the whole exchange.

**How.** The obvious design is a mutex on the book. Clients block behind each other, one slow order stalls everybody, and a mutex is not fair, so submission order is lost.

So clients never touch the book. They push a request into `LFQueue` and go back to work. One engine thread pops and applies.

The queue is a ring of slots. Each slot holds an atomic sequence number that acts like a traffic light. A producer claims a slot with a compare and swap on the tail counter, writes into it, then flips the light green. The consumer only takes a slot whose light says it is its turn. Capacity is a power of two, so wrapping is a bitmask, not a modulo.

**Got me.** Client latency is just the enqueue, so a slow match cannot stall anyone else. And the queue is FIFO by arrival, so execution order is fixed instead of whatever the scheduler felt like. A mutex gives you neither.

11 tests.

Files: `lf_queue.h`, `exchange_main.cpp`

## Step 5: Let clients see the market

**What.** The exchange broadcasts every change to the book, so a client can rebuild it without ever reading it.

**How.** Three message types, which is all Nasdaq's ITCH really needs.

| Message | What the client does |
|---|---|
| Added | Put the order at the back of that price's queue |
| Executed | Shrink that order. If it hits zero, drop it |
| Cancelled | Drop that order |

Every mutating method takes an optional `std::vector<MDMessage>*` and pushes onto it as it works. Pass nothing and no messages are made, so every existing caller and test kept compiling untouched.

Order entry is one MPSC queue. Market data is one SPSC queue per client, same `LFQueue`, one producer instead of many. Nobody shares a queue, so nobody contends for one.

Every message carries a sequence number. A client that sees an unexpected number knows its book is now a guess, not a copy.

**Got me.** A live run with four client threads.

| | |
|---|---|
| Requests | 77,406 |
| Trades | 52,154 |
| Market data messages | 111,842 |
| Book rebuilt | 8,235 orders, all four clients, exact match |
| Drops and gaps | 0 |

Three decisions worth defending:

- The engine uses `try_push`, never a blocking push. A slow subscriber loses a message and sees the gap in its sequence numbers. A matching engine that stalls because one client is slow is a broken exchange.
- `emit` does not burn a sequence number when nobody is listening. That would leave a phantom hole and make subscribers think they missed something.
- Growing an order sends Cancelled then Added, not a modify message. The client does not need to know the queue priority rules, replaying those two lands the order exactly where the exchange put it.

20 tests, driving the real book through partial fills, sweeps, resizes, expiries and disconnects, then asserting the rebuilt book prints identically.

Files: `md_message.h`, `md_book.h`

## Next

Latency benchmarking, a tick indexed book to compare against, and snapshot bootstrap so a client can join mid session.

## Built with

C++26, Bazel, GoogleTest, macOS arm64.
