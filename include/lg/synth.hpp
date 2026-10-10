// Seeded synthetic ITCH 5.0 session. This is NOT real exchange traffic: it is a reproducible
// message mix (adds, partial cancels, deletes, executions, replaces) over a few symbols, with
// prices kept inside the default book window. It exists so the benchmark runs anywhere without
// downloading multi-gigabyte Nasdaq files.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "lg/itch_encode.hpp"

namespace lg {

struct SynthConfig {
  uint64_t seed = 42;
  uint64_t messages = 1'000'000;  // order messages, excluding the session header
  uint16_t symbols = 4;           // stock locates 1..symbols
  uint32_t tick = 100;            // one cent
  uint32_t mid_lo = 950000;       // mid price stays in [$95.00, $105.00]
  uint32_t mid_hi = 1050000;
};

struct SynthCounts {
  uint64_t add = 0, cancel = 0, remove = 0, execute = 0, replace = 0;
};

inline std::vector<uint8_t> generate_session(const SynthConfig& cfg, SynthCounts* counts = nullptr) {
  static const std::array<const char*, 8> names = {"AAPL", "MSFT", "NVDA", "AMZN",
                                                   "GOOG", "META", "TSLA", "AVGO"};
  struct Live {
    uint64_t ref;
    char side;
    uint32_t price;
    uint32_t shares;
  };

  std::mt19937_64 rng(cfg.seed);
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  itch::Encoder enc;
  enc.out.reserve(std::size_t(cfg.messages) * 32 + 4096);
  SynthCounts c;

  const uint16_t nsym = std::max<uint16_t>(1, std::min<uint16_t>(cfg.symbols, uint16_t(names.size())));
  std::vector<std::vector<Live>> live(nsym);
  std::vector<uint32_t> mid(nsym, (cfg.mid_lo + cfg.mid_hi) / 2 / cfg.tick * cfg.tick);
  uint64_t next_ref = 1, match = 1;
  uint64_t ts = 34'200ull * 1'000'000'000ull;  // 09:30:00 in ns since midnight

  enc.system_event(0, ts, 'O');  // start of messages
  for (uint16_t s = 0; s < nsym; ++s) enc.stock_directory(uint16_t(s + 1), ts, names[s]);
  enc.system_event(0, ts, 'Q');  // start of market hours

  auto offset_ticks = [&] {
    // Mostly near the touch, occasionally deeper: 0..30 ticks.
    const uint32_t a = uint32_t(rng() % 4), b = uint32_t(rng() % 4), d = uint32_t(rng() % 3);
    return std::min<uint32_t>(30, a + b * d);
  };
  auto quote = [&](uint16_t s, char side) {
    const uint32_t off = offset_ticks();
    return side == 'B' ? mid[s] - off * cfg.tick : mid[s] + (off + 1) * cfg.tick;
  };
  auto take = [&](std::vector<Live>& L, std::size_t i) {
    L[i] = L.back();
    L.pop_back();
  };

  // Invariant that keeps the book uncrossed: every bid is at or below mid, every ask above it.
  uint64_t n = 0;
  while (n < cfg.messages) {
    ts += 1 + rng() % 2000;
    const uint16_t s = uint16_t(rng() % nsym);
    const uint16_t loc = uint16_t(s + 1);
    auto& L = live[s];

    if (unit(rng) < 0.05) {
      if (rng() & 1) mid[s] = std::min(cfg.mid_hi, mid[s] + cfg.tick);
      else mid[s] = std::max(cfg.mid_lo, mid[s] - cfg.tick);
      // Resting orders the move has crossed are filled, as an aggressive order would fill them.
      for (std::size_t i = 0; i < L.size() && n < cfg.messages;) {
        const bool crossed = L[i].side == 'B' ? L[i].price > mid[s] : L[i].price <= mid[s];
        if (!crossed) {
          ++i;
          continue;
        }
        enc.executed(loc, ts, L[i].ref, L[i].shares, match++);
        take(L, i);
        ++c.execute;
        ++n;
      }
      if (n >= cfg.messages) break;
    }

    // Adds dominate while the book is thin; once it is deep, removals catch up so depth stays bounded.
    const double add_p = L.size() < 50 ? 1.0 : (L.size() < 800 ? 0.5 : 0.3);
    const double r = unit(rng);
    const double u = r < add_p ? 0.0 : 0.45 + 0.55 * (r - add_p) / (1.0 - add_p);
    ++n;
    if (u < 0.45) {
      const char side = (rng() & 1) ? 'B' : 'S';
      const uint32_t px = quote(s, side);
      const uint32_t shares = 100 * uint32_t(1 + rng() % 10);
      const uint64_t ref = next_ref++;
      enc.add(loc, ts, ref, side, shares, names[s], px, unit(rng) < 0.1);
      L.push_back({ref, side, px, shares});
      ++c.add;
    } else if (u < 0.65) {
      const std::size_t i = rng() % L.size();
      if (L[i].shares > 100) {
        const uint32_t lots = L[i].shares / 100;
        const uint32_t cut = 100 * uint32_t(1 + rng() % (lots - 1));
        enc.cancel(loc, ts, L[i].ref, cut);
        L[i].shares -= cut;
      } else {
        enc.cancel(loc, ts, L[i].ref, L[i].shares);
        take(L, i);
      }
      ++c.cancel;
    } else if (u < 0.80) {
      const std::size_t i = rng() % L.size();
      enc.remove(loc, ts, L[i].ref);
      take(L, i);
      ++c.remove;
    } else if (u < 0.92) {
      // Execute against the best-priced of three random orders, so fills lean toward the touch.
      std::size_t i = rng() % L.size();
      for (int k = 0; k < 2; ++k) {
        const std::size_t j = rng() % L.size();
        const bool better = L[j].side == 'B' ? L[j].price > L[i].price : L[j].price < L[i].price;
        if (L[j].side == L[i].side && better) i = j;
      }
      const uint32_t qty = std::min<uint32_t>(L[i].shares, 100 * uint32_t(1 + rng() % 3));
      if (unit(rng) < 0.1) enc.executed_with_price(loc, ts, L[i].ref, qty, match++, L[i].price);
      else enc.executed(loc, ts, L[i].ref, qty, match++);
      L[i].shares -= qty;
      if (L[i].shares == 0) take(L, i);
      ++c.execute;
    } else {
      const std::size_t i = rng() % L.size();
      const uint32_t px = quote(s, L[i].side);
      const uint32_t shares = 100 * uint32_t(1 + rng() % 10);
      const uint64_t ref = next_ref++;
      enc.replace(loc, ts, L[i].ref, ref, shares, px);
      L[i] = {ref, L[i].side, px, shares};
      ++c.replace;
    }
  }
  enc.system_event(0, ts + 1, 'C');  // end of messages
  if (counts) *counts = c;
  return std::move(enc.out);
}

}  // namespace lg
