// Writes ITCH 5.0 messages in BinaryFILE framing. Used by the synthetic generator and the tests.
#pragma once

#include <cassert>
#include <cstdint>
#include <string_view>
#include <vector>

#include "lg/itch.hpp"

namespace lg::itch {

class Encoder {
 public:
  std::vector<uint8_t> out;

  void system_event(uint16_t locate, uint64_t ts, char code) {
    begin('S', locate, ts);
    put8(uint8_t(code));
    end();
  }

  void stock_directory(uint16_t locate, uint64_t ts, std::string_view symbol) {
    begin('R', locate, ts);
    put_symbol(symbol);
    put8('Q');       // market category
    put8('N');       // financial status
    put32(100);      // round lot size
    put8('N');       // round lots only
    put8('C');       // issue classification
    put8('Z');       // issue sub-type (2 bytes)
    put8(' ');
    put8('P');       // authenticity
    put8('N');       // short sale threshold
    put8(' ');       // IPO flag
    put8('1');       // LULD reference price tier
    put8('N');       // ETP flag
    put32(0);        // ETP leverage factor
    put8('N');       // inverse indicator
    end();
  }

  void add(uint16_t locate, uint64_t ts, uint64_t ref, char side, uint32_t shares,
           std::string_view symbol, uint32_t price, bool with_mpid = false) {
    begin(with_mpid ? 'F' : 'A', locate, ts);
    put64(ref);
    put8(uint8_t(side));
    put32(shares);
    put_symbol(symbol);
    put32(price);
    if (with_mpid) {
      put8('L'); put8('G'); put8('S'); put8('Y');  // attribution
    }
    end();
  }

  void executed(uint16_t locate, uint64_t ts, uint64_t ref, uint32_t shares, uint64_t match) {
    begin('E', locate, ts);
    put64(ref);
    put32(shares);
    put64(match);
    end();
  }

  void executed_with_price(uint16_t locate, uint64_t ts, uint64_t ref, uint32_t shares,
                           uint64_t match, uint32_t price) {
    begin('C', locate, ts);
    put64(ref);
    put32(shares);
    put64(match);
    put8('Y');  // printable
    put32(price);
    end();
  }

  void cancel(uint16_t locate, uint64_t ts, uint64_t ref, uint32_t shares) {
    begin('X', locate, ts);
    put64(ref);
    put32(shares);
    end();
  }

  void remove(uint16_t locate, uint64_t ts, uint64_t ref) {
    begin('D', locate, ts);
    put64(ref);
    end();
  }

  void replace(uint16_t locate, uint64_t ts, uint64_t ref, uint64_t new_ref, uint32_t shares,
               uint32_t price) {
    begin('U', locate, ts);
    put64(ref);
    put64(new_ref);
    put32(shares);
    put32(price);
    end();
  }

  // Raw frame, for tests that need malformed input.
  void raw(const std::vector<uint8_t>& payload) {
    put16(uint16_t(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
  }

 private:
  std::size_t start_ = 0;

  void begin(char type, uint16_t locate, uint64_t ts) {
    start_ = out.size();
    put16(0);  // length, patched in end()
    put8(uint8_t(type));
    put16(locate);
    put16(0);  // tracking number
    put16(uint16_t(ts >> 32));
    put32(uint32_t(ts));
  }

  void end() {
    const std::size_t len = out.size() - start_ - 2;
    assert(len == expected_length(out[start_ + 2]));
    out[start_] = uint8_t(len >> 8);
    out[start_ + 1] = uint8_t(len);
  }

  void put8(uint8_t v) { out.push_back(v); }
  void put16(uint16_t v) { put8(uint8_t(v >> 8)); put8(uint8_t(v)); }
  void put32(uint32_t v) { put16(uint16_t(v >> 16)); put16(uint16_t(v)); }
  void put64(uint64_t v) { put32(uint32_t(v >> 32)); put32(uint32_t(v)); }
  void put_symbol(std::string_view s) {
    for (std::size_t i = 0; i < 8; ++i) put8(i < s.size() ? uint8_t(s[i]) : uint8_t(' '));
  }
};

}  // namespace lg::itch
