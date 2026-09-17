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
  using U = std::make_unsigned_t<T>;
  if(index >= std::numeric_limits<U>::digits) return false;
  const U flag = U{1} << index;
  return (static_cast<U>(value) & flag) == flag;
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
  if(intValue == 0u)
    throw std::invalid_argument("Cannot find the index of an empty flag");
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
  if(off > 32u || size > 32u - off)
    throw std::invalid_argument("32-bit mask range is invalid");
  if(size == 0u) return 0u;
  const u32 mask = size == 32u ? MaxU32 : (1u << size) - 1u;
  return mask << off;
}

inline constexpr u32
bitSet32(const u32 buf, const u32 value, const u32 off, const u32 size)
{
  if(off > 32u || size > 32u - off)
    throw std::invalid_argument("32-bit field range is invalid");
  const u32 mask = size == 32u ? MaxU32
                               : size == 0u ? 0u : (1u << size) - 1u;
  return (buf & ~(mask << off)) | ((value & mask) << off);
}

inline constexpr u32
bitGet32(const u32 buf, const u32 off, const u32 size)
{
  if(off > 32u || size > 32u - off)
    throw std::invalid_argument("32-bit field range is invalid");
  const u32 mask = size == 32u ? MaxU32
                               : size == 0u ? 0u : (1u << size) - 1u;
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
  if(bitCount > 32u)
    throw std::invalid_argument("Cannot reverse more than 32 bits");
  u32 ret = 0u;
  for(u32 bitIdx = 0u; bitIdx < bitCount; ++bitIdx)
    ret |= ((v >> bitIdx) & 0x1u) << (bitCount - bitIdx - 1u);
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
  const int required =
    snprintf(nullptr, 0, format, std::forward<Args>(args)...);
  if(required < 0) throw std::runtime_error("String formatting failed");

  Str buf(static_cast<u64>(required), '\0');
  const int written = snprintf(
    buf.data(), buf.size() + 1u, format, std::forward<Args>(args)...);
  if(written != required)
    throw std::runtime_error("String formatting result changed between passes");

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
  if(!buffer || size == 0u || !format)
    throw std::invalid_argument("Formatting buffer is invalid");
  if(size > static_cast<u64>(std::numeric_limits<size_t>::max()))
    throw std::length_error("Formatting buffer is too large");
  const int result = snprintf(
    buffer, static_cast<size_t>(size), format,
    std::forward<Args>(args)...);
  if(result < 0)
    throw std::runtime_error("String formatting failed");
  return buffer;
}

#ifndef VD_OS_WINDOWS
inline u32 decodeUtf8Codepoint(Strv value, u64& offset)
{
  const auto first = static_cast<u8>(value[offset++]);
  if(first <= 0x7fu) return first;

  u32 codepoint = 0u;
  u32 trailing  = 0u;
  u32 minimum   = 0u;
  if((first & 0xe0u) == 0xc0u) {
    codepoint = first & 0x1fu;
    trailing  = 1u;
    minimum   = 0x80u;
  } else if((first & 0xf0u) == 0xe0u) {
    codepoint = first & 0x0fu;
    trailing  = 2u;
    minimum   = 0x800u;
  } else if((first & 0xf8u) == 0xf0u) {
    codepoint = first & 0x07u;
    trailing  = 3u;
    minimum   = 0x10000u;
  } else {
    throw std::runtime_error("Invalid UTF-8 string");
  }

  if(trailing > value.size() - offset)
    throw std::runtime_error("Truncated UTF-8 string");
  for(u32 idx = 0u; idx < trailing; ++idx) {
    const auto next = static_cast<u8>(value[offset++]);
    if((next & 0xc0u) != 0x80u)
      throw std::runtime_error("Invalid UTF-8 continuation byte");
    codepoint = (codepoint << 6u) | (next & 0x3fu);
  }
  if(
    codepoint < minimum || codepoint > 0x10ffffu ||
    (codepoint >= 0xd800u && codepoint <= 0xdfffu))
    throw std::runtime_error("Invalid UTF-8 codepoint");
  return codepoint;
}

