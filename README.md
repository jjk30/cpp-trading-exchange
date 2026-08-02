# cpp-trading-exchange

A trading exchange written from scratch in C++26, built over eight weeks for the Build Fellowship.

Client threads send orders. One engine thread owns the book and matches them. Nothing locks the book. Clients never read the book either — they rebuild their own copy from a market data feed.

## What is in here

| File | What it does |
|---|---|
| `mem_pool.h` | Fixed size memory pool. Grabs one block at startup so no order ever hits the heap. |
| `order.h` | One order: id, client, ticker, side, type, price, size. |
| `trade.h` | What comes out when two orders match. |
| `order_book.h` / `.cpp` | The book. Add, cancel, update, disconnect, TTL expiry, matching. |
| `lf_queue.h` | Lock free queue. Used both ways: many clients push orders in, and the engine pushes market data out. |
| `md_message.h` | One market data event. Added, executed, or cancelled, with a sequence number. |
| `md_book.h` | A client's own copy of the book, rebuilt from the feed alone. |
| `exchange_main.cpp` | Runs it. Client threads, engine thread, market data, waits for Ctrl+C. |
| `mem_pool_test.cpp`, `order_test.cpp`, `order_book_test.cpp`, `lf_queue_test.cpp`, `md_book_test.cpp` | The tests. |

## Run it

You need Bazel 8.5.1. Version 9 does not work with googletest yet, which is why `.bazelversion` pins it.

```bash
git clone https://github.com/jjk30/cpp-trading-exchange.git
cd cpp-trading-exchange

bazel test //...           # 5 test targets
bazel run //:exchange_main # Ctrl+C to stop
```

## Step 1: Stop the orders from touching the heap

**What I did.** Wrote `MemPool<T>`, a fixed size memory pool.

**How.** Ask the OS for one block of memory at startup and never ask again. Slots are handed out from that block. Free slots are tracked as a stack of indices, so taking one out or putting one back is a single push or pop. Objects are built in place with `std::construct_at` and torn down with `std::destroy_at`.

**What it got me.** Adding an order never calls `new`. No allocator, no lock inside malloc, no surprise pause in the middle of a match. Every slot is next to the last one, so the CPU cache actually helps.

19 tests, including a 1000 round stress loop.

Files: `mem_pool.h`, `mem_pool_test.cpp`

## Step 2: Build the order book

**What I did.** Wrote `Order` and `OrderBook` with add, cancel, and toString.

**How.** Three containers, because the book has two jobs that pull in different directions.

| Container | Job |
|---|---|
| `std::map` for bids | best buy price in O(1) |
| `std::map` for asks | best sell price in O(1) |
| `std::unordered_map` by order id | cancel by id in O(1) |

Orders at the same price wait in a `std::list`, first in first filled. The id index stores an iterator pointing straight at the order, so cancel is one jump instead of a scan.

**What it got me.** Both of the book's hot paths are constant time. List and not vector matters here: erasing from the middle of a list is O(1) and does not break the iterators either side of it, which is the only reason the stored iterator stays valid.

One bug worth remembering. Bids and asks started as different map types, one sorted backwards. A ternary cannot return two different types, so it would not compile. Fix was to sort both low to high and read bids from the back with `rbegin()`.

3 tests for Order, 8 for the book at this point.

Files: `order.h`, `order_book.h`, `order_test.cpp`, `order_book_test.cpp`

## Step 3: Make it behave like a real venue

**What I did.** Added everything a book needs beyond add and cancel, plus a matching engine.

**How.**

- `update` resizes a resting order. Smaller keeps its place in the queue. Larger loses it and goes to the back, which is what a real exchange does.
- `disconnect` pulls every order belonging to one client. Needed a second index from client id to that client's order ids.
- Orders carry a time to live and get purged when they expire.
- Self trade prevention stops a client matching against itself.
- The matching engine fills on price first, then time.

Then I split the class into `order_book.h` and `order_book.cpp`, and measured coverage.

**What it got me.** 83 tests passing, and coverage well past the 80 percent bar.

