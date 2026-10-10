// Nasdaq TotalView-ITCH 5.0 decoder for the subset of messages that move an order book.
//
// Spec: https://nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHSpecification.pdf
// All integers are big-endian. Prices are unsigned with 4 implied decimals.
// Every message starts with: type (1), stock locate (2), tracking number (2), timestamp (6, ns since midnight).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace lg::itch {

inline uint16_t be16(const uint8_t* p) { return uint16_t(uint16_t(p[0]) << 8 | p[1]); }
inline uint32_t be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}
inline uint64_t be48(const uint8_t* p) { return uint64_t(be16(p)) << 32 | be32(p + 2); }
inline uint64_t be64(const uint8_t* p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }

enum class MsgType : uint8_t {
  None = 0,
  SystemEvent = 'S',
  StockDirectory = 'R',
  AddOrder = 'A',
  AddOrderMpid = 'F',
  OrderExecuted = 'E',
  OrderExecutedWithPrice = 'C',
  OrderCancel = 'X',
  OrderDelete = 'D',
  OrderReplace = 'U',
};

// Fixed payload length per message type, or 0 for types this decoder does not handle.
constexpr std::size_t expected_length(uint8_t type) {
  switch (type) {
    case 'S': return 12;
    case 'R': return 39;
    case 'A': return 36;
    case 'F': return 40;
    case 'E': return 31;
    case 'C': return 36;
    case 'X': return 23;
    case 'D': return 19;
    case 'U': return 35;
    default: return 0;
  }
}

// One decoded message, flattened so the hot path never touches a variant or the heap.
struct Msg {
  MsgType type{MsgType::None};
  char side{0};   // 'B' or 'S' on adds
  char event{0};  // system event code
  uint16_t locate{0};
  uint64_t timestamp{0};
  uint64_t ref{0};      // order reference number (original ref on replace)
  uint64_t new_ref{0};  // replace only
  uint32_t shares{0};
  uint32_t price{0};
  std::array<char, 8> stock{};
};

enum class Status : uint8_t { Ok, Skipped, BadLength };

// Decodes one payload (without the BinaryFILE length prefix). The length is checked
// against the spec before any field is read, so a short frame can never read past its end.
inline Status decode(const uint8_t* p, std::size_t len, Msg& m) {
  if (len == 0) return Status::BadLength;
  const std::size_t need = expected_length(p[0]);
  if (need == 0) return Status::Skipped;
  if (len != need) return Status::BadLength;

  m.type = MsgType(p[0]);
  m.locate = be16(p + 1);
  m.timestamp = be48(p + 5);
  switch (p[0]) {
    case 'S':
      m.event = char(p[11]);
      break;
    case 'R':
      std::memcpy(m.stock.data(), p + 11, 8);
      break;
    case 'A':
    case 'F':
      m.ref = be64(p + 11);
      m.side = char(p[19]);
      m.shares = be32(p + 20);
      std::memcpy(m.stock.data(), p + 24, 8);
      m.price = be32(p + 32);
      break;
    case 'E':
    case 'X':
      m.ref = be64(p + 11);
      m.shares = be32(p + 19);
      break;
    case 'C':
      m.ref = be64(p + 11);
      m.shares = be32(p + 19);
      m.price = be32(p + 32);  // execution price; the resting order keeps its own price
      break;
    case 'D':
      m.ref = be64(p + 11);
      break;
    case 'U':
      m.ref = be64(p + 11);
      m.new_ref = be64(p + 19);
      m.shares = be32(p + 27);
      m.price = be32(p + 31);
      break;
  }
  return Status::Ok;
}

// Nasdaq BinaryFILE framing: every message is preceded by a 2-byte big-endian length.
class FrameReader {
 public:
  explicit FrameReader(std::span<const uint8_t> buf) : buf_(buf) {}

  bool next(const uint8_t*& payload, std::size_t& len) {
    if (pos_ + 2 > buf_.size()) {
      truncated_ = pos_ != buf_.size();
      return false;
    }
    len = be16(buf_.data() + pos_);
    if (pos_ + 2 + len > buf_.size()) {
      truncated_ = true;
      return false;
    }
    payload = buf_.data() + pos_ + 2;
    pos_ += 2 + len;
    return true;
  }

  bool truncated() const { return truncated_; }

 private:
  std::span<const uint8_t> buf_;
  std::size_t pos_ = 0;
  bool truncated_ = false;
};

}  // namespace lg::itch
