#pragma once

#include "vuldir/core/Definitions.hpp"
#include "vuldir/core/Logger.hpp"
#include "vuldir/core/STL.hpp"
#include "vuldir/core/Types.hpp"
#include "vuldir/core/Uti.hpp"

namespace vd {

class BitIStream
{
public:
  BitIStream(Span<u8 const> bytes):
    m_bytes{bytes},
    m_cursor{bytes.begin()},
    m_buffer{0u},
    m_bufferSize{0u}
  {}

  [[nodiscard]] u32 Read(u32 count)
  {
    if(count > 32u)
      throw std::invalid_argument("BitIStream reads at most 32 bits");
    while(m_bufferSize < count) {
      if(m_cursor == m_bytes.end())
        throw std::runtime_error("Reached end of bit stream");

      u64 byte = *m_cursor;
      m_buffer |= byte << m_bufferSize;

      m_cursor++;
      m_bufferSize += 8u;
    }

    m_bufferSize -= count;
    const u64 mask = count == 32u ? 0xffffffffull
                                  : ((1ull << count) - 1u);
    u32 value = static_cast<u32>(m_buffer & mask);
    m_buffer >>= count;

    return value;
  }

  template<typename T>
  T Read(u32 count)
  {
    return static_cast<T>(Read(count));
  }

  [[nodiscard]] u32 Peek(u32 count)
  {
    if(count > 32u)
      throw std::invalid_argument("BitIStream peeks at most 32 bits");
    while(m_bufferSize < count) {
      if(m_cursor == m_bytes.end())
        throw std::runtime_error("Reached end of bit stream");

      u64 byte = *m_cursor;
      m_buffer |= byte << m_bufferSize;

      m_cursor++;
      m_bufferSize += 8u;
    }

    const u64 mask = count == 32u ? 0xffffffffull
                                  : ((1ull << count) - 1u);
    return static_cast<u32>(m_buffer & mask);
  }

  void Skip(u32 count) { [[maybe_unused]] auto v = Read(count); }

  void SkipToNextByte() { Skip(m_bufferSize % 8u); }

  bool HasMoreData() const
  {
    return m_bufferSize > 0u || m_cursor != m_bytes.end();
  }

private:
  Span<u8 const>           m_bytes;
  Span<u8 const>::iterator m_cursor;

  u64 m_buffer;
  u32 m_bufferSize;
};

class BitOStream
{
public:
  BitOStream(u64 sizeHint = 0u): m_bytes{}, m_bitSize{0u}
  {
    if(sizeHint > 0u) m_bytes.reserve(sizeHint);
  }

  void Write(BitIStream& src, u64 count)
  {
    while(count > 0u) {
      const u32 bits = static_cast<u32>(std::min<u64>(count, 32u));
      Write(src.Read(bits), bits);
      count -= bits;
    }
  }

  void Write(u32 v, u32 count)
  {
    if(count > 32u)
      throw std::invalid_argument("BitOStream writes at most 32 bits");
    for(u32 bit = 0u; bit < count; ++bit) {
      const u32 offset = static_cast<u32>(m_bitSize % 8u);
      if(offset == 0u) m_bytes.push_back(0u);
      if((v & (1u << bit)) != 0u)
        m_bytes.back() |= static_cast<u8>(1u << offset);
      ++m_bitSize;
    }
  }

  u64 size() const { return m_bitSize; }

  Arr<u8>& data() { return m_bytes; }

  const Arr<u8>& data() const { return m_bytes; }

private:
  Arr<u8> m_bytes;
  u64     m_bitSize;
};

class ByteIStream
{
public:
  ByteIStream(Span<u8 const> bytes):
    m_bytes{bytes}, m_cursor{0u}
  {}

  template<typename T>
  [[nodiscard]] T Read()
  {
    static_assert(std::is_trivially_copyable_v<T>);
    ensureAvailable(sizeof(T));
    T data{};
    std::memcpy(&data, getCursorPtr(), sizeof(T));
    m_cursor += sizeof(T);
    return data;
  }

  template<typename T>
  [[nodiscard]] T ReadSwap()
  {
    return vd::byteSwap(Read<T>());
  }

  template<typename T>
  T Peek() const
  {
    static_assert(std::is_trivially_copyable_v<T>);
    ensureAvailable(sizeof(T));
    T data{};
    std::memcpy(&data, getCursorPtr(), sizeof(T));
    return data;
  }

  template<typename T>
  T PeekSwap() const
  {
    return vd::byteSwap(Peek<T>());
  }