| | |
|---|---|
| Lines | 96.60 percent (9 missed of 265) |
| Functions | 100 percent (22 of 22) |
| Branches | 94.90 percent |

The nine uncovered lines are defensive guards for states the class invariants already prevent. I left them in. Deleting a safety check to win a coverage point makes the code worse.

If you are on a Mac, note that `bazel coverage` finishes green but writes `LF:0` for every file, so the report comes out empty. I got the real numbers by building the test with clang directly and running `llvm-cov` on the profile.

Files: `order_book.cpp`, `trade.h`

## Step 4: Let many clients in at once

**What I did.** Wrote a lock free queue and a `main` that runs the whole exchange.

**How.** The obvious design is a mutex on the book, with every client thread matching under it. That is worse than it sounds. Clients block behind each other, one slow order stalls everybody, and a mutex is not fair, so submission order gets lost.

So clients never touch the book. They push a request into `LFQueue` and go back to work. One engine thread pops and applies.

The queue is a ring of slots. Each slot carries an atomic sequence number that works like a traffic light. A producer claims a slot with a compare and swap on the tail counter, writes into it, then flips the light green. The consumer only takes a slot whose light says it is its turn. Capacity is a power of two, so wrapping around is a bitmask instead of a modulo.

`exchange_main.cpp` builds the engine, spins a thread per client, and waits for Ctrl+C. The signal handler does nothing but flip an atomic flag, because that is all a handler can safely do.

**What it got me.** Client latency is just the enqueue, so a slow match cannot stall anybody else. And because the queue is FIFO by arrival, execution happens in one fixed order instead of whatever the OS scheduler felt like. A mutex gives you neither.

Files: `lf_queue.h`, `lf_queue_test.cpp`, `exchange_main.cpp`

## Step 5: Let clients see the market

**What I did.** Made the exchange broadcast every change to the book, so a client can rebuild the book without ever reading it.

**How.** Three message types, which is all Nasdaq's ITCH really needs:

| Message | What the client does |
|---|---|
| Added | put the order in, at the back of that price's queue |
| Executed | shrink that order. If it hits zero, drop it |
| Cancelled | drop that order |

The book now reports what it changed. Every mutating method takes an optional `std::vector<MDMessage>*`, and pushes onto it as it works. Pass nothing and no messages are made, so every existing caller and test kept compiling untouched.

Order entry is one MPSC queue. Market data is one SPSC queue per client — same `LFQueue`, just one producer instead of many. Nobody shares a market data queue, so nobody contends for one.

Every message carries a sequence number. If a client sees a number it did not expect, it knows it missed something and its book is now a guess, not a copy.

**What it got me.** Four client threads, 77,406 requests, 52,154 trades, 111,842 market data messages. All four clients rebuilt an 8,235 order book byte for byte, with zero drops and zero gaps.

Three decisions worth defending:

- The engine uses `try_push`, never a blocking push. A slow subscriber loses a message and sees the gap in its sequence numbers. A matching engine that stalls because one client is slow is a broken exchange.
- `emit` does not burn a sequence number when nobody is listening. Burning one would leave a phantom hole and make subscribers think they missed something.
- Growing an order sends Cancelled then Added, not a modify message. The client does not need to know the queue priority rules — replaying those two lands the order exactly where the exchange put it.

20 tests, which drive the real book through partial fills, sweeps, resizes, expiries and disconnects, then assert the rebuilt book prints identically.

Files: `md_message.h`, `md_book.h`, `md_book_test.cpp`

## What is still missing

- No latency numbers. Nothing here claims nanoseconds, and it should not until I have measured it.
- No second implementation to compare against. A tick indexed array would be the obvious one.
- The pool only covers the `Order` objects. `std::map` and `std::list` still allocate a node on every insert, so three of the four allocations per add are still there. That is the next real thing to fix.
- No snapshot bootstrap. Every client here starts at sequence 1 and follows from the beginning. A client joining mid day would need to buffer the live feed, request a snapshot, and replay past it. Only the incremental half is built.

## Built with

C++26, Bazel, GoogleTest, macOS arm64.
