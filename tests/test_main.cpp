// Dependency-free tests, run by CTest. Each CHECK failure prints its location; any failure exits 1.
#include <cstdio>
#include <random>
#include <vector>

#include "lg/book.hpp"
#include "lg/gate.hpp"
#include "lg/itch.hpp"
#include "lg/itch_encode.hpp"
#include "lg/replay.hpp"
#include "lg/stats.hpp"
#include "lg/synth.hpp"

static int g_failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                     \
    }                                                                   \
  } while (0)

using namespace lg;
using itch::MsgType;
using itch::Status;

// Decodes every frame of an encoder's output.
static std::vector<itch::Msg> decode_all(const std::vector<uint8_t>& buf, std::vector<Status>* st = nullptr) {
  std::vector<itch::Msg> out;
  itch::FrameReader fr(buf);
  const uint8_t* p;
  std::size_t len;
  while (fr.next(p, len)) {
    itch::Msg m;
    const Status s = itch::decode(p, len, m);
    if (st) st->push_back(s);
    out.push_back(m);
  }
  return out;
}

static void test_decode_round_trip() {
  itch::Encoder e;
  e.system_event(0, 7, 'O');
  e.stock_directory(3, 8, "AAPL");
  e.add(3, 0x0000123456789ABCull, 42, 'B', 300, "AAPL", 1012345);
  e.add(3, 10, 43, 'S', 100, "AAPL", 1012400, true);
  e.executed(3, 11, 42, 100, 9001);
  e.executed_with_price(3, 12, 42, 100, 9002, 1012300);
  e.cancel(3, 13, 43, 50);
  e.remove(3, 14, 43);
  e.replace(3, 15, 42, 44, 200, 1012200);

  std::vector<Status> st;
  const auto m = decode_all(e.out, &st);
  CHECK(m.size() == 9);
  for (auto s : st) CHECK(s == Status::Ok);
  CHECK(m[0].type == MsgType::SystemEvent && m[0].event == 'O');
  CHECK(m[1].type == MsgType::StockDirectory && m[1].locate == 3 && m[1].stock[0] == 'A');
  CHECK(m[2].type == MsgType::AddOrder && m[2].ref == 42 && m[2].side == 'B' &&
        m[2].shares == 300 && m[2].price == 1012345 && m[2].timestamp == 0x0000123456789ABCull);
  CHECK(m[3].type == MsgType::AddOrderMpid && m[3].side == 'S' && m[3].price == 1012400);
  CHECK(m[4].type == MsgType::OrderExecuted && m[4].ref == 42 && m[4].shares == 100);
  CHECK(m[5].type == MsgType::OrderExecutedWithPrice && m[5].price == 1012300);
  CHECK(m[6].type == MsgType::OrderCancel && m[6].ref == 43 && m[6].shares == 50);
  CHECK(m[7].type == MsgType::OrderDelete && m[7].ref == 43);
  CHECK(m[8].type == MsgType::OrderReplace && m[8].ref == 42 && m[8].new_ref == 44 &&
        m[8].shares == 200 && m[8].price == 1012200);
}

static void test_malformed_input() {
  itch::Encoder e;
  e.raw({'A', 0, 1, 0, 0});              // add order, but far too short
  e.raw({'Z', 1, 2, 3});                 // a type this decoder does not handle
  std::vector<Status> st;
  decode_all(e.out, &st);
  CHECK(st.size() == 2);
  CHECK(st[0] == Status::BadLength);
  CHECK(st[1] == Status::Skipped);

  // Length prefix promises more bytes than the buffer holds.
  std::vector<uint8_t> cut = {0, 36, 'A', 0, 1};
  itch::FrameReader fr(cut);
  const uint8_t* p;
  std::size_t len;
  CHECK(!fr.next(p, len));
  CHECK(fr.truncated());
}

