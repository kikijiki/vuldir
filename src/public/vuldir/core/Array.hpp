#pragma once

#include "vuldir/core/Definitions.hpp"
#include "vuldir/core/STL.hpp"
#include "vuldir/core/Types.hpp"

namespace vd {

template<typename T, typename Allocator = std::allocator<T>>
class Arr
{
public:
  using value_type = T;

public:
  Arr() noexcept:
    m_data{}, m_size{0u}, m_capacity{0u}, m_allocator{Allocator()}
  {}

  Arr(Allocator& allocator) noexcept:
    m_data{}, m_size{0u}, m_capacity{0u}, m_allocator{allocator}
  {}

  Arr(const Arr& v): Arr()
  {
    ensureCapacity(v.size());
    for(u64 idx = 0u; idx < v.size(); ++idx) push_back(v[idx]);
  }

  Arr(const Arr& v, Allocator& allocator): Arr(allocator)
  {
    ensureCapacity(v.size());
    for(u64 idx = 0u; idx < v.size(); ++idx) push_back(v[idx]);
  }

  Arr(Arr&& v) noexcept: Arr() { swap(v); }

  ~Arr()
  {
    clear();

    m_allocator.deallocate(m_data, m_capacity);

    m_size     = 0u;
    m_capacity = 0u;
    m_data     = nullptr;
  }

  T&       operator[](const u64 idx) { return m_data[idx]; }
  const T& operator[](const u64 idx) const { return m_data[idx]; }

  T*       begin() noexcept { return m_data; }
  const T* begin() const noexcept { return m_data; }

  T*       end() noexcept { return m_data + m_size; }
  const T* end() const noexcept { return m_data + m_size; }

  T*       data() noexcept { return m_data; }
  const T* data() const noexcept { return m_data; }

  u64 size() const noexcept { return m_size; }
  u32 size32() const noexcept { return toU32(m_size); }

  void push_back(const T& v)
  {
    reserve(m_size + 1u);
    m_data[m_size] = v;
    m_size++;
  }

  void push_back(T&& v)
  {
    reserve(m_size + 1u);
    m_data[m_size] = std::move(v);
    m_size++;
  }

  template<class... Args>
  void emplace_back(Args&&... args)
  {
    reserve(m_size + 1u);
    new(m_data[m_size]) T(std::forward<Args>(args)...);
    m_size++;
  }

  void reserve(const u64 capacity)
  {
    if(capacity <= m_capacity) return;

    auto newCapacity = std::max(capacity, m_capacity * 2u);
    reallocate(newCapacity);
  }

  void clear()
  {
    for(u64 idx = 0u; idx < m_size; ++idx) m_data[idx].~T();

    m_size = 0u;
  }

private:
  void reallocate(const u64 newCapacity)
  {
    T* newData = m_allocator.allocate(newCapacity);

    for(u64 idx = 0u; idx < m_size; ++idx)
      newData[idx] = std::move(m_data[idx]);

    m_allocator.deallocate(m_data, m_capacity);

    m_capacity = newCapacity;
    m_data     = newData;
  }

  void swap(Arr& v)
  {
    using std::swap;
    swap(m_size, v.m_size);
    swap(m_capacity, v.m_capacity);
    swap(m_data, v.m_data);
    swap(m_allocator, v.m_allocator);
  }

private:
  u64       m_size;
  u64       m_capacity;
  T*        m_data;
  Allocator m_allocator;
};

template<typename T, u64 S>
class SArr
{
public:
  using value_type = T;

  T&       operator[](const u64 idx) noexcept { return m_data[idx]; }
  const T& operator[](const u64 idx) const noexcept
  {
    return m_data[idx];
  }

  T*       begin() noexcept { return m_data; }
  const T* begin() const noexcept { return m_data; }
  T*       end() noexcept { return m_data + S; }
  const T* end() const noexcept { return m_data + S; }

  T&       front() noexcept { return m_data[0]; }
  const T& front() const noexcept { return m_data[0]; }
  T&       back() noexcept { return m_data[S - 1]; }
  const T& back() const noexcept { return m_data[S - 1]; }

  u64      size() const noexcept { return S; }
  u32      size32() const noexcept { return toU32(S); }
  T*       data() noexcept { return m_data; }
  const T* data() const noexcept { return m_data; }

private:
  T m_data[S];
};

template<
  typename T, u64 Capacity, typename Allocator = std::allocator<T>>
class FArr
{
public:
  using value_type = T;

public:
  FArr() noexcept:
    m_data{m_storage},
    m_size{0u},
    m_capacity{Capacity},
    m_storage{},
    m_allocator{Allocator()}
  {}

  FArr(Allocator& allocator) noexcept:
    m_data{&m_storage},
    m_size{0u},
    m_capacity{Capacity},
    m_storage{},
    m_allocator{allocator}
  {}

  FArr(const FArr& v): FArr()
  {
    reserve(v.size());
    for(u64 idx = 0u; idx < v.size(); ++idx) push_back(v[idx]);
  }

  FArr(const FArr& v, Allocator& allocator): FArr(allocator)
  {
    reserve(v.size());
    for(u64 idx = 0u; idx < v.size(); ++idx) push_back(v[idx]);
  }

  FArr(const Arr<T>& v): FArr()
  {
    reserve(v.size());
    for(u64 idx = 0u; idx < v.size(); ++idx) push_back(v[idx]);
  }

  FArr(const Arr<T>& v, Allocator& allocator): FArr(allocator)
  {
    reserve(v.size());
    for(u64 idx = 0u; idx < v.size(); ++idx) push_back(v[idx]);
  }