inline void appendUtf8Codepoint(Str& value, u32 codepoint)
{
  if(
    codepoint > 0x10ffffu ||
    (codepoint >= 0xd800u && codepoint <= 0xdfffu))
    throw std::runtime_error("Invalid wide codepoint");
  if(codepoint <= 0x7fu) {
    value.push_back(static_cast<char>(codepoint));
  } else if(codepoint <= 0x7ffu) {
    value.push_back(static_cast<char>(0xc0u | (codepoint >> 6u)));
    value.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
  } else if(codepoint <= 0xffffu) {
    value.push_back(static_cast<char>(0xe0u | (codepoint >> 12u)));
    value.push_back(
      static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
    value.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
  } else {
    value.push_back(static_cast<char>(0xf0u | (codepoint >> 18u)));
    value.push_back(
      static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3fu)));
    value.push_back(
      static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
    value.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
  }
}
#endif

inline WStr widen(const Str& str)
{
  if(str.empty()) return {};

#ifdef VD_OS_WINDOWS
  if(str.size() > static_cast<u64>(std::numeric_limits<i32>::max()))
    throw std::length_error("UTF-8 string is too large to convert");
  const i32 inputSize = static_cast<i32>(str.size());
  const auto size = MultiByteToWideChar(
    CP_UTF8, MB_ERR_INVALID_CHARS, str.data(), inputSize, nullptr, 0);
  if(size <= 0) throw std::runtime_error("Invalid UTF-8 string");
  WStr ret(static_cast<u64>(size), L'\0');
  if(
    MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, str.data(), inputSize, ret.data(),
      size) != size)
    throw std::runtime_error("Failed to convert UTF-8 string");
  return ret;
#else
  static_assert(sizeof(wchar_t) >= sizeof(u32));
  WStr result;
  result.reserve(str.size());
  for(u64 offset = 0u; offset < str.size();)
    result.push_back(
      static_cast<wchar_t>(decodeUtf8Codepoint(str, offset)));
  return result;
#endif
}

inline Str narrow(const WStr& str)
{
  if(str.empty()) return {};
#ifdef VD_OS_WINDOWS
  if(str.size() > static_cast<u64>(std::numeric_limits<i32>::max()))
    throw std::length_error("Wide string is too large to convert");
  const i32 inputSize = static_cast<i32>(str.size());
  const auto size = WideCharToMultiByte(
    CP_UTF8, WC_ERR_INVALID_CHARS, str.data(), inputSize, nullptr, 0,
    nullptr, nullptr);
  if(size <= 0) throw std::runtime_error("Invalid wide string");
  Str ret(static_cast<u64>(size), '\0');
  if(
    WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, str.data(), inputSize, ret.data(),
      size, nullptr, nullptr) != size)
    throw std::runtime_error("Failed to convert wide string");
  return ret;
#else
  Str result;
  result.reserve(str.size());
  for(const wchar_t value: str)
    appendUtf8Codepoint(result, static_cast<u32>(value));
  return result;
#endif
}

