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
    while(m_bufferSize < count) {
      if(m_cursor == m_bytes.end())
        throw std::runtime_error("Reached end of bit stream");

      u32 byte = *m_cursor;
      m_buffer |= byte << m_bufferSize;

      m_cursor++;
      m_bufferSize += 8u;
    }

    m_bufferSize -= count;
    u32 value = m_buffer & ((1u << count) - 1u);
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
    while(m_bufferSize < count) {
      if(m_cursor == m_bytes.end())
        throw std::runtime_error("Reached end of bit stream");

      u32 byte = *m_cursor;
      m_buffer |= byte << m_bufferSize;

      m_cursor++;
      m_bufferSize += 8u;
    }

    return m_buffer & ((1u << count) - 1u);
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

  u32 m_buffer;
  u32 m_bufferSize;
};

class BitOStream
{
public:
  BitOStream(u64 sizeHint = 0u): m_bytes{}, m_offset{0u}
  {
    if(sizeHint > 0u) m_bytes.reserve(sizeHint);
  }

  void Write(BitIStream& src, u64 count)
  {
    for(;;) {
      if(count >= 8u) {
        Write(src.Read(8u), 8u);
        count -= 8u;
      } else {
        u8 bitsLeft = toU8(count);
        Write(src.Read(bitsLeft), bitsLeft);
        break;
      }
    }
  }

  void Write(u32 v, u32 count)
  {
    u32 bitsDone = 0u;
    u32 bitsLeft = count;

    if(m_offset == 0u) m_bytes.push_back(0u);

    { // Write bits in the current byte.
      u32 bitsToByteEnd = 8u - m_offset;
      u32 writableBits  = std::min(count, bitsToByteEnd);

      if(writableBits > 0u) {
        u32 mask = (1 << writableBits) - 1;
        m_bytes.back() |= (v & mask) << m_offset;
        m_offset = (m_offset + writableBits) % 8u;
      }

      bitsDone += writableBits;
      bitsLeft -= writableBits;
    }

    // Whole bytes
    while((bitsDone + 8u) <= count) {
      u8 byte = toU8((v >> bitsDone) & 0xff);
      m_bytes.push_back(byte);
      bitsDone += 8u;
      bitsLeft -= 8u;
    }

    // Remaining bits.
    if(bitsDone < count) {
      u32 mask = (1 << bitsDone) - 1;
      u8  bits = toU8((v >> bitsDone) & mask);
      m_bytes.push_back(bits);
      m_offset = bitsLeft;
    }
  }

  u64 size() { return (m_bytes.size() * 8u) - (8u - m_offset); }

  Arr<u8>& data() { return m_bytes; }

  const Arr<u8>& data() const { return m_bytes; }

private:
  Arr<u8> m_bytes;
  u32     m_offset;
};

class ByteIStream
{
public:
  ByteIStream(Span<u8 const> bytes):
    m_bytes{bytes}, m_cursor{bytes.begin()}
  {}

  template<typename T>
  [[nodiscard]] T Read()
  {
    auto data = *getCursorData<T>();
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
    return *getCursorData<T>();
  }

  template<typename T>
  T PeekSwap() const
  {
    return vd::byteSwap(Peek<T>());
  }

  template<typename T>
  [[nodiscard]] const T* ReadPtr()
  {
    auto* data = getCursorData<T>();
    m_cursor += sizeof(T);
    return data;
  }

  template<typename T>
  [[nodiscard]] const T* PeekPtr() const
  {
    return getCursorData<T>();
  }

  [[nodiscard]] Span<u8 const> ReadBytes(u64 size)
  {
    auto* data = getCursorPtr();
    SkipBytes(size);
    return {data, size};
  }

  [[nodiscard]] Span<u8 const> PeekBytes(u64 size) const
  {
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
    m_cursor += static_cast<Span<u8 const>::difference_type>(size);
  }

  u64 GetOffset() const
  {
    return toU64(std::distance(m_bytes.begin(), m_cursor));
  }

  u64 GetSizeLeft() const { return m_bytes.size() - GetOffset(); }

  bool HasMoreData() const { return GetSizeLeft() > 0u; }

  u64 size() const { return m_bytes.size(); }

private:
  const u8* getCursorPtr() const { return &(*m_cursor); }

  template<typename T>
  const T* getCursorData() const
  {
    return reinterpret_cast<const T*>(getCursorPtr());
  }

private:
  Span<u8 const>           m_bytes;
  Span<u8 const>::iterator m_cursor;
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
    char* first = reinterpret_cast<char*>(buffer.data());
    setg(first, first, first + buffer.size());
  }

  MemoryStreambuf(Span<u8 const> buffer)
  {
    char* first =
      const_cast<char*>(reinterpret_cast<const char*>(buffer.data()));
    setg(first, first, first + buffer.size());
  }

  pos_type seekoff(
    off_type off, std::ios_base::seekdir dir,
    std::ios_base::openmode) override
  {
    if(dir == std::ios_base::cur) gbump(static_cast<int>(off));
    else if(dir == std::ios_base::end)
      setg(eback(), egptr() + off, egptr());
    else if(dir == std::ios_base::beg)
      setg(eback(), eback() + off, egptr());
    return gptr() - eback();
  }

  pos_type seekpos(pos_type sp, std::ios_base::openmode which) override
  {
    return seekoff(
      sp - pos_type(off_type(0)), std::ios_base::beg, which);
  }
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