  FArr(FArr&& v) noexcept: FArr() { swap(v); }

  ~FArr()
  {
    clear();

    if(isUsingHeap()) m_allocator.deallocate(m_data, m_capacity);

    m_size     = 0u;
    m_capacity = 0u;
    m_data     = nullptr;
  }

  FArr& operator=(const FArr& v)
  {
    if(this == &v) return *this;
    reserve(v.size());
    for(u64 idx = 0u; idx < v.size(); ++idx) push_back(v[idx]);
    return *this;
  }

  FArr& operator=(FArr&& v) noexcept
  {
    if(this == &v) return *this;
    clear();
    swap(v);
    return *this;
  }

  T&       operator[](const u64 idx) { return m_data[idx]; }
  const T& operator[](const u64 idx) const { return m_data[idx]; }

  T*       begin() noexcept { return m_data; }
  const T* begin() const noexcept { return m_data; }

  T*       end() noexcept { return m_data + m_size; }
  const T* end() const noexcept { return m_data + m_size; }

  T*       data() noexcept { return m_data; }
  const T* data() const noexcept { return m_data; }

  u64 size() const noexcept { return m_size; }
  u32 size32() const noexcept { return toU32(m_size); }

  void push_back(const T& v)
  {
    reserve(m_size + 1u);
    m_data[m_size] = v;
    m_size++;
  }

  void push_back(T&& v)
  {
    reserve(m_size + 1u);
    m_data[m_size] = std::move(v);
    m_size++;
  }

  template<class... Args>
  void emplace_back(Args&&... args)
  {
    reserve(m_size + 1u);
    new(m_data[m_size]) T(std::forward<Args>(args)...);
    m_size++;
  }

  void reserve(const u64 capacity)
  {
    if(capacity <= m_capacity) return;

    auto newCapacity = std::max(capacity, m_capacity * 2u);
    reallocate(newCapacity);
  }

  void clear()
  {
    for(u64 idx = 0u; idx < m_size; ++idx) m_data[idx].~T();
    m_size = 0u;
  }

private:
  void reallocate(const u64 newCapacity)
  {
    T* newData = m_allocator.allocate(newCapacity);

    for(u64 idx = 0u; idx < m_size; ++idx)
      newData[idx] = std::move(m_data[idx]);

    if(isUsingHeap()) m_allocator.deallocate(m_data, m_capacity);

    m_capacity = newCapacity;
    m_data     = newData;
  }

  bool isUsingHeap() const { return m_data != m_storage; }

  void swap(FArr& v)
  {
    auto thisIsHeap  = isUsingHeap();
    auto otherIsHeap = v.isUsingHeap();

    using std::swap;
    swap(m_size, v.m_size);
    swap(m_capacity, v.m_capacity);
    swap(m_data, v.m_data);
    swap(m_storage, v.m_storage);
    swap(m_allocator, v.m_allocator);

    if(!thisIsHeap) m_data = &m_storage;
    if(!otherIsHeap) v.m_data = &v.m_storage;
  }

private:
  u64 m_size;
  u64 m_capacity;
  T*  m_data;
  T   m_storage[Capacity];

  Allocator m_allocator;
};

template<typename T, typename A = std::allocator<T>>
using Arr8 = FArr<T, 8, A>;
template<typename T, typename A = std::allocator<T>>
using Arr16 = FArr<T, 16, A>;
template<typename T, typename A = std::allocator<T>>
using Arr32 = FArr<T, 32, A>;
template<typename T, typename A = std::allocator<T>>
using Arr64 = FArr<T, 64, A>;
template<typename T, typename A = std::allocator<T>>
using Arr128 = FArr<T, 128, A>;
template<typename T, typename A = std::allocator<T>>
using Arr256 = FArr<T, 256, A>;

template<typename T>
class Span
{
public:
  using data_type       = T;
  using iterator        = T*;
  using difference_type = u64;

public:
  Span(): m_data{}, m_size{} {}
  Span(const Span&) noexcept = default;
  ~Span() noexcept           = default;

  Span& operator=(const Span& s) noexcept = default;

  Span(T* data, u64 size): m_data{data}, m_size{size} {}
  Span(T* first, T* last): m_data{first}, m_size{last - first} {}

  template<u64 S>
  Span(const std::array<T, S>& v): m_data{v.data()}, m_size{v.size()}
  {}
  Span(const std::vector<T>& v): m_data{v.data()}, m_size{v.size()} {}

  template<u64 S>
  Span(const FArr<T, S>& v): m_data{v.data()}, m_size{v.size()}
  {}
  Span(const Arr<T>& v): m_data{v.data()}, m_size{v.size()} {}
  Span(const SArr<T>& v): m_data{v.data()}, m_size{v.size()} {}

  Span(const std::initializer_list<T>& v):
    m_data{v.data()}, m_size{v.size()}
  {}

public:
  T*       begin() { return m_data; }
  const T* begin() const { return m_data; }
  T*       end() { return m_data + m_size; }
  const T* end() const { return m_data + m_size; }

  T*       data() { return m_data; }
  const T* data() const { return m_data; }
  u64      size() const { return m_size; }
  u32      size32() const { return toU32(m_size); }

  bool empty() const { return m_size == 0u; }

  T& front() { return *m_data; }
  T& back() { return *m_data[m_size - 1u]; }
  T& operator[](u64 idx) { return *m_data[idx]; }

private:
  T*  m_data;
  u64 m_size;
};

} // namespace vd
