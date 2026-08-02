#pragma once

#include <cstdint>

#include "order.h"

// What kind of change happened to the book.
enum class MDType : uint8_t {
  Added,      // order is now resting
  Executed,   // some size traded off an order
  Cancelled,  // order is gone
};

// One market data event. Fixed size, no heap, safe to copy into a queue.
// One book means one ticker, so the ticker is implied by the feed.
struct MDMessage {
  // Client uses this to spot a gap. Every message gets the next number.
  uint64_t seq{0};

  MDType type{MDType::Added};

  uint64_t order_id{0};
  Side side{};
  int64_t price{0};

  // Added: the full resting size.
  // Executed: how much just traded.
  // Cancelled: ignored.
  uint64_t size{0};
};
