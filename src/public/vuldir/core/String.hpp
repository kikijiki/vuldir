#pragma once

#include "vuldir/core/Definitions.hpp"
#include "vuldir/core/Platform.hpp"
#include "vuldir/core/STL.hpp"
#include "vuldir/core/Types.hpp"
#include "vuldir/core/Uti.hpp"

#include <string>

namespace vd {

// template<typename Allocator = std::allocator<T>>
// class Str
//{
// public:
//  // TODO
// private:
//  u64       m_size;
//  u64       m_capacity;
//  char*     m_data;
//  Allocator m_allocator;
//};

template<u64 Size>
class FStr
{
public:
  FStr(): m_size{0u}, m_str{} {}
  FStr(const char* str):
    m_size{str ? std::min(Size, (u64)strlen(str) + 1u) : 0u}, m_str{}
  {
    if(str) StrCpy(m_str, sizeof(m_str), str);
  }
  FStr(FStr const&)            = default;
  FStr(FStr&&)                 = default;
  FStr& operator=(FStr const&) = default;
  FStr& operator=(FStr&&)      = default;
  ~FStr()                      = default;

  u64         size() const { return m_size; }
  u32         size32() const { return toU32(m_size); }
  const char* data() const { return m_str; }
  const char* c_str() const { return m_str; }

private:
  u64  m_size;
  char m_str[Size];
};

using Str8    = FStr<8>;
using Str16   = FStr<16>;
using Str32   = FStr<32>;
using Str64   = FStr<64>;
using Str128  = FStr<128>;
using Str256  = FStr<256>;
using Str512  = FStr<512>;
using Str1024 = FStr<1024>;

class StrRef
{
public:
  constexpr StrRef() = default;
  constexpr StrRef(const char* str): m_str(str) {}

  StrRef(const Str& str): m_str(str.c_str()) {}
  template<u64 Size>
  StrRef(const FStr<Size>& rhs): m_str(rhs.c_str())
  {}

  u64                   size() const { return toU64(strlen(m_str)); }
  u32                   size32() const { return toU32(strlen(m_str)); }
  constexpr const char* c_str() const { return m_str; }
  constexpr const char* data() const { return m_str; }

  constexpr bool empty() const { return !m_str || !m_str[0]; }

private:
  const char* m_str{};
};

} // namespace vd
