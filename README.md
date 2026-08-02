# cpp-trading-exchange
# cpp-trading-exchange

A trading exchange written from scratch in C++26. Built over eight weeks for the Build Fellowship.

The whole thing runs in one process. Client threads send orders, a single engine thread owns the book and matches them.

## What is in here

| File | What it does |
|---|---|
| `mem_pool.h` | Fixed-size memory pool. Grabs one block at startup so no order ever hits the heap. |
| `order.h` | One order: id, client, ticker, side, type, price, size. |
| `trade.h` | What comes out when two orders match. |
| `order_book.h` / `.cpp` | The book. Add, cancel, update, disconnect, TTL expiry, matching. |
| `lf_queue.h` | Lock-free queue. Many client threads push, one engine thread pops. No mutex. |
| `exchange_main.cpp` | Runs it. Spins up client threads, waits for Ctrl+C. |

## Build and run

You need Bazel 8.5.1. Version 9 does not work with googletest yet, which is why `.bazelversion` pins it.

```bash
git clone https://github.com/jjk30/cpp-trading-exchange.git
cd cpp-trading-exchange

bazel test //...           # run every test
bazel run //:exchange_main # run the exchange, Ctrl+C to stop
```

## Tests

83 tests, all passing.

| Component | Tests |
|---|---|
| MemPool | 19 |
| Order | 3 |
| OrderBook | 61 |

Coverage on `order_book.cpp`:

- Lines 96.60 percent (9 missed out of 265)
- Functions 100 percent (22 of 22)
- Branches 94.90 percent

The nine uncovered lines are defensive guards for states the class invariants already prevent, like a null return from a pool that cannot be full at that point. I left them in. Deleting a safety check to win a coverage point makes the code worse.

One thing to know if you are on a Mac: `bazel coverage` finishes green but writes `LF:0` for every file, so the report is empty. I got the real numbers by building the test with clang directly and running `llvm-cov` on the profile.

## How the book is put together

Three containers, because the book has two jobs that pull in different directions.

| Container | Job |
|---|---|
| `std::map<int64_t, PriceLevel>` for bids | best buy price in O(1) |
| `std::map<int64_t, PriceLevel>` for asks | best sell price in O(1) |
| `std::unordered_map<uint64_t, Location>` | cancel by order id in O(1) |

Both maps sort low to high, so best ask is `asks_.begin()` and best bid is `bids_.rbegin()`. Keeping them the same type matters, because a ternary cannot return two different types.

Orders at the same price wait in a `std::list`, first in first filled. List and not vector, because erasing from the middle of a list is O(1) and does not invalidate the iterators sitting either side of it. The index holds one of those iterators, so that guarantee is the whole reason cancel is fast.

## How the threading works

Clients do not lock the book. They push a request into `LFQueue` and go straight back to work. One engine thread pops and applies.

Two things come out of that. Client latency is just the enqueue, so a slow match cannot stall anybody else. And because the queue is FIFO by arrival, the order of execution is one fixed sequence instead of whatever the OS scheduler felt like. A shared mutex would give you neither.

The queue is a ring of slots, each with an atomic sequence number acting as a traffic light. A producer claims a slot by CAS on the tail counter, writes into it, then releases the light. The consumer only takes a slot whose light says its turn. Capacity is a power of two so the wraparound is a bitmask instead of a modulo.

## Known gaps

- No latency numbers yet. Nothing in this README says nanoseconds, and it should not until I have measured it.
- No second implementation to compare against. A tick indexed array version would be the obvious one, since it drops the per node allocation that `std::map` and `std::list` still do on every insert.
- The pool covers the `Order` objects only. The containers still allocate a node per insert. That is the next real thing to fix.

## Built with

C++26, Bazel, GoogleTest, macOS arm64.
