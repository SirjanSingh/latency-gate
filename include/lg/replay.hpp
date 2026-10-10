// Replays a BinaryFILE buffer through one or two books.
#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>

#include "lg/book.hpp"
#include "lg/itch.hpp"

namespace lg {

struct ReplayReport {
  uint64_t frames = 0;
  uint64_t decoded = 0;
  uint64_t skipped = 0;     // message types this decoder does not handle
  uint64_t bad_length = 0;  // frame length disagrees with the spec
  bool truncated = false;   // trailing bytes do not form a whole frame
  uint64_t top_mismatches = 0;
  bool depth_match = true;  // full depth equal at the end (check mode only)
  std::array<uint64_t, 256> by_type{};
  Top final_top;
};

// Runs the reference book and the flat book side by side and compares top of book after
// every applied event, then the full depth at the end.
inline ReplayReport replay_check(std::span<const uint8_t> buf, uint16_t locate,
                                 BookMap& ref, BookFlat& flat, int max_reports = 5) {
  ReplayReport r;
  itch::FrameReader fr(buf);
  const uint8_t* p;
  std::size_t len;
  itch::Msg m;
  while (fr.next(p, len)) {
    ++r.frames;
    switch (itch::decode(p, len, m)) {
      case itch::Status::Skipped: ++r.skipped; continue;
      case itch::Status::BadLength: ++r.bad_length; continue;
      case itch::Status::Ok: break;
    }
    ++r.decoded;
    ++r.by_type[uint8_t(m.type)];
    const bool a = apply(ref, m, locate);
    apply(flat, m, locate);
    if (a && !(ref.top() == flat.top())) {
      if (int(r.top_mismatches) < max_reports) {
        const Top x = ref.top(), y = flat.top();
        std::fprintf(stderr,
                     "mismatch at frame %llu (type %c ref %llu): map %u x %llu / %u x %llu, "
                     "flat %u x %llu / %u x %llu\n",
                     (unsigned long long)r.frames, char(m.type), (unsigned long long)m.ref,
                     x.bid_px, (unsigned long long)x.bid_qty, x.ask_px,
                     (unsigned long long)x.ask_qty, y.bid_px, (unsigned long long)y.bid_qty,
                     y.ask_px, (unsigned long long)y.ask_qty);
      }
      ++r.top_mismatches;
    }
  }
  r.truncated = fr.truncated();
  r.depth_match = ref.levels() == flat.levels();
  r.final_top = ref.top();
  return r;
}

}  // namespace lg
