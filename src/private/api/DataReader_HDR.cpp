#include "vuldir/DataReader.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace vd;

// Radiance RGBE (.hdr): minimal reader for IBL environment maps.
static bool isHdrSignature(std::istream& src)
{
  auto pos = src.tellg();
  char magic[10]{};
  src.read(magic, 10);
  src.seekg(pos);
  return std::strncmp(magic, "#?RADIANCE", 10) == 0 ||
         std::strncmp(magic, "#?RGBE", 6) == 0;
}

bool DataReader::isHdr(std::istream& src)
{
  return isHdrSignature(src);
}

static void rgbeToFloat(const u8 rgbe[4], f32 rgb[3])
{
  if(rgbe[3]) {
    const f32 scale =
      std::ldexp(1.f, static_cast<int>(rgbe[3]) - 128 - 8);
    rgb[0] = rgbe[0] * scale;
    rgb[1] = rgbe[1] * scale;
    rgb[2] = rgbe[2] * scale;
  } else {
    rgb[0] = rgb[1] = rgb[2] = 0.f;
  }
}

static void
readHdrScanline(ByteIStream& in, u32 width, Arr<u8>& rgbeRow)
{
  rgbeRow.resize(static_cast<size_t>(width) * 4u);

  // New RLE: first pixel marker 2,2,hi,lo width
  if(width < 8u || width > 0x7fffu) {
    for(u32 i = 0; i < width; ++i) {
      rgbeRow[i * 4 + 0] = in.Read<u8>();
      rgbeRow[i * 4 + 1] = in.Read<u8>();
      rgbeRow[i * 4 + 2] = in.Read<u8>();
      rgbeRow[i * 4 + 3] = in.Read<u8>();
    }
    return;
  }

  u8 b0 = in.Read<u8>();
  u8 b1 = in.Read<u8>();
  u8 b2 = in.Read<u8>();
  u8 b3 = in.Read<u8>();

  if(b0 != 2 || b1 != 2 || (b2 & 0x80)) {
    // Flat scanline: already consumed first pixel
    rgbeRow[0] = b0;
    rgbeRow[1] = b1;
    rgbeRow[2] = b2;
    rgbeRow[3] = b3;
    for(u32 i = 1; i < width; ++i) {
      rgbeRow[i * 4 + 0] = in.Read<u8>();
      rgbeRow[i * 4 + 1] = in.Read<u8>();
      rgbeRow[i * 4 + 2] = in.Read<u8>();
      rgbeRow[i * 4 + 3] = in.Read<u8>();
    }
    return;
  }

  const u32 scanWidth = (static_cast<u32>(b2) << 8) | b3;
  if(scanWidth != width)
    throw std::runtime_error("HDR: scanline width mismatch");

  Arr<u8> channel(width);
  for(u32 c = 0; c < 4; ++c) {
    u32 x = 0;
    while(x < width) {
      u8 code = in.Read<u8>();
      if(code == 0u)
        throw std::runtime_error("HDR: zero-length RLE packet");
      if(code > 128) {
        u8  value = in.Read<u8>();
        u32 count = code - 128;
        if(x + count > width)
          throw std::runtime_error("HDR: RLE overrun");
        for(u32 i = 0; i < count; ++i) channel[x++] = value;
      } else {
        u32 count = code;
        if(x + count > width)
          throw std::runtime_error("HDR: RLE overrun");
        for(u32 i = 0; i < count; ++i) channel[x++] = in.Read<u8>();
      }
    }
    for(u32 i = 0; i < width; ++i) rgbeRow[i * 4 + c] = channel[i];
  }
}

