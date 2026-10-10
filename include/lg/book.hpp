// Two single-symbol order books with the same interface:
//   BookMap  - std::map price levels. Simple, obviously correct, used as the reference.
//   BookFlat - one array slot per tick inside a declared price window, best bid/ask cached.
// Both are driven by apply(), and the replay tool checks them against each other on every event.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "lg/itch.hpp"

namespace lg {

struct Top {
  uint32_t bid_px = 0;
  uint64_t bid_qty = 0;
  uint32_t ask_px = 0;
  uint64_t ask_qty = 0;
  bool operator==(const Top&) const = default;
};

// Every rule violation is counted rather than thrown, so a bad feed shows up in the stats.
struct BookStats {
  uint64_t applied = 0;
  uint64_t other_symbol = 0;
  uint64_t unknown_ref = 0;    // execute/cancel/delete/replace for an order we never saw
  uint64_t over_fill = 0;      // execute or cancel for more shares than remain (clamped)
  uint64_t duplicate_ref = 0;  // add with a reference number already live (ignored)
  uint64_t out_of_window = 0;  // BookFlat only: price outside the declared window (rejected)
};

using Level = std::tuple<char, uint32_t, uint64_t>;  // side, price, total shares

class BookMap {
 public:
  explicit BookMap(std::size_t expected_orders = 0) { orders_.reserve(expected_orders); }

  bool add(uint64_t ref, char side, uint32_t price, uint32_t shares) {
    auto [it, inserted] = orders_.try_emplace(ref, Order{price, shares, side});
    if (!inserted) {
      ++stats.duplicate_ref;
      return false;
    }
    if (side == 'B') bids_[price] += shares;
    else asks_[price] += shares;
    return true;
  }

  void reduce(uint64_t ref, uint32_t shares) {
    auto it = orders_.find(ref);
    if (it == orders_.end()) {
      ++stats.unknown_ref;
      return;
    }
    Order& o = it->second;
    if (shares > o.shares) {
      ++stats.over_fill;
      shares = o.shares;
    }
    o.shares -= shares;
    level_sub(o.side, o.price, shares);
    if (o.shares == 0) orders_.erase(it);
  }

  void remove(uint64_t ref) {
    auto it = orders_.find(ref);
    if (it == orders_.end()) {
      ++stats.unknown_ref;
      return;
    }
    level_sub(it->second.side, it->second.price, it->second.shares);
    orders_.erase(it);
  }

  // ITCH replace: the original order leaves the book, a new one with the same side enters.
  void replace(uint64_t ref, uint64_t new_ref, uint32_t price, uint32_t shares) {
    auto it = orders_.find(ref);
    if (it == orders_.end()) {
      ++stats.unknown_ref;
      return;
    }
    const char side = it->second.side;
    level_sub(side, it->second.price, it->second.shares);
    orders_.erase(it);
    add(new_ref, side, price, shares);
  }

  Top top() const {
    Top t;
    if (!bids_.empty()) {
      t.bid_px = bids_.begin()->first;
      t.bid_qty = bids_.begin()->second;
    }
    if (!asks_.empty()) {
      t.ask_px = asks_.begin()->first;
      t.ask_qty = asks_.begin()->second;
    }
    return t;
  }

  std::vector<Level> levels() const {
    std::vector<Level> v;
    for (auto& [px, q] : bids_) v.emplace_back('B', px, q);
    for (auto& [px, q] : asks_) v.emplace_back('S', px, q);
    return v;
  }

  std::size_t live_orders() const { return orders_.size(); }

  BookStats stats;

 private:
  struct Order {
    uint32_t price;
    uint32_t shares;
    char side;
  };

  void level_sub(char side, uint32_t price, uint64_t shares) {
    if (side == 'B') sub(bids_, price, shares);
    else sub(asks_, price, shares);
  }

  template <class M>
  static void sub(M& m, uint32_t price, uint64_t shares) {
    auto it = m.find(price);
    if (it == m.end()) return;
    it->second -= shares;
    if (it->second == 0) m.erase(it);
  }

  std::map<uint32_t, uint64_t, std::greater<>> bids_;  // best (highest) first
  std::map<uint32_t, uint64_t> asks_;                  // best (lowest) first
  std::unordered_map<uint64_t, Order> orders_;
};

class BookFlat {
 public:
  // Covers prices lo, lo + tick, ..., lo + (levels - 1) * tick. Anything else is rejected and counted.
  BookFlat(uint32_t lo, uint32_t tick, uint32_t levels, std::size_t expected_orders = 0)
      : lo_(lo), tick_(tick), n_(int32_t(levels)), bid_(levels, 0), ask_(levels, 0) {
    orders_.reserve(expected_orders);
  }

  bool add(uint64_t ref, char side, uint32_t price, uint32_t shares) {
    int32_t i;
    if (!index(price, i)) {
      ++stats.out_of_window;
      return false;
    }
    auto [it, inserted] = orders_.try_emplace(ref, Order{i, shares, side});
    if (!inserted) {
      ++stats.duplicate_ref;
      return false;
    }
    if (side == 'B') {
      bid_[i] += shares;
      if (i > best_bid_) best_bid_ = i;
    } else {
      ask_[i] += shares;
      if (best_ask_ < 0 || i < best_ask_) best_ask_ = i;
    }
    return true;
  }

