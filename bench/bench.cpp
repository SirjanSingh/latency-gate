// lg-bench: per-message service latency of decode + book update + top-of-book read.
//   lg-bench FILE [--book flat|map] [--reps 1] [--run-id 1] [--cpu 2] [--locate 1]
//                 [--label head] [--csv results.csv]
//
// Method:
//   * The whole file is loaded and split into frames before timing. No I/O in the timed loop.
//   * The thread is pinned with sched_setaffinity (pinning limits migration; it is not isolation).
//   * One untimed warm-up pass on a throwaway book, then each rep runs on a fresh book.
//   * steady_clock is read immediately before decode and after the top-of-book read. Samples go
//     into vectors reserved up front, written after the second clock read.
//   * The cost of two back-to-back clock reads is measured and reported, not subtracted.
//   * A separate untimed pass measures throughput without per-message clock reads.
// One CSV row per (run, message type) plus an ALL row; raw samples are not written.
#include <sched.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "lg/book.hpp"
#include "lg/itch.hpp"
#include "lg/stats.hpp"
#include "lg/util.hpp"

namespace {

using clk = std::chrono::steady_clock;

constexpr std::array<char, 7> kTypes = {'A', 'F', 'E', 'C', 'X', 'D', 'U'};

int type_slot(lg::itch::MsgType t) {
  for (std::size_t i = 0; i < kTypes.size(); ++i)
    if (char(t) == kTypes[i]) return int(i);
  return -1;
}

struct Frame {
  const uint8_t* p;
  std::size_t len;
};

volatile uint64_t g_sink = 0;  // keeps the top-of-book read from being optimised away

bool pin(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  return sched_setaffinity(0, sizeof(set), &set) == 0;
}

uint64_t ns(clk::time_point a, clk::time_point b) {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
}

std::pair<uint64_t, uint64_t> timer_overhead() {
  std::vector<uint64_t> d(200000);
  for (auto& x : d) {
    const auto a = clk::now();
    const auto b = clk::now();
    x = ns(a, b);
  }
  std::sort(d.begin(), d.end());
  return {d.front(), lg::percentile_sorted(d, 0.5)};
}

template <class Book>
Book make_book(std::size_t orders) {
  if constexpr (std::is_same_v<Book, lg::BookFlat>) {
    const lg::Window w;
    return lg::BookFlat(w.lo, w.tick, w.levels, orders);
  } else {
    return lg::BookMap(orders);
  }
}

template <class Book>
void run_untimed(const std::vector<Frame>& frames, uint16_t locate, Book& book) {
  lg::itch::Msg m;
  for (const auto& f : frames) {
    if (lg::itch::decode(f.p, f.len, m) != lg::itch::Status::Ok) continue;
    if (lg::apply(book, m, locate)) {
      const lg::Top t = book.top();
      g_sink = g_sink + t.bid_px + t.ask_qty;
    }
  }
}

template <class Book>
int bench(const std::vector<Frame>& frames, const lg::Args& args, const std::string& book_name) {
  const uint16_t locate = uint16_t(args.num("--locate", 1));
  const int cpu = int(args.num("--cpu", 2));
  const uint64_t reps = std::max<uint64_t>(1, args.num("--reps", 1));
  const uint64_t run0 = args.num("--run-id", 1);
  const std::string label = args.get("--label", "run");
  const std::string csv = args.get("--csv");
  const std::size_t orders = 1 << 16;

  const bool pinned = pin(cpu);
  const auto [ovh_min, ovh_med] = timer_overhead();

  {  // warm-up: page in the input, train branch predictors, grow allocator pools
    Book warm = make_book<Book>(orders);
    run_untimed(frames, locate, warm);
  }

  double mps = 0;
  {
    Book b = make_book<Book>(orders);
    const auto a = clk::now();
    run_untimed(frames, locate, b);
    const auto z = clk::now();
    mps = double(frames.size()) / (double(ns(a, z)) / 1e9);
  }

  FILE* out = nullptr;
  if (!csv.empty()) {
    FILE* probe = std::fopen(csv.c_str(), "r");
    const bool fresh = probe == nullptr;
    if (probe) std::fclose(probe);
    out = std::fopen(csv.c_str(), "a");
    if (!out) {
      std::fprintf(stderr, "cannot open %s\n", csv.c_str());
      return 1;
    }
    if (fresh)
      std::fprintf(out,
                   "label,book,run,type,count,p50_ns,p90_ns,p99_ns,p999_ns,max_ns,mean_ns,"
                   "timer_overhead_ns,cpu,pinned,frames_per_sec\n");
  }

  std::fprintf(stderr, "%s book, cpu %d (%s), timer overhead min %llu ns / median %llu ns, "
                       "untimed throughput %.2f M frames/s\n",
               book_name.c_str(), cpu, pinned ? "pinned" : "NOT pinned",
               (unsigned long long)ovh_min, (unsigned long long)ovh_med, mps / 1e6);
  std::fprintf(stderr, "%-4s %-4s %10s %8s %8s %8s %8s %10s\n", "run", "type", "count", "p50",
               "p99", "p99.9", "max", "mean(ns)");

  for (uint64_t r = 0; r < reps; ++r) {
    std::array<std::vector<uint64_t>, kTypes.size()> samples;
    for (auto& s : samples) s.reserve(frames.size());

    Book book = make_book<Book>(orders);
    lg::itch::Msg m;
    for (const auto& f : frames) {
      const auto t0 = clk::now();
      if (lg::itch::decode(f.p, f.len, m) != lg::itch::Status::Ok) continue;
      if (!lg::apply(book, m, locate)) continue;
      const lg::Top t = book.top();
      const auto t1 = clk::now();
      g_sink = g_sink + t.bid_px + t.ask_qty;
      samples[std::size_t(type_slot(m.type))].push_back(ns(t0, t1));
    }

    std::vector<uint64_t> all;
    for (auto& s : samples) all.insert(all.end(), s.begin(), s.end());

    auto emit = [&](const char* type, std::vector<uint64_t>& v) {
      if (v.empty()) return;
      std::sort(v.begin(), v.end());
      double sum = 0;
      for (auto x : v) sum += double(x);
      const auto p50 = lg::percentile_sorted(v, 0.50), p90 = lg::percentile_sorted(v, 0.90),
                 p99 = lg::percentile_sorted(v, 0.99), p999 = lg::percentile_sorted(v, 0.999);
      const double mean = sum / double(v.size());
      std::fprintf(stderr, "%-4llu %-4s %10zu %8llu %8llu %8llu %8llu %10.1f\n",
                   (unsigned long long)(run0 + r), type, v.size(), (unsigned long long)p50,
                   (unsigned long long)p99, (unsigned long long)p999,
                   (unsigned long long)v.back(), mean);
      if (out)
        std::fprintf(out, "%s,%s,%llu,%s,%zu,%llu,%llu,%llu,%llu,%llu,%.2f,%llu,%d,%d,%.0f\n",
                     label.c_str(), book_name.c_str(), (unsigned long long)(run0 + r), type,
                     v.size(), (unsigned long long)p50, (unsigned long long)p90,
                     (unsigned long long)p99, (unsigned long long)p999,
                     (unsigned long long)v.back(), mean, (unsigned long long)ovh_med, cpu,
                     pinned ? 1 : 0, std::string(type) == "ALL" ? mps : 0.0);
    };
    for (std::size_t i = 0; i < kTypes.size(); ++i) {
      const char name[2] = {kTypes[i], 0};
      emit(name, samples[i]);
    }
    emit("ALL", all);
  }
  if (out) std::fclose(out);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  lg::Args args(argc, argv);
  const std::string path = args.positional();
  if (path.empty()) {
    std::fprintf(stderr,
                 "usage: lg-bench FILE [--book flat|map] [--reps N] [--run-id N] [--cpu N] "
                 "[--locate N] [--label NAME] [--csv FILE]\n");
    return 2;
  }
  const auto buf = lg::read_file(path);
  std::vector<Frame> frames;
  frames.reserve(buf.size() / 24);
  lg::itch::FrameReader fr(buf);
  const uint8_t* p;
  std::size_t len;
  while (fr.next(p, len)) frames.push_back({p, len});

  const std::string book = args.get("--book", "flat");
  if (book == "flat") return bench<lg::BookFlat>(frames, args, book);
  if (book == "map") return bench<lg::BookMap>(frames, args, book);
  std::fprintf(stderr, "unknown --book %s\n", book.c_str());
  return 2;
}
