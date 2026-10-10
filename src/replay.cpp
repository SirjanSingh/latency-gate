// lg-replay: decode a BinaryFILE ITCH stream and rebuild one symbol's book.
//   lg-replay FILE [--locate 1] [--check]
// With --check, the std::map reference book and the flat book run side by side and must agree
// on top of book after every event and on full depth at the end. Exit code 1 on any disagreement.
#include <cstdio>

#include "lg/replay.hpp"
#include "lg/util.hpp"

int main(int argc, char** argv) {
  lg::Args args(argc, argv);
  const std::string path = args.positional();
  if (path.empty()) {
    std::fprintf(stderr, "usage: lg-replay FILE [--locate N] [--check]\n");
    return 2;
  }
  const uint16_t locate = uint16_t(args.num("--locate", 1));
  const lg::Window w;

  const auto buf = lg::read_file(path);
  lg::BookMap ref(1 << 16);
  lg::BookFlat flat(w.lo, w.tick, w.levels, 1 << 16);
  const lg::ReplayReport r = lg::replay_check(buf, locate, ref, flat);

  std::printf("frames %llu, decoded %llu, skipped %llu, bad length %llu, truncated %s\n",
              (unsigned long long)r.frames, (unsigned long long)r.decoded,
              (unsigned long long)r.skipped, (unsigned long long)r.bad_length,
              r.truncated ? "yes" : "no");
  std::printf("by type:");
  for (char t : {'S', 'R', 'A', 'F', 'E', 'C', 'X', 'D', 'U'})
    std::printf(" %c=%llu", t, (unsigned long long)r.by_type[uint8_t(t)]);
  std::printf("\nlocate %u: applied %llu, unknown ref %llu, over-fill %llu, duplicate %llu, "
              "out of window %llu, live orders %zu\n",
              locate, (unsigned long long)ref.stats.applied,
              (unsigned long long)ref.stats.unknown_ref, (unsigned long long)ref.stats.over_fill,
              (unsigned long long)ref.stats.duplicate_ref,
              (unsigned long long)flat.stats.out_of_window, ref.live_orders());
  std::printf("final top: bid %.4f x %llu, ask %.4f x %llu\n", r.final_top.bid_px / 1e4,
              (unsigned long long)r.final_top.bid_qty, r.final_top.ask_px / 1e4,
              (unsigned long long)r.final_top.ask_qty);

  if (args.flag("--check")) {
    const bool ok = r.top_mismatches == 0 && r.depth_match && flat.stats.out_of_window == 0;
    std::printf("check: top-of-book mismatches %llu, full depth %s -> %s\n",
                (unsigned long long)r.top_mismatches, r.depth_match ? "equal" : "DIFFERENT",
                ok ? "PASS" : "FAIL");
    if (!ok) return 1;
  }
  return (r.truncated || r.bad_length) ? 1 : 0;
}
