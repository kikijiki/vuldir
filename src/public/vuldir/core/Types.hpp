//! \author Matteo Bernacchia <dev@kikijiki.com>
//! \date   2017/04/20

#pragma once

#include "vuldir/core/Definitions.hpp"

#include <cstdint>
#include <limits>

#undef min
#undef max

namespace vd {

using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using f32 = float;
using f64 = double;

#define VD_CAST(TYPE, NAME)       \
  template<typename T>            \
  inline constexpr TYPE NAME(T v) \
  {                               \
    return static_cast<TYPE>(v);  \
  }

VD_CAST(u8, toU8);
VD_CAST(u16, toU16);
VD_CAST(u32, toU32);
VD_CAST(u64, toU64);
VD_CAST(i8, toI8);
VD_CAST(i16, toI16);
VD_CAST(i32, toI32);
VD_CAST(i64, toI64);
VD_CAST(f32, toF32);
VD_CAST(f64, toF64);

#undef VD_CAST

static constexpr u32 MaxU32 = std::numeric_limits<u32>::max();
static constexpr u64 MaxU64 = std::numeric_limits<u64>::max();

constexpr u64 operator""_KiB(unsigned long long v)
{
  return 1024ull * v;
}

constexpr u64 operator""_KB(unsigned long long v)
{
  return 1000ull * v;
}

constexpr u64 operator""_MiB(unsigned long long v)
{
  return 1024ull * 1024ull * v;
}

constexpr u64 operator""_MB(unsigned long long v)
{
  return 1000ul * 1000ull * v;
}

constexpr u64 operator""_GiB(unsigned long long v)
{
  return 1024ull * 1024ull * 1024ull * v;
}

constexpr u64 operator""_GB(unsigned long long v)
{
  return 1000ul * 1000ull * 1000ull * v;
}

constexpr u64 operator""_TiB(unsigned long long v)
{
  return 1024ull * 1024ull * 1024ull * 1024ull * v;
}

constexpr u64 operator""_TB(unsigned long long v)
{
  return 1000ul * 1000ull * 1000ull * 1000ull * v;
}

} // namespace vd
