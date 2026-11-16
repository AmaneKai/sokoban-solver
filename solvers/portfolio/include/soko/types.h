// Fixed-width integer aliases, solver constants, state layout, and aligned allocators.
#ifndef NATIVE_INCLUDE_SOKO_TYPES_H_
#define NATIVE_INCLUDE_SOKO_TYPES_H_

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

namespace soko {

inline constexpr int kMaxCells = 17424;
inline constexpr int kMaxBoxes = 32;
inline constexpr int kStatePoolSize = 10'000'000;
inline constexpr int kHashBits = 25;
inline constexpr int kHashSize = 1 << kHashBits;
inline constexpr int kHashMask = kHashSize - 1;
inline constexpr u16 kUnreach = 0xFFFF;
inline constexpr int kHeapArity = 4;
inline constexpr int kSoftUnreach = 1000;
inline constexpr int kDeadHeuristic = 100'000;

// Marks a box that has no assigned goal.
inline constexpr u8 kNoGoal = 0xFF;

// Bit layout of SokoState::push_dir. The low two bits hold the push direction.
inline constexpr u8 kDirMask = 0x03;
inline constexpr u8 kFlagDelivery = 0x40;
inline constexpr u8 kFlagRoomPlace = 0x80;

// One search state. The flexible array holds the sorted box cells, followed by
// one goal index per box. The header must stay at 24 bytes.
struct SokoState {
  u64 hash;
  u32 prev_idx;
  u16 g;
  u16 h;
  u16 last_dest;
  u16 switches;
  u16 normalized_player;
  u8 push_dir;
  u8 macro_steps;
  u16 boxes[];
};
static_assert(sizeof(SokoState) == 24, "SokoState header must stay at 24 bytes");

inline u32 Xorshift32(u32* state) noexcept {
  u32 x = *state;
  if (x == 0) x = 1;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

template <typename T>
std::unique_ptr<T[]> AllocUninitialized(size_t n) {
  return std::unique_ptr<T[]>(new T[n]);
}

struct FreeDeleter {
  void operator()(u32* p) const noexcept { std::free(p); }
};
using ZeroedU32Array = std::unique_ptr<u32[], FreeDeleter>;

inline ZeroedU32Array AllocZeroed(size_t n) {
  void* p = std::calloc(n, sizeof(u32));
  if (!p) throw std::bad_alloc();
  return ZeroedU32Array(static_cast<u32*>(p));
}

struct AlignedDeleter {
  void operator()(u8* p) const noexcept { ::operator delete[](p, std::align_val_t{64}); }
};
using AlignedByteBuffer = std::unique_ptr<u8[], AlignedDeleter>;

inline AlignedByteBuffer AllocAlignedUninitialized(size_t bytes) {
  return AlignedByteBuffer(static_cast<u8*>(::operator new[](bytes, std::align_val_t{64})));
}

}  // namespace soko

#endif  // NATIVE_INCLUDE_SOKO_TYPES_H_