data::Image
DataReader::readHdr(std::istream& src, const ImageOptions& options)
{
  if(!isHdr(src)) throw std::runtime_error("HDR: bad signature");

  // Header lines until empty line
  Str  line;
  auto readLine = [&]() -> bool {
    line.clear();
    char ch;
    while(src.get(ch)) {
      if(ch == '\n') return true;
      if(ch != '\r') line.push_back(ch);
      if(line.size() > 16u * 1024u)
        throw std::runtime_error("HDR: header line is too long");
    }
    return !line.empty();
  };

  if(!readLine()) throw std::runtime_error("HDR: truncated header");
  i32  width = 0, height = 0;
  char majorAxis = 0, minorAxis = 0;
  bool majorIncreasing = false, minorIncreasing = false;
  bool sawFormat = false;

  for(;;) {
    if(!readLine()) throw std::runtime_error("HDR: truncated header");
    if(line.empty()) break;
    if(line.rfind("FORMAT=", 0) == 0) {
      if(line != "FORMAT=32-bit_rle_rgbe") {
        throw std::runtime_error("HDR: unsupported FORMAT");
      }
      sawFormat = true;
    }
  }
  if(!sawFormat) throw std::runtime_error("HDR: missing FORMAT");

  if(!readLine()) throw std::runtime_error("HDR: missing resolution");
  i32 majorSize = 0, minorSize = 0;
  {
    char axis0 = 0, axis1 = 0;
    char sign0 = 0, sign1 = 0;
    i32  a = 0, b = 0;
    if(
      std::sscanf(
        line.c_str(), "%c%c %d %c%c %d", &sign0, &axis0, &a, &sign1,
        &axis1, &b) != 6) {
      throw std::runtime_error("HDR: bad resolution line");
    }
    axis0 = static_cast<char>(
      std::toupper(static_cast<unsigned char>(axis0)));
    axis1 = static_cast<char>(
      std::toupper(static_cast<unsigned char>(axis1)));
    if(
      (sign0 != '+' && sign0 != '-') ||
      (sign1 != '+' && sign1 != '-') || axis0 == axis1 ||
      (axis0 != 'X' && axis0 != 'Y') || (axis1 != 'X' && axis1 != 'Y'))
      throw std::runtime_error("HDR: invalid resolution axes");

    majorAxis       = axis0;
    minorAxis       = axis1;
    majorIncreasing = sign0 == '+';
    minorIncreasing = sign1 == '+';
    majorSize       = a;
    minorSize       = b;
    if(axis0 == 'Y' || axis0 == 'y') {
      height = a;
      width  = b;
    } else {
      width  = a;
      height = b;
    }
  }

  if(width <= 0 || height <= 0)
    throw std::runtime_error("HDR: invalid dimensions");
  const u64 pixelCount =
    static_cast<u64>(width) * static_cast<u64>(height);
  constexpr u64 MaxDecodedImageSize = 512u * 1024u * 1024u;
  if(pixelCount > MaxDecodedImageSize / (4u * sizeof(f32)))
    throw std::runtime_error("HDR: decoded image is too large");

  auto        bytes = streamReadBytes(src);
  ByteIStream in({bytes.data(), bytes.size()});

  data::Image out;
  out.uri    = options.uri;
  out.format = Format::R32G32B32A32_SFLOAT;
  out.size   = {static_cast<u32>(width), static_cast<u32>(height)};

  Arr<f32> pixels(static_cast<size_t>(pixelCount) * 4u);
  Arr<u8>  rgbeRow;

  for(i32 major = 0; major < majorSize; ++major) {
    readHdrScanline(in, static_cast<u32>(minorSize), rgbeRow);
    const i32 majorCoord =
      majorIncreasing ? major : (majorSize - 1 - major);
    for(i32 minor = 0; minor < minorSize; ++minor) {
      const i32 minorCoord =
        minorIncreasing ? minor : (minorSize - 1 - minor);
      const i32 x = majorAxis == 'X' ? majorCoord : minorCoord;
      const i32 y = minorAxis == 'Y' ? minorCoord : majorCoord;
      f32       rgb[3];
      rgbeToFloat(&rgbeRow[static_cast<size_t>(minor) * 4], rgb);
      const size_t dst = (static_cast<size_t>(y) * width + x) * 4u;
      pixels[dst + 0]  = rgb[0];
      pixels[dst + 1]  = rgb[1];
      pixels[dst + 2]  = rgb[2];
      pixels[dst + 3]  = 1.f;
    }
  }

  out.texels.resize(pixels.size() * sizeof(f32));
  std::memcpy(out.texels.data(), pixels.data(), out.texels.size());
  return out;
}
