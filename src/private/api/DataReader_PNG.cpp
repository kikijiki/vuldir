#include "vuldir/DataReader.hpp"

using namespace vd;

static constexpr u8 PngSignature[] = {137, 80, 78, 71, 13, 10, 26, 10};

static constexpr u8 DeflateHCLENMap[] = {
  16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

static constexpr u8 DeflateExtraLengthBits[] = {
  0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
  2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

static constexpr u32 DeflateExtraLengthValue[] = {
  3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
  31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};

static constexpr u8 DeflateExtraDistanceBits[] = {
  0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
  6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
static constexpr u32 DeflateExtraDistanceValue[] = {
  1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
  33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
  1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};

class HuffmanTable
{
public:
  HuffmanTable(u32 maxLength): m_entries{}, m_maxLength{maxLength}
  {
    if(maxLength >= MaxBits)
      throw std::runtime_error("Huffman table: size is too big");

    u64 entryCount = 1ull << maxLength;
    m_entries.resize(entryCount);
  }

  HuffmanTable(u32 maxLength, Span<u8 const> symbolLengths):
    HuffmanTable(maxLength)
  {
    Initialize(symbolLengths);
  }

  void Initialize(Span<u8 const> symbolLengths)
  {
    u32 lengthCounts[MaxBits] = {};
    for(u32 symbolIdx = 0u; symbolIdx < std::size(symbolLengths);
        ++symbolIdx) {
      if(symbolLengths[symbolIdx] >= std::size(lengthCounts))
        throw std::runtime_error(
          "Huffman table: invalid symbol lengths");
      ++lengthCounts[symbolLengths[symbolIdx]];
    }
    lengthCounts[0] = 0u;

    u32 nextCode[MaxBits] = {};
    for(u32 idx = 1u; idx < MaxBits; ++idx) {
      nextCode[idx] = (nextCode[idx - 1u] + lengthCounts[idx - 1u])
                      << 1;
    }

    for(u32 symbolIdx = 0u; symbolIdx < std::size(symbolLengths);
        ++symbolIdx) {
      auto length = symbolLengths[symbolIdx];
      if(length == 0u) continue;

      if(length >= MaxBits)
        throw std::runtime_error(
          "Huffman table: invalid symbol lengths");

      auto code = nextCode[length];
      ++nextCode[length];

      u32 padBits  = m_maxLength - length;
      u32 padCount = 1u << padBits;
      for(u32 pad = 0u; pad < padCount; ++pad) {
        u32 reverseIdx = (code << padBits) | pad;
        u32 entryIdx   = bitReverse(reverseIdx, m_maxLength);

        m_entries[entryIdx].symbol = static_cast<u16>(symbolIdx);
        m_entries[entryIdx].bits   = length;
      }
    }
  }

  u32 Decode(BitIStream& src)
  {
    auto idx = src.Peek(m_maxLength);
    if(idx >= m_entries.size())
      throw std::runtime_error("Huffman table: symbol out of range");

    const auto& entry = m_entries[idx];

    if(entry.bits == 0u)
      throw std::runtime_error("Huffman table: symbol has zero length");

    src.Skip(entry.bits);
    return entry.symbol;
  }

  template<typename T>
  T Decode(BitIStream& src)
  {
    return static_cast<T>(Decode(src));
  }

private:
  static constexpr u32 MaxBits = 16u;

  struct HuffmanEntry {
    u16 symbol;
    u8  bits;
  };

private:
  std::vector<HuffmanEntry> m_entries;
  u32                       m_maxLength;
};

static Arr<u8> ZLibDeflate(Span<u8 const> inData, u64 sizeHint = 0u)
{
  ByteIStream inBytes(inData);

  u8 CMF = inBytes.Read<u8>();
  u8 FLG = inBytes.Read<u8>();

  u8 compressionMethod = CMF & 0xf;
  // u8 compressionInfo   = CMF >> 4;
  // u8 checkBits         = FLG & 0x1f;
  u8 presetDictionary = (FLG >> 5) & 0x1;
  // u8 compressionLevel  = FLG >> 6;

  if(compressionMethod != 8u || presetDictionary != 0u)
    throw std::runtime_error("Deflate: unsopported settings");

  BitIStream  inBits(inBytes.ReadAllBytes());
  ByteOStream outBytes(sizeHint);

  // Deflate
  for(;;) {
    u32 BFINAL = inBits.Read(1);
    u32 BTYPE  = inBits.Read(2);

    HuffmanTable literalLengthTable(15u);
    HuffmanTable distanceTable(15u);

    if(BTYPE == 0u) // Literal
    {
      inBits.SkipToNextByte();
      u32 LEN  = inBits.Read(16);
      u32 NLEN = inBits.Read(16);

      if(LEN != ~NLEN)
        throw std::runtime_error("Deflate: bad block length");

      for(u32 idx = 0u; idx < LEN; ++idx)
        outBytes.Write(static_cast<u8>(inBits.Read(8u)));
    } else {
      std::array<u8, 512u> huffmanBuffer = {};
      std::span<u8>        lengthData;
      std::span<u8>        distanceData;

      if(BTYPE == 1u) { // Default table
        for(u32 idx = 0u; idx < 144u; ++idx) huffmanBuffer[idx] = 8u;
        for(u32 idx = 144u; idx < 256u; ++idx) huffmanBuffer[idx] = 9u;
        for(u32 idx = 256u; idx < 280u; ++idx) huffmanBuffer[idx] = 7u;
        for(u32 idx = 280u; idx < 288u; ++idx) huffmanBuffer[idx] = 8u;
        for(u32 idx = 288u; idx < 318u; ++idx) huffmanBuffer[idx] = 5u;

        lengthData   = {huffmanBuffer.data(), 288u};
        distanceData = {huffmanBuffer.data() + 288u, 32u};
      } else if(BTYPE == 2u) { // Dynamic table
        u32 HLIT  = inBits.Read(5) + 257u;
        u32 HDIST = inBits.Read(5) + 1u;
        u32 HCLEN = inBits.Read(4) + 4u;

        u8 HCLEN_table[std::size(DeflateHCLENMap)] = {};
        for(u32 idx = 0u; idx < HCLEN; ++idx)
          HCLEN_table[DeflateHCLENMap[idx]] =
            static_cast<u8>(inBits.Read(3u));

        HuffmanTable lengthTable(7u, HCLEN_table);

        u32 lengthIdx   = 0u;
        u32 lengthCount = HLIT + HDIST;

        while(lengthIdx < lengthCount) {
          u8 lengthRepeat  = 1u;
          u8 lengthValue   = 0u;
          u8 encodedLength = lengthTable.Decode<u8>(inBits);

          if(encodedLength <= 15u) {
            lengthValue = encodedLength;
          } else if(encodedLength == 16u) {
            lengthRepeat = 3u + inBits.Read<u8>(2);
            if(lengthIdx == 0u)
              throw std::runtime_error("Deflate: bad encoded length");
            lengthValue = huffmanBuffer[lengthIdx - 1u];
          } else if(encodedLength == 17u) {
            lengthRepeat = 3u + inBits.Read<u8>(3);
          } else if(encodedLength == 18u) {
            lengthRepeat = 11u + inBits.Read<u8>(7);
          } else {
            throw std::runtime_error("Deflate: bad encoded length");
          }

          for(u32 idx = 0u; idx < lengthRepeat; ++idx) {
            huffmanBuffer[lengthIdx] = lengthValue;
            ++lengthIdx;
          }

          lengthData   = {huffmanBuffer.data(), HLIT};
          distanceData = {huffmanBuffer.data() + HLIT, HDIST};
        }

        if(lengthIdx != lengthCount)
          throw new std::runtime_error(
            "Deflate: bad literal length count");

      } else {
        throw std::runtime_error("Deflate: bad block type");
      }

      literalLengthTable.Initialize(lengthData);
      distanceTable.Initialize(distanceData);

      for(;;) {
        u32 literalLength = literalLengthTable.Decode(inBits);

        if(literalLength == 256u) break;

        if(literalLength < 256u) {
          outBytes.Write(literalLength & 0xff);
        } else {
          auto literalIdx = literalLength - 257u;
          auto length     = DeflateExtraLengthValue[literalIdx];

          auto lengthExtraBits = DeflateExtraLengthBits[literalIdx];
          if(lengthExtraBits > 0u)
            length += inBits.Read(lengthExtraBits);

          auto distanceIdx = distanceTable.Decode(inBits);
          auto distance    = DeflateExtraDistanceValue[distanceIdx];

          auto distanceExtraBits =
            DeflateExtraDistanceBits[distanceIdx];
          if(distanceExtraBits > 0u)
            distance += inBits.Read(distanceExtraBits);

          if(outBytes.size() < distance)
            throw std::runtime_error(
              "Deflate: out of bounds lookback distance");

          auto rangeStart = outBytes.size() - distance;
          for(u32 idx = 0u; idx < length; ++idx)
            outBytes.Write(outBytes[rangeStart + idx]);
        }
      }
    }
    if(BFINAL) break;
  }

  return outBytes.data();
}

static Arr<u8> PngReconstruct(
  Span<u8 const> data, UInt2 size, u32 channelCount, u32 bitsPerChannel)
{
  u32 bytesPerPixel = bitsPerChannel * channelCount / 8u;
  u32 scanlineSize  = size[0] * bytesPerPixel;
  u32 outputSize    = size[1] * scanlineSize;

  ByteIStream inData(data);
  ByteOStream outData(outputSize);

  const auto paeth = [](u8 a, u8 b, u8 c) {
    auto p  = a + b - c;
    auto pa = std::abs(p - a);
    auto pb = std::abs(p - b);
    auto pc = std::abs(p - c);
    if(pa <= pb && pa <= pc) return a;
    else if(pb <= pc)
      return b;
    else
      return c;
  };

  Span<u8 const> previousScanline;
  for(u32 rowIdx = 0u; rowIdx < size[1]; ++rowIdx) {
    u64 scanlineStart = outData.size();

    u32  filter   = inData.Read<u8>();
    auto scanline = inData.ReadBytes(scanlineSize);

    switch(filter) {
      case 0u:
        outData.Write(scanline);
        break;
      case 1u: {
        for(u32 byteIdx = 0u; byteIdx < bytesPerPixel; ++byteIdx)
          outData.Write(scanline[byteIdx]);
        for(u32 byteIdx = bytesPerPixel; byteIdx < scanline.size();
            ++byteIdx) {
          u8 cur  = scanline[byteIdx];
          u8 left = outData[outData.size() - bytesPerPixel];
          outData.Write(cur + left);
        }
      } break;
      case 2u:
        for(u32 byteIdx = 0u; byteIdx < scanline.size(); ++byteIdx) {
          u8 cur = scanline[byteIdx];
          u8 top = previousScanline[byteIdx];
          outData.Write(cur + top);
        }
        break;
      case 3u:
        for(u32 byteIdx = 0u; byteIdx < bytesPerPixel; ++byteIdx) {
          u8 cur  = scanline[byteIdx];
          u8 left = 0u;
          u8 top  = previousScanline[byteIdx];
          outData.Write(cur + (left + top) / 2u);
        }
        for(u32 byteIdx = bytesPerPixel; byteIdx < scanline.size();
            ++byteIdx) {
          u8 cur  = scanline[byteIdx];
          u8 left = outData[outData.size() - bytesPerPixel];
          u8 top  = previousScanline[byteIdx];
          outData.Write(cur + (left + top) / 2u);
        }
        break;
      case 4u:
        for(u32 byteIdx = 0u; byteIdx < bytesPerPixel; ++byteIdx) {
          u8 cur     = scanline[byteIdx];
          u8 left    = 0u;
          u8 top     = previousScanline[byteIdx];
          u8 topLeft = 0;
          outData.Write(cur + paeth(left, top, topLeft));
        }
        for(u32 byteIdx = bytesPerPixel; byteIdx < scanline.size();
            ++byteIdx) {
          u8 cur     = scanline[byteIdx];
          u8 left    = outData[outData.size() - bytesPerPixel];
          u8 top     = previousScanline[byteIdx];
          u8 topLeft = previousScanline[byteIdx - bytesPerPixel];
          outData.Write(cur + paeth(left, top, topLeft));
        }
        break;
      default:
        throw std::runtime_error("PNG: bad scanline filter");
    }
    previousScanline = {&outData[scanlineStart], scanlineSize};
  }
  return outData.data();
}

bool DataReader::isPng(std::istream& src)
{
  auto pos  = src.tellg();
  auto size = streamSize(src);

  if(size < std::size(PngSignature)) return false;

  std::array<char, sizeof(PngSignature)> signature;
  src.read(signature.data(), signature.size());
  src.seekg(pos);

  return memcmp(
           std::data(signature), PngSignature, sizeof(PngSignature)) ==
         0u;
}

data::Image
DataReader::readPng(std::istream& src, const ImageOptions& options)
{
  if(!isPng(src)) throw std::runtime_error("PNG: bad signature");
  src.seekg(sizeof(PngSignature));

  auto bytes = streamReadBytes(src);

  data::Image out;
  out.uri = options.uri;

  ByteIStream stream({bytes.data(), bytes.size()});

  u8 colorType   = 0u;
  u8 compression = 0u;
  u8 filter      = 0u;
  u8 interlace   = 0u;

  u32 bitsPerChannel = 0u;
  u32 channelCount   = 0u;

  Arr<u8> compressedImageBytes;
  compressedImageBytes.reserve(bytes.size());

  Arr<u8> palette;

  for(;;) {
    bool hasMoreChunks = true;

    auto dataSize  = stream.ReadSwap<u32>();
    auto chunkType = stream.ReadSwap<u32>();

    switch(chunkType) {
      case fourCC("IHDR"): {
        out.size[0] = stream.ReadSwap<u32>();
        out.size[1] = stream.ReadSwap<u32>();

        bitsPerChannel = stream.Read<u8>();

        colorType   = stream.Read<u8>();
        compression = stream.Read<u8>();
        filter      = stream.Read<u8>();
        interlace   = stream.Read<u8>();

        if(compression != 0)
          throw std::runtime_error("PNG: unsopported compression");
        if(filter != 0)
          throw std::runtime_error("PNG: unsopported filter");
        if(interlace != 0)
          throw std::runtime_error("PNG: unsopported interlace");

        switch(colorType) {
          case 0u: // Grayscale
            channelCount = 1u;
            out.format   = (bitsPerChannel <= 8u) ? Format::R8_UNORM
                                                  : Format::R16_UNORM;
            break;
          case 2u: // Color
            channelCount = 3u;
            out.format   = (bitsPerChannel == 8u)
                             ? Format::R8G8B8A8_UNORM
                             : Format::R16G16B16A16_UNORM;
            break;
          case 3u: // Indexed color
            channelCount = 1u;
            out.format   = Format::R8G8B8A8_UNORM;
            break;
          case 4u: // Grayscale with alpha
            // channelCount = 2u;
            throw std::runtime_error(
              "PNG: grayscale with alpha not supported");
          case 6u: // Color with alpha
            channelCount = 4u;
            out.format   = (bitsPerChannel == 8u)
                             ? Format::R8G8B8A8_UNORM
                             : Format::R16G16B16A16_UNORM;
            break;
        }
      } break;

      case fourCC("IDAT"): {
        auto chunkData = stream.ReadBytes(dataSize);
        compressedImageBytes.insert(
          compressedImageBytes.end(), chunkData.begin(),
          chunkData.end());
      } break;

      case fourCC("PLTE"): {
        palette.reserve(dataSize / 3u);
        for(u32 idx = 0u; idx < dataSize / 3u; ++idx) {
          palette.push_back(stream.Read<u8>());
          palette.push_back(stream.Read<u8>());
          palette.push_back(stream.Read<u8>());
          palette.push_back(255);
        }
      } break;

      case fourCC("tRNS"): {
        if(colorType != 3u)
          throw std::runtime_error(
            "PNG: transparency chunk supported only for indexed color "
            "images");

        out.format = Format::R8G8B8A8_UNORM;
        for(u64 idx = 0u; idx < dataSize; ++idx)
          palette[idx * 4u + 3u] = stream.Read<u8>();
      } break;

      case fourCC("IEND"):
        hasMoreChunks = false;
        break;

      default:
        stream.SkipBytes(dataSize);
        break;
    }

    // Skip CRC
    [[maybe_unused]] u32 crc = stream.ReadSwap<u32>();

    if(!hasMoreChunks) break;

    if(!stream.HasMoreData())
      throw std::runtime_error("PNG: missing end chunk");
  }

  u64 pixelCount =
    static_cast<u64>(out.size[0]) * static_cast<u64>(out.size[1]);
  u64 bitsPerPixel = (channelCount * bitsPerChannel) / 8u;
  u64 sizeHint     = pixelCount * bitsPerPixel +
                 out.size[1]; // Extra byte for each scanline's filter.

  auto expandedData = ZLibDeflate(compressedImageBytes, sizeHint);
  auto imageData    = PngReconstruct(
    expandedData, out.size, channelCount, bitsPerChannel);

  // Apply palette if present.
  if(!palette.empty()) {
    // Update channel count from 1 (indexed) to the final one.
    channelCount   = 3u;
    bitsPerChannel = 8u;

    Arr<u8> buffer;
    buffer.reserve(imageData.size() * channelCount);

    if(channelCount == 3u) {
      for(u64 idx: imageData) {
        buffer.push_back(palette[idx * 4u + 0u]);
        buffer.push_back(palette[idx * 4u + 1u]);
        buffer.push_back(palette[idx * 4u + 2u]);
      }
    } else {
      for(u64 idx: imageData) {
        buffer.push_back(palette[idx * 4u + 0u]);
        buffer.push_back(palette[idx * 4u + 1u]);
        buffer.push_back(palette[idx * 4u + 2u]);
        buffer.push_back(palette[idx * 4u + 3u]);
      }
    }

    imageData = std::move(buffer);
  }

  if(channelCount == 3u) {
    channelCount         = 4u;
    u64 bytesPerChannel  = bitsPerChannel / 8u;
    u64 oldChannelCount  = channelCount;
    u64 oldBytesPerPixel = oldChannelCount * bytesPerChannel;

    u64 newSize = pixelCount * channelCount * bitsPerChannel / 8u;

    Arr<u8> buffer;
    buffer.reserve(newSize);

    for(u64 idx = 0u; idx < imageData.size(); idx += oldBytesPerPixel) {
      for(u64 byteIdx = 0u; byteIdx < oldBytesPerPixel; ++byteIdx)
        buffer.push_back(imageData[idx + byteIdx]);
      for(u64 padIdx = 0u; padIdx < bytesPerChannel; ++padIdx)
        buffer.push_back(options.alphaPadding);
    }

    imageData = std::move(buffer);
  }

  // If there are less than 8 bits per channel (grayscale), scale them
  // up to 8.
  if(bitsPerChannel < 8u) {
    Arr<u8> buffer;
    buffer.reserve(pixelCount * channelCount);

    u32 scaling = 8u - bitsPerChannel;

    BitIStream s(imageData);
    while(s.HasMoreData())
      buffer.push_back(
        static_cast<u8>(s.Read(bitsPerChannel) << scaling));

    imageData = std::move(buffer);
    // bitsPerChannel = 8u;
  }

  out.texels = std::move(imageData);
  return out;
}