inline void strCpy(char* dst, u64 size, const char* src)
{
  if(!dst || !src || size == 0u)
    throw std::invalid_argument("String copy arguments are invalid");
  const u64 length = std::strlen(src);
  if(length >= size)
    throw std::length_error("String does not fit the destination buffer");
  std::memcpy(dst, src, static_cast<size_t>(length + 1u));
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

  const auto end = stream.tellg();
  if(end < 0)
    throw std::runtime_error("Cannot determine file size");
  const u64 size = static_cast<u64>(end);
  if(
    size > static_cast<u64>(std::numeric_limits<size_t>::max()) ||
    size > static_cast<u64>(std::numeric_limits<std::streamsize>::max()))
    throw std::length_error("File is too large to read");

  Arr<u8> result;
  result.resize(size);

  stream.seekg(0, std::ios::beg);
  stream.read(
    reinterpret_cast<char*>(result.data()),
    static_cast<std::streamsize>(size));
  if(stream.gcount() != static_cast<std::streamsize>(size))
    throw std::runtime_error("File ended before its declared size");

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
inline T streamRead(std::istream& s)
{
  static_assert(std::is_trivially_copyable_v<T>);
  T v{};
  s.read(reinterpret_cast<char*>(&v), sizeof(T));
  if(s.gcount() != static_cast<std::streamsize>(sizeof(T)))
    throw std::runtime_error("Stream ended before the requested value");
  return v;
}

inline u64 streamSize(std::istream& s)
{
  const auto pos = s.tellg();
  if(pos < 0)
    throw std::runtime_error("Cannot determine stream position");
  s.seekg(0, std::ios::end);
  const auto end = s.tellg();
  if(end < 0) {
    s.clear();
    s.seekg(pos);
    throw std::runtime_error("Cannot determine stream size");
  }
  s.seekg(pos);
  return static_cast<u64>(end);
}

inline Arr<u8> streamReadBytes(std::istream& s, u64 size)
{
  if(size > static_cast<u64>(std::numeric_limits<std::streamsize>::max()))
    throw std::length_error("Stream read size is too large");
  Arr<u8> v(static_cast<size_t>(size));
  s.read(
    reinterpret_cast<char*>(v.data()),
    static_cast<std::streamsize>(size));
  if(s.gcount() != static_cast<std::streamsize>(size))
    throw std::runtime_error("Stream ended before the requested byte count");
  return v;
}

inline Arr<u8> streamReadBytes(std::istream& s)
{
  const auto position = s.tellg();
  if(position < 0)
    throw std::runtime_error("Cannot determine stream position");
  const u64 size = streamSize(s);
  const u64 offset = static_cast<u64>(position);
  if(offset > size)
    throw std::runtime_error("Stream position exceeds its size");
  return streamReadBytes(s, size - offset);
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
inline constexpr T alignUp(T val, T alignment)
{
  if(
    alignment == 0u ||
    (alignment & (alignment - static_cast<T>(1))) != 0u)
    throw std::invalid_argument("Alignment must be a non-zero power of two");
  if(val > std::numeric_limits<T>::max() - (alignment - 1u))
    throw std::overflow_error("Aligned value overflows");
  return (val + alignment - static_cast<T>(1)) &
         ~(alignment - static_cast<T>(1));
}

template<typename T>
inline constexpr T alignDown(T val, T alignment)
{
  if(
    alignment == 0u ||
    (alignment & (alignment - static_cast<T>(1))) != 0u)
    throw std::invalid_argument("Alignment must be a non-zero power of two");
  return val & ~(alignment - static_cast<T>(1));
}

template<typename T>
inline constexpr T divideRoundingUp(T a, T b)
{
  if(b == 0u) throw std::invalid_argument("Cannot divide by zero");
  return a / b + static_cast<T>(a % b != 0u);
}

inline Arr<u8> decodeBase64(Strv data)
{
  Arr<u8> out;
  out.reserve((data.size() * 3u) / 4u);

  u32  accumulator = 0u;
  u32  bitCount    = 0u;
  u32  symbols     = 0u;
  u32  padding     = 0u;
  bool sawPadding  = false;

  for(unsigned char c: data) {
    if(std::isspace(c)) continue;
    ++symbols;

    if(c == '=') {
      sawPadding = true;
      if(++padding > 2u)
        throw std::runtime_error("Base64: invalid padding");
      continue;
    }
    if(sawPadding)
      throw std::runtime_error("Base64: data after padding");

    u32 value = 0u;
    if(c >= 'A' && c <= 'Z') value = c - 'A';
    else if(c >= 'a' && c <= 'z')
      value = c - 'a' + 26u;
    else if(c >= '0' && c <= '9')
      value = c - '0' + 52u;
    else if(c == '+')
      value = 62u;
    else if(c == '/')
      value = 63u;
    else
      throw std::runtime_error("Base64: invalid character");

    accumulator = (accumulator << 6u) | value;
    bitCount += 6u;
    if(bitCount >= 8u) {
      bitCount -= 8u;
      out.push_back(toU8((accumulator >> bitCount) & 0xffu));
    }
  }

  if(
    symbols % 4u == 1u ||
    (padding != 0u && symbols % 4u != 0u) ||
    (padding == 1u && bitCount != 2u) ||
    (padding == 2u && bitCount != 4u) ||
    (bitCount != 0u &&
     (accumulator & ((1u << bitCount) - 1u)) != 0u))
    throw std::runtime_error("Base64: invalid length or padding");

  return out;
}

inline constexpr u64 getAlignmentDiff(u64 address, u64 alignment)
{
  if(alignment <= 1u) return 0u;
  const u64 remainder = address % alignment;
  return remainder == 0u ? 0u : alignment - remainder;
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
