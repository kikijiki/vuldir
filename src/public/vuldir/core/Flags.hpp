#pragma once

#include "vuldir/core/Definitions.hpp"
#include "vuldir/core/STL.hpp"
#include "vuldir/core/Types.hpp"
#include "vuldir/core/Uti.hpp"

namespace vd {

#define VD_FLAGS(T) \
  inline Flags<T> operator|(T a, T b) { return {a, b}; }

template<
  typename FlagType,
  typename MaskType = std::underlying_type_t<FlagType>>
class Flags
{
public:
  template<typename... Ts>
  using AllFlags =
    typename std::conjunction<std::is_same<Ts, FlagType>...>::type;

public:
  class iterator
  {
  public:
    iterator(const Flags& flags, u64 index = 0u):
      m_flags{flags}, m_index{index}
    {
      while(m_index < m_flags.MaskSize() &&
            !m_flags.IsBitSet(m_index)) {
        ++m_index;
      }
    }

    iterator& operator++()
    {
      do {
        ++m_index;
      } while(m_index < m_flags.MaskSize() &&
              !m_flags.IsBitSet(m_index));
      return *this;
    }

    iterator operator++(int)
    {
      iterator retval = *this;
      ++(*this);
      return retval;
    }

    bool operator==(iterator other) const
    {
      return m_flags == other.m_flags && m_index == other.m_index;
    }
    bool operator!=(iterator other) const { return !(*this == other); }
    FlagType operator*() const
    {
      return static_cast<FlagType>(1 << m_index);
    }

    using difference_type   = u64;
    using value_type        = u64;
    using pointer           = const u64*;
    using reference         = const u64&;
    using iterator_category = std::forward_iterator_tag;

  private:
    const Flags& m_flags;
    u64          m_index;
  };

public:
  Flags() {}

  Flags(MaskType mask): m_mask{mask} {}

  template<typename... F>
  Flags(const F... f)
  {
    Set(std::forward<const F>(f)...);
  }

public:
  template<typename... F>
  Flags& Set(const F... f)
  {
    static_assert(
      AllFlags<F...>::value,
      "All parameters must be flags of the expected type");

    m_mask |= (static_cast<MaskType>(f) | ...);
    return *this;
  }

  template<typename... F>
  Flags& Unset(const F... f)
  {
    static_assert(
      AllFlags<F...>::value,
      "All parameters must be flags of the expected type");

    m_mask &= ~(static_cast<MaskType>(f) | ...);
    return *this;
  }

  bool IsBitSet(const u64 index) const
  {
    return (m_mask & (1 << index)) != 0;
  }
  bool IsSet(const FlagType flag) const
  {
    return (m_mask & static_cast<MaskType>(flag)) != 0;
  }

  u32 BitCount() const { return toU32(std::popcount((u64)m_mask)); }

  const MaskType  GetMask() const { return m_mask; }
  const MaskType* GetMaskPtr() const { return &m_mask; }

  operator const MaskType() const { return m_mask; }

  u64 MaskSize() const { return sizeof(MaskType) * 8u; }

  iterator begin() const { return iterator(*this, 0u); }
  iterator end() const { return iterator(*this, MaskSize()); }

  bool operator<(const Flags& other) const
  {
    return m_mask < other.m_mask;
  }

private:
  MaskType m_mask{};
};
} // namespace vd