template <class Book>
static void scripted_book(Book& b) {
  auto msg = [](MsgType t, uint64_t ref, char side, uint32_t shares, uint32_t px, uint64_t nref = 0) {
    itch::Msg m;
    m.type = t; m.locate = 1; m.ref = ref; m.side = side; m.shares = shares; m.price = px; m.new_ref = nref;
    return m;
  };
  apply(b, msg(MsgType::AddOrder, 1, 'B', 100, 1000000), 1);
  apply(b, msg(MsgType::AddOrder, 2, 'B', 200, 1000100), 1);
  apply(b, msg(MsgType::AddOrder, 3, 'S', 300, 1000300), 1);
  apply(b, msg(MsgType::AddOrder, 4, 'S', 100, 1000200), 1);
  apply(b, msg(MsgType::AddOrder, 9, 'B', 999, 1000500), 2);  // other symbol: ignored
  CHECK((b.top() == Top{1000100, 200, 1000200, 100}));

  apply(b, msg(MsgType::OrderExecuted, 4, 0, 100, 0), 1);      // best ask fully filled
  CHECK((b.top() == Top{1000100, 200, 1000300, 300}));
  apply(b, msg(MsgType::OrderCancel, 2, 0, 50, 0), 1);         // partial cancel
  CHECK((b.top() == Top{1000100, 150, 1000300, 300}));
  apply(b, msg(MsgType::OrderDelete, 2, 0, 0, 0), 1);          // best bid gone, next level shows
  CHECK((b.top() == Top{1000000, 100, 1000300, 300}));
  apply(b, msg(MsgType::OrderReplace, 1, 0, 500, 1000200, 5), 1);
  CHECK((b.top() == Top{1000200, 500, 1000300, 300}));
  apply(b, msg(MsgType::OrderCancel, 5, 0, 9999, 0), 1);       // over-cancel is clamped
  CHECK(b.stats.over_fill == 1);
  CHECK((b.top() == Top{0, 0, 1000300, 300}));
  apply(b, msg(MsgType::OrderDelete, 77, 0, 0, 0), 1);         // unknown reference
  CHECK(b.stats.unknown_ref == 1);
  CHECK(b.stats.other_symbol == 1);
}

static void test_books_scripted() {
  BookMap m;
  scripted_book(m);
  const Window w;
  BookFlat f(w.lo, w.tick, w.levels);
  scripted_book(f);
  CHECK(m.levels() == f.levels());

  BookFlat tiny(1000000, 100, 10);
  CHECK(!tiny.add(1, 'B', 999900, 100));   // below window
  CHECK(!tiny.add(2, 'B', 1000050, 100));  // off tick
  CHECK(tiny.stats.out_of_window == 2);
}

static void test_differential_synthetic() {
  SynthConfig cfg;
  cfg.messages = 200000;
  cfg.seed = 7;
  const auto buf = generate_session(cfg);
  for (uint16_t loc = 1; loc <= cfg.symbols; ++loc) {
    const Window w;
    BookMap ref;
    BookFlat flat(w.lo, w.tick, w.levels);
    const ReplayReport r = replay_check(buf, loc, ref, flat);
    CHECK(r.top_mismatches == 0);
    CHECK(r.depth_match);
    CHECK(r.bad_length == 0 && r.skipped == 0 && !r.truncated);
    CHECK(ref.stats.unknown_ref == 0 && ref.stats.over_fill == 0 && ref.stats.duplicate_ref == 0);
    CHECK(flat.stats.out_of_window == 0);
    CHECK(ref.stats.applied > 0);
  }
}

static void test_percentile() {
  std::vector<int> v = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  CHECK(percentile_sorted(v, 0.5) == 5);
  CHECK(percentile_sorted(v, 0.99) == 10);
  CHECK(percentile_sorted(v, 0.1) == 1);
  CHECK(median({3.0, 1.0, 2.0}) == 2.0);
  CHECK(median({4.0, 1.0, 2.0, 3.0}) == 2.5);
}

// Simulated per-run p99 values: a level, multiplied by run-to-run noise.
static std::vector<double> runs(double level, double noise, int n, uint64_t seed) {
  std::mt19937_64 rng(seed);
  std::normal_distribution<double> d(1.0, noise);
  std::vector<double> v;
  for (int i = 0; i < n; ++i) v.push_back(level * d(rng));
  return v;
}

static void test_gate_verdicts() {
  // Identical code, 3% noise: must not fail.
  CHECK(judge(runs(200, 0.03, 8, 1), runs(200, 0.03, 8, 2), 0.05).verdict != Verdict::Regression);
  // Same, many seeds: count false alarms.
  int false_alarms = 0;
  for (uint64_t s = 0; s < 200; ++s)
    if (judge(runs(200, 0.03, 8, 100 + s), runs(200, 0.03, 8, 900 + s), 0.05).verdict == Verdict::Regression)
      ++false_alarms;
  CHECK(false_alarms <= 4);  // at most 2% of 200 trials
  // 25% slower tail: must fail.
  CHECK(judge(runs(200, 0.03, 8, 3), runs(250, 0.03, 8, 4), 0.05).verdict == Verdict::Regression);
  // 25% faster: reported as an improvement, never a failure.
  CHECK(judge(runs(200, 0.03, 8, 5), runs(150, 0.03, 8, 6), 0.05).verdict == Verdict::Improvement);
  // Too few runs to judge.
  CHECK(judge({1, 2}, {1, 2}, 0.05).verdict == Verdict::InsufficientData);
}

int main() {
  test_decode_round_trip();
  test_malformed_input();
  test_books_scripted();
  test_differential_synthetic();
  test_percentile();
  test_gate_verdicts();
  if (g_failures) {
    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("all tests passed\n");
  return 0;
}
