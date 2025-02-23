#pragma once

#include "vuldir/core/Definitions.hpp"
#include "vuldir/core/Platform.hpp"
#include "vuldir/core/STL.hpp"
#include "vuldir/core/Types.hpp"

#include <locale.h>
#include <stdlib.h>
#include <wchar.h>

namespace vd {

template<typename Cont>
inline constexpr u32 size32(const Cont& cont)
{
  return toU32(std::size(cont));
}

template<u64 Size>
struct StaticPool {
  using Pool = std::pmr::monotonic_buffer_resource;

  u8   buffer[Size] = {};
  Pool pool{std::data(buffer), std::size(buffer)};

  operator Pool*() { return &pool; }

  template<typename T>
  std::pmr::vector<T> NewVector()
  {
    return std::pmr::vector<T>(&pool);
  }
};
using SmallPool = StaticPool<1_KiB>;

template<typename T, size_t N>
inline constexpr size_t countOf(T (&)[N])
{
  return N;
}

template<typename T, size_t N>
inline constexpr u32 countOf32(T (&)[N])
{
  return toU32(N);
}

template<typename T>
inline constexpr T clamp(const T v, const T vmin, const T vmax)
{
  return std::max(std::min(v, vmax), vmin);
}

template<typename E>
inline constexpr typename std::underlying_type_t<E> enumValue(E e)
{
  return static_cast<typename std::underlying_type_t<E>>(e);
}

template<typename T1, typename T2>
inline constexpr bool hasFlag(const T1 value, const T2 flag)
{
  return static_cast<T2>(value & flag) == flag;
}

template<typename T>
inline constexpr bool hasBit(const T value, const u32 index)
{
  const auto flag = 1u << index;
  return (value & flag) == flag;
}

template<typename T>
inline constexpr bool hasAnyBit(const T value, const T bits)
{
  return (value & bits) != 0;
}

template<typename T>
inline constexpr bool hasFlag(
  const std::underlying_type_t<T> value,
  const std::underlying_type_t<T> flag)
{
  return (value & flag) == flag;
}

template<typename T>
inline constexpr u32 flagIndex(T value)
{
  auto intValue = static_cast<unsigned long>(value);
#ifdef _MSC_VER
  unsigned long index;
  _BitScanForward(&index, intValue);
  return toU32(index);
#else
  return toU32(__builtin_ffsl(intValue) - 1);
#endif
}

inline constexpr u32 bitMask32(const u32 off, const u32 size)
{
  return ~(0xffffffff << size) << off;
}

inline constexpr u32
bitSet32(const u32 buf, const u32 value, const u32 off, const u32 size)
{
  const u32 mask = ~(0xffffffff << size);
  return (buf & ~(mask << off)) | ((value & mask) << off);
}

inline constexpr u32
bitGet32(const u32 buf, const u32 off, const u32 size)
{
  const u32 mask = ~(0xffffffff << size);
  return (buf >> off) & mask;
}

template<typename T>
inline constexpr T
bitExtract32(const u32 buf, const u32 off, const u32 mask)
{
  return static_cast<T>((buf & mask) >> off);
}

inline constexpr u8 byteSwap(u8 v) { return v; }

inline constexpr u16 byteSwap(u16 v)
{
  return toU16(v >> 8) | toU16(v << 8);
}

inline constexpr u32 byteSwap(u32 v)
{
  return toU32(v >> 24) | toU32((v << 8) & 0x00FF0000) |
         toU32((v >> 8) & 0x0000FF00) | toU32(v << 24);
}

inline constexpr u64 byteSwap(u64 v)
{
  return toU64(v >> 56) | toU64((v << 40) & 0x00FF000000000000) |
         toU64((v << 24) & 0x0000FF0000000000) |
         toU64((v << 8) & 0x000000FF00000000) |
         toU64((v >> 8) & 0x00000000FF000000) |
         toU64((v >> 24) & 0x0000000000FF0000) |
         toU64((v >> 40) & 0x000000000000FF00) | toU64(v << 56);
}

inline constexpr u8 byteReverse(u8 v)
{
  return toU8((v * 0x0202020202ULL & 0x010884422010ULL) % 0x3ff);
}

inline constexpr u32 bitReverse(u32 v, u32 bitCount)
{
  u32 ret = 0u;
  for(u32 bitIdx = 0u; bitIdx <= (bitCount / 2u); ++bitIdx) {
    u32 inverted = bitCount - bitIdx - 1u;
    ret |= ((v >> bitIdx) & 0x1) << inverted;
    ret |= ((v >> inverted) & 0x1) << bitIdx;
  }
  return ret;
}

template<typename Derived, typename Base>
UPtr<Derived> static_unique_ptr_cast(UPtr<Base>&& p)
{
  auto d = static_cast<Derived*>(p.release());
  return std::unique_ptr<Derived>(d);
}

inline Str
strJoin(const std::initializer_list<Str>& strings, const Str& delimiter)
{
  Str  out{};
  auto it = strings.begin();
  if(it != strings.end()) {
    out += *it;
    ++it;
    for(; it != strings.end(); ++it) {
      out.append(delimiter).append(*it);
    }
  }

  return out;
}

inline Arr<const char*> toCharVector(const Arr<Str>& strVec)
{
  Arr<const char*> charVec;
  std::transform(
    strVec.begin(), strVec.end(), std::back_inserter(charVec),
    [](const Str& str) { return str.c_str(); });
  return charVec;
}

template<typename T>
inline Arr<const T*> toPtrVector(const Arr<UPtr<T>>& uptrVec)
{
  Arr<const T*> ptrVec;
  ptrVec.reserve(uptrVec.size());
  std::transform(
    uptrVec.begin(), uptrVec.end(), std::back_inserter(ptrVec),
    [](const UPtr<T>& uptr) { return uptr.get(); });
  return ptrVec;
}

template<typename... Args>
Str formatString(const char* format, Args&&... args)
{
  Str        buf;
  const auto size =
    toU64(snprintf(nullptr, 0, format, std::forward<Args>(args)...)) +
    1u;
  buf.resize(size);
  snprintf(
    const_cast<char*>(buf.data()), size, format,
    std::forward<Args>(args)...);

  return buf;
}

#ifdef VD_OS_WINDOWS
inline Str formatHRESULT(HRESULT hr)
{
  char* desc = nullptr;
  char  buffer[1024]{};

  FormatMessageA(
    FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS |
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_MAX_WIDTH_MASK,
    NULL, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (char*)&desc,
    1024, nullptr);

  if(desc) {
    sprintf_s(buffer, "[0x%08lx] %s", hr, desc);
    ::LocalFree(desc);
  } else {
    sprintf_s(buffer, "[0x%08lx] Unknown", hr);
  }

  return buffer;
}
#endif

template<typename... Args>
inline const char* formatBuffer(
  char* buffer, const u64 size, const char* format, Args&&... args)
{
  snprintf(buffer, size, format, args...);
  return buffer;
}

inline WStr widen(const Str& str)
{
  WStr ret(str.size(), L' ');

#ifdef VD_OS_WINDOWS
  const auto size = MultiByteToWideChar(
    CP_UTF8, 0, str.data(), toI32(str.size()), nullptr, 0);
  if(size > 0) {
    ret.resize(toU64(size) + 1u);
    MultiByteToWideChar(
      CP_UTF8, 0, str.data(), toI32(str.size()), &ret[0], size);
  }
#else
  ret.resize(mbstowcs(&ret[0], str.c_str(), ret.size()));
#endif

  return ret;
}

inline Str narrow(const WStr& str)
{
  Str ret(str.size(), ' ');
#ifdef VD_OS_WINDOWS
  const auto size = WideCharToMultiByte(
    CP_UTF8, 0, str.data(), toI32(str.size()), nullptr, 0, nullptr,
    nullptr);
  if(size > 0) {
    ret.resize(toU64(size) + 1u);
    WideCharToMultiByte(
      CP_UTF8, 0, str.data(), toI32(str.size()), &ret[0], size, nullptr,
      nullptr);
  }
#else
  ret.resize(wcstombs(&ret[0], str.c_str(), ret.size())); // TODO
#endif
  return ret;
}

inline void strCpy(char* dst, u64 size, const char* src)
{
#ifdef VD_OS_WINDOWS
  strcpy_s(dst, size, src);
#else
  VD_UNUSED(size);
  strcpy(dst, src);
#endif
}

template<typename Ex, typename... Args>
inline Ex makeError(const char* msg, Args&&... args)
{
  return Ex(formatString(msg, args...));
}

template<typename Ex, typename... Args>
inline Ex makeError(int code, const char* msg, Args&&... args)
{
  return Ex(formatString(msg, args...), code);
}

inline Arr<u8> getBytes(const fs::path& path)
{
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if(!stream) return {};

  auto size = toU64(stream.tellg());

  Arr<u8> result;
  result.resize(size);

  stream.seekg(0, std::ios::beg);
  stream.read(
    reinterpret_cast<char*>(result.data()),
    static_cast<std::streamsize>(size));

  return result;
}

template<typename T>
inline Span<u8 const> getBytes(const T& data)
{
  if constexpr(std::is_pointer_v<T>) return getBytes(*data);

  const u8* ptr      = nullptr;
  u64       byteSize = 0u;

  if constexpr(std::is_trivial_v<T>) {
    byteSize = sizeof(T);
    ptr      = reinterpret_cast<const u8*>(&data);
  } else { // Array, Vector
    byteSize = std::size(data) * sizeof(typename T::value_type);
    ptr      = reinterpret_cast<const u8*>(std::data(data));
  }

  return {ptr, byteSize};
}

inline constexpr u32 fourCC(const char* s)
{
  return (toU32(s[3]) << 0) | (toU32(s[2]) << 8) | (toU32(s[1]) << 16) |
         (toU32(s[0]) << 24);
}

template<typename T>
inline void streamRead(std::istream& s)
{
  T v;
  s.read(reinterpret_cast<char*>(&v), sizeof(T));
  return v;
}

inline u64 streamSize(std::istream& s)
{
  auto pos = s.tellg();
  s.seekg(0, std::ios::end);
  auto size = toU64(s.tellg());
  s.seekg(pos);
  return size;
}

inline Arr<u8> streamReadBytes(std::istream& s, u64 size)
{
  Arr<u8> v(size);
  s.read(
    reinterpret_cast<char*>(v.data()),
    static_cast<std::streamsize>(size));
  return v;
}

inline Arr<u8> streamReadBytes(std::istream& s)
{
  auto size = streamSize(s);

  Arr<u8> v(size);
  s.read(
    reinterpret_cast<char*>(v.data()),
    static_cast<std::streamsize>(size));
  return v;
}

inline Str u8strToStr(const std::u8string& u8str)
{
  return Str(u8str.begin(), u8str.end());
}

inline Str pathToStr(const fs::path& path)
{
  return u8strToStr(path.u8string());
}

template<typename T>
inline T alignUp(T val, T alignment)
{
  return (val + alignment - static_cast<T>(1)) &
         ~(alignment - static_cast<T>(1));
}

template<typename T>
inline T alignDown(T val, T alignment)
{
  return val & ~(alignment - static_cast<T>(1));
}

template<typename T>
inline T divideRoundingUp(T a, T b)
{
  return (a + b - static_cast<T>(1)) / b;
}

// TODO: there are much faster implementations.
inline Arr<u8> decodeBase64(Strv data)
{
  constexpr SArr<i8, 256> table = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 10
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 20
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 30
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 40
    0,  0,  0,  62, 0,  0,  0,  63, 52, 53, // 50
    54, 55, 56, 57, 58, 59, 60, 61, 0,  0,  // 60
    0,  0,  0,  0,  0,  0,  1,  2,  3,  4,  // 70
    5,  6,  7,  8,  9,  10, 11, 12, 13, 14, // 80
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, // 90
    25, 0,  0,  0,  0,  0,  0,  26, 27, 28, // 100
    29, 30, 31, 32, 33, 34, 35, 36, 37, 38, // 110
    39, 40, 41, 42, 43, 44, 45, 46, 47, 48, // 120
    49, 50, 51};

  Arr<u8> out;

  i8 v1 = 0;
  i8 v2 = -8;
  for(auto c: data) {
    v1 = toI8(v1 << 6) + table[toU64(c)];
    v2 += 6;
    if(v2 >= 0) {
      out.push_back(toU8((v1 >> v2) & 0xFF));
      v2 -= 8;
    }
  }

  return out;
}

inline constexpr u64 getAlignmentDiff(u64 address, u64 alignment)
{
  if(alignment == 0u || alignment == 1u) return address;

  const auto alignedAddress =
    (address + alignment - 1u) & ~(alignment - 1u);
  return alignedAddress - address;
}

template<typename T>
struct Transform {
  template<typename SRC, typename Func>
  static Arr<T> ToVec(const SRC& src, Func func)
  {
    Arr<T> result;
    result.reserve(src.size());
    std::transform(
      std::begin(src), std::end(src), std::back_inserter(result), func);
    return result;
  }
};

} // namespace vd