  template<typename T>
  [[nodiscard]] const T* ReadPtr()
  {
    ensureAvailable(sizeof(T));
    auto* data = getCursorData<T>();
    m_cursor += sizeof(T);
    return data;
  }

  template<typename T>
  [[nodiscard]] const T* PeekPtr() const
  {
    ensureAvailable(sizeof(T));
    return getCursorData<T>();
  }

  [[nodiscard]] Span<u8 const> ReadBytes(u64 size)
  {
    ensureAvailable(size);
    auto* data = getCursorPtr();
    SkipBytes(size);
    return {data, size};
  }

  [[nodiscard]] Span<u8 const> PeekBytes(u64 size) const
  {
    ensureAvailable(size);
    return {getCursorPtr(), size};
  }

  [[nodiscard]] Span<u8 const> ReadAllBytes()
  {
    auto* data = getCursorPtr();
    auto  size = GetSizeLeft();
    SkipBytes(size);
    return {data, size};
  }

  void SkipBytes(u64 size)
  {
    ensureAvailable(size);
    m_cursor += size;
  }

  u64 GetOffset() const { return m_cursor; }

  u64 GetSizeLeft() const { return m_bytes.size() - GetOffset(); }

  bool HasMoreData() const { return GetSizeLeft() > 0u; }

  u64 size() const { return m_bytes.size(); }

private:
  void ensureAvailable(u64 size) const
  {
    if(size > GetSizeLeft())
      throw std::out_of_range("ByteIStream: read past end of stream");
  }

  const u8* getCursorPtr() const
  {
    if(m_cursor == 0u) return m_bytes.data();
    return m_bytes.data() + m_cursor;
  }

  template<typename T>
  const T* getCursorData() const
  {
    const auto* data = getCursorPtr();
    if(reinterpret_cast<uintptr_t>(data) % alignof(T) != 0u)
      throw std::runtime_error("ByteIStream: unaligned pointer access");
    return reinterpret_cast<const T*>(data);
  }

private:
  Span<u8 const> m_bytes;
  u64            m_cursor;
};

class ByteOStream
{
public:
  ByteOStream(u64 sizeHint = 0u) { m_data.reserve(sizeHint); }

  void Write(u8 v) { m_data.push_back(v); }

  void Write(Span<u8 const> v)
  {
    m_data.insert(m_data.end(), v.begin(), v.end());
  }

  u8& operator[](u64 idx) { return m_data[idx]; }
  u8  operator[](u64 idx) const { return m_data[idx]; }

  Arr<u8>&       data() { return m_data; }
  const Arr<u8>& data() const { return m_data; }
  u64            size() const { return m_data.size(); }
  u8*            raw_data() { return m_data.data(); }
  const u8*      raw_data() const { return m_data.data(); }

private:
  Arr<u8> m_data;
};

struct MemoryStreambuf : std::streambuf {
  MemoryStreambuf(std::span<u8> buffer)
  {
    char* first = buffer.empty()
                    ? &m_empty
                    : reinterpret_cast<char*>(buffer.data());
    setg(first, first, first + buffer.size());
  }

  MemoryStreambuf(Span<u8 const> buffer)
  {
    char* first = buffer.empty()
                    ? &m_empty
                    : const_cast<char*>(
                        reinterpret_cast<const char*>(buffer.data()));
    setg(first, first, first + buffer.size());
  }

  pos_type seekoff(
    off_type off, std::ios_base::seekdir dir,
    std::ios_base::openmode) override
  {
    const off_type size = egptr() - eback();
    off_type       base = 0;
    if(dir == std::ios_base::cur)
      base = gptr() - eback();
    else if(dir == std::ios_base::end)
      base = size;
    else if(dir != std::ios_base::beg)
      return pos_type(off_type(-1));

    if(off < -base || off > size - base)
      return pos_type(off_type(-1));
    const off_type target = base + off;
    setg(eback(), eback() + target, egptr());
    return pos_type(target);
  }

  pos_type seekpos(pos_type sp, std::ios_base::openmode which) override
  {
    const off_type offset = sp - pos_type(off_type(0));
    return seekoff(offset, std::ios_base::beg, which);
  }

private:
  char m_empty = 0;
};

class IMemoryStream : public std::istream
{
public:
  IMemoryStream(Span<u8 const> buffer):
    std::istream(&m_buffer), m_buffer(buffer)
  {}

private:
  MemoryStreambuf m_buffer;
};
} // namespace vd