  void reduce(uint64_t ref, uint32_t shares) {
    auto it = orders_.find(ref);
    if (it == orders_.end()) {
      ++stats.unknown_ref;
      return;
    }
    Order& o = it->second;
    if (shares > o.shares) {
      ++stats.over_fill;
      shares = o.shares;
    }
    o.shares -= shares;
    level_sub(o.side, o.idx, shares);
    if (o.shares == 0) orders_.erase(it);
  }

  void remove(uint64_t ref) {
    auto it = orders_.find(ref);
    if (it == orders_.end()) {
      ++stats.unknown_ref;
      return;
    }
    level_sub(it->second.side, it->second.idx, it->second.shares);
    orders_.erase(it);
  }

  void replace(uint64_t ref, uint64_t new_ref, uint32_t price, uint32_t shares) {
    auto it = orders_.find(ref);
    if (it == orders_.end()) {
      ++stats.unknown_ref;
      return;
    }
    const char side = it->second.side;
    level_sub(side, it->second.idx, it->second.shares);
    orders_.erase(it);
    add(new_ref, side, price, shares);
  }

  Top top() const {
    Top t;
    if (best_bid_ >= 0) {
      t.bid_px = price_of(best_bid_);
      t.bid_qty = bid_[best_bid_];
    }
    if (best_ask_ >= 0) {
      t.ask_px = price_of(best_ask_);
      t.ask_qty = ask_[best_ask_];
    }
    return t;
  }

  std::vector<Level> levels() const {
    std::vector<Level> v;
    for (int32_t i = n_ - 1; i >= 0; --i)
      if (bid_[i]) v.emplace_back('B', price_of(i), bid_[i]);
    for (int32_t i = 0; i < n_; ++i)
      if (ask_[i]) v.emplace_back('S', price_of(i), ask_[i]);
    return v;
  }

  std::size_t live_orders() const { return orders_.size(); }

  BookStats stats;

 private:
  struct Order {
    int32_t idx;
    uint32_t shares;
    char side;
  };

  bool index(uint32_t price, int32_t& i) const {
    if (price < lo_) return false;
    const uint32_t off = price - lo_;
    if (off % tick_ != 0) return false;
    const uint32_t k = off / tick_;
    if (k >= uint32_t(n_)) return false;
    i = int32_t(k);
    return true;
  }

  uint32_t price_of(int32_t i) const { return lo_ + uint32_t(i) * tick_; }

  // When the best level empties, walk to the next non-empty one. The walk is bounded by the window.
  void level_sub(char side, int32_t i, uint64_t shares) {
    if (side == 'B') {
      bid_[i] -= shares;
      if (i == best_bid_)
        while (best_bid_ >= 0 && bid_[best_bid_] == 0) --best_bid_;
    } else {
      ask_[i] -= shares;
      if (i == best_ask_) {
        while (best_ask_ < n_ && ask_[best_ask_] == 0) ++best_ask_;
        if (best_ask_ == n_) best_ask_ = -1;
      }
    }
  }

  uint32_t lo_;
  uint32_t tick_;
  int32_t n_;
  std::vector<uint64_t> bid_;
  std::vector<uint64_t> ask_;
  int32_t best_bid_ = -1;
  int32_t best_ask_ = -1;
  std::unordered_map<uint64_t, Order> orders_;
};

// Applies one decoded message to a book that follows a single stock locate.
template <class Book>
inline bool apply(Book& book, const itch::Msg& m, uint16_t locate) {
  using itch::MsgType;
  switch (m.type) {
    case MsgType::AddOrder:
    case MsgType::AddOrderMpid:
    case MsgType::OrderExecuted:
    case MsgType::OrderExecutedWithPrice:
    case MsgType::OrderCancel:
    case MsgType::OrderDelete:
    case MsgType::OrderReplace:
      break;
    default:
      return false;
  }
  if (m.locate != locate) {
    ++book.stats.other_symbol;
    return false;
  }
  ++book.stats.applied;
  switch (m.type) {
    case MsgType::AddOrder:
    case MsgType::AddOrderMpid:
      book.add(m.ref, m.side, m.price, m.shares);
      break;
    case MsgType::OrderExecuted:
    case MsgType::OrderExecutedWithPrice:
    case MsgType::OrderCancel:
      book.reduce(m.ref, m.shares);
      break;
    case MsgType::OrderDelete:
      book.remove(m.ref);
      break;
    case MsgType::OrderReplace:
      book.replace(m.ref, m.new_ref, m.price, m.shares);
      break;
    default:
      break;
  }
  return true;
}

// Default window used by the tools: $80.00 to $119.99 in one-cent ticks.
// ITCH prices carry 4 implied decimals, so one cent is 100 price units.
struct Window {
  uint32_t lo = 800000;
  uint32_t tick = 100;
  uint32_t levels = 4000;
};

}  // namespace lg
