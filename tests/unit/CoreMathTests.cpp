#include <catch2/catch_test_macros.hpp>

#include "vuldir/core/Json.hpp"
#include "vuldir/core/Library.hpp"
#include "vuldir/core/Math.hpp"
#include "vuldir/core/Telemetry.hpp"

using namespace vd;

static_assert(!std::is_copy_constructible_v<Library>);
static_assert(!std::is_move_constructible_v<Library>);

TEST_CASE("disabled trace contexts are safe", "[core][telemetry]")
{
  VDTraceContext root(nullptr);
  REQUIRE(root.prev == nullptr);
  {
    VDTraceContext child(&root);
    REQUIRE(root.next == &child);
    VD_TRACE("disabled trace", 42u);
  }
  REQUIRE(root.next == nullptr);
}

TEST_CASE("matrix component operations preserve every component", "[core][math]")
{
  constexpr Float22 lhs{{{1.f, 2.f}, {3.f, 4.f}}};
  constexpr Float22 rhs{{{4.f, 3.f}, {2.f, 1.f}}};

  constexpr auto sum    = mt::Add(lhs, rhs);
  constexpr auto offset = mt::Add(lhs, 2.f);
  constexpr auto diff   = mt::Sub(lhs, rhs);
  constexpr auto scaled = mt::Mul(lhs, 2.f);
  constexpr auto divided = mt::Div(lhs, 2.f);

  STATIC_REQUIRE(sum[0][0] == 5.f);
  STATIC_REQUIRE(sum[1][1] == 5.f);
  STATIC_REQUIRE(offset[1][0] == 5.f);
  STATIC_REQUIRE(diff[0][1] == -1.f);
  STATIC_REQUIRE(scaled[1][0] == 6.f);
  STATIC_REQUIRE(divided[1][1] == 2.f);
}

TEST_CASE("math constants and scalar division have their declared shape", "[core][math]")
{
  constexpr auto ones = mt::One33<int>();
  constexpr auto w    = mt::W4<int>();
  constexpr auto quot = mt::Div(12, Int3{2, 3, 4});

  STATIC_REQUIRE(ones[2][2] == 1);
  STATIC_REQUIRE(w.size() == 4u);
  STATIC_REQUIRE(w[3] == 1);
  STATIC_REQUIRE(quot[0] == 6);
  STATIC_REQUIRE(quot[1] == 4);
  STATIC_REQUIRE(quot[2] == 3);
}

TEST_CASE("half precision conversions handle rounding and subnormals", "[core][math]")
{
  REQUIRE(mt::Float32ToFloat16(1.f) == 0x3c00u);
  REQUIRE(mt::Float32ToFloat16(1.00048828125f) == 0x3c00u);
  REQUIRE(std::bit_cast<u32>(mt::Float16ToFloat32(0x0001u)) == 0x33800000u);
  REQUIRE(mt::Float32ToFloat16(mt::Float16ToFloat32(0x0001u)) == 0x0001u);
  REQUIRE(mt::Float32ToFloat16(mt::Float16ToFloat32(0x7bffu)) == 0x7bffu);
}

TEST_CASE("alignment differences are offsets rather than addresses", "[core][utility]")
{
  STATIC_REQUIRE(getAlignmentDiff(37u, 0u) == 0u);
  STATIC_REQUIRE(getAlignmentDiff(37u, 1u) == 0u);
  STATIC_REQUIRE(getAlignmentDiff(12u, 4u) == 0u);
  STATIC_REQUIRE(getAlignmentDiff(13u, 4u) == 3u);
  STATIC_REQUIRE(getAlignmentDiff(13u, 6u) == 5u);
}

TEST_CASE("bit and alignment helpers handle boundary values", "[core][utility]")
{
  STATIC_REQUIRE(bitMask32(0u, 32u) == MaxU32);
  STATIC_REQUIRE(bitMask32(32u, 0u) == 0u);
  STATIC_REQUIRE(bitSet32(0u, MaxU32, 0u, 32u) == MaxU32);
  STATIC_REQUIRE(bitGet32(MaxU32, 0u, 32u) == MaxU32);
  STATIC_REQUIRE(bitReverse(1u, 32u) == 0x80000000u);
  STATIC_REQUIRE(bitReverse(1u, 0u) == 0u);
  STATIC_REQUIRE(divideRoundingUp(MaxU64, MaxU64) == 1u);
  REQUIRE_THROWS_AS(bitMask32(1u, 32u), std::invalid_argument);
  REQUIRE_THROWS_AS(
    alignUp(MaxU64, static_cast<u64>(2u)), std::overflow_error);
  REQUIRE_THROWS_AS(alignDown(4u, 3u), std::invalid_argument);
  REQUIRE_THROWS_AS(divideRoundingUp(1u, 0u), std::invalid_argument);
}

TEST_CASE("formatted strings do not retain the terminator", "[core][utility]")
{
  const auto value = formatString("%s %u", "value", 42u);
  REQUIRE(value == "value 42");
  REQUIRE(value.size() == 8u);
}

TEST_CASE("UTF conversions are exact and reject malformed input", "[core][utility]")
{
  const Str utf8 = "ASCII \xe6\x97\xa5\xe6\x9c\xac \xf0\x9f\x98\x80";
  REQUIRE(narrow(widen(utf8)) == utf8);
  REQUIRE(widen("").empty());
  REQUIRE(narrow(L"").empty());
  REQUIRE_THROWS(widen(Str(1u, static_cast<char>(0xffu))));
}

TEST_CASE("stream byte reads use the remaining length", "[core][stream]")
{
  std::istringstream stream{"abcdef"};
  stream.seekg(2);
  const auto remaining = streamReadBytes(stream);
  REQUIRE(remaining == Arr<u8>{'c', 'd', 'e', 'f'});

  std::istringstream shortStream{"abc"};
  REQUIRE_THROWS_AS(
    streamReadBytes(shortStream, 4u), std::runtime_error);

  std::istringstream valueStream{Str("\x34\x12", 2u)};
  REQUIRE(streamRead<u16>(valueStream) == 0x1234u);
  REQUIRE_THROWS_AS(streamRead<u8>(valueStream), std::runtime_error);
}

TEST_CASE("memory streams reject out-of-range seeks", "[core][stream]")
{
  const Arr<u8> bytes{'a', 'b', 'c'};
  IMemoryStream stream(bytes);

  stream.seekg(-1, std::ios::end);
  REQUIRE(stream.good());
  REQUIRE(stream.get() == 'c');

  stream.seekg(1, std::ios::beg);
  REQUIRE(streamSize(stream) == 3u);
  REQUIRE(stream.tellg() == 1);

  stream.seekg(4, std::ios::beg);
  REQUIRE(stream.fail());
  stream.clear();
  stream.seekg(-4, std::ios::end);
  REQUIRE(stream.fail());

  const Arr<u8> empty;
  IMemoryStream emptyStream(empty);
  REQUIRE(streamSize(emptyStream) == 0u);
  REQUIRE(emptyStream.tellg() == 0);
}

TEST_CASE("bit streams round-trip unaligned words", "[core][stream]")
{
  BitOStream output;
  output.Write(0x5u, 3u);
  output.Write(0xabcdef01u, 32u);
  output.Write(0x2u, 2u);
  REQUIRE(output.size() == 37u);

  BitIStream input(output.data());
  REQUIRE(input.Read(3u) == 0x5u);
  REQUIRE(input.Peek(32u) == 0xabcdef01u);
  REQUIRE(input.Read(32u) == 0xabcdef01u);
  REQUIRE(input.Read(2u) == 0x2u);
  REQUIRE_THROWS_AS(input.Read(33u), std::invalid_argument);

  BitOStream byteAligned;
  byteAligned.Write(0xa5u, 8u);
  REQUIRE(byteAligned.size() == 8u);
  REQUIRE(byteAligned.data() == Arr<u8>{0xa5u});
}

TEST_CASE("JSON numeric conversions reject lossy integer values", "[core][json]")
{
  const Json::Value positive = Json::Number{42.0};
  const Json::Value negative = Json::Number{-1.0};
  const Json::Value fraction = Json::Number{1.5};
  const Json::Value tooLarge = std::ldexp(1.0, 64);

  REQUIRE(positive.AsNumber<u32>() == 42u);
  REQUIRE_THROWS_AS(negative.AsNumber<u32>(), std::runtime_error);
  REQUIRE_THROWS_AS(fraction.AsNumber<u32>(), std::runtime_error);
  REQUIRE_THROWS_AS(tooLarge.AsNumber<u64>(), std::runtime_error);
}

TEST_CASE("JSON strings decode escapes and reject malformed input", "[core][json]")
{
  Json::Value value;
  std::istringstream valid{
    R"json("line\nquote:\" slash:\/ A:\u0041 face:\uD83D\uDE00")json"};
  value.Read(valid);
  REQUIRE(
    value.AsString() ==
    Str("line\nquote:\" slash:/ A:A face:\xf0\x9f\x98\x80"));

  auto rejects = [](Strv text) {
    Json::Value candidate;
    std::istringstream input{Str(text)};
    REQUIRE_THROWS(candidate.Read(input));
  };
  rejects(R"json("bad\xescape")json");
  rejects(R"json("\uD800")json");
  rejects(R"json("\uDC00")json");
  rejects("\"raw\nnewline\"");
  rejects(R"json({"key":1,"key":2})json");
}

TEST_CASE("JSON numbers follow the JSON grammar", "[core][json]")
{
  Json::Value value;
  std::istringstream valid{"-0.5e+2"};
  value.Read(valid);
  REQUIRE(value.AsNumber() == -50.0);

  auto rejects = [](Strv text) {
    Json::Value candidate;
    std::istringstream input{Str(text)};
    REQUIRE_THROWS(candidate.Read(input));
  };
  rejects("01");
  rejects("1.");
  rejects("-.1");
  rejects("1e");
  rejects("1e9999");
}

TEST_CASE("JSON array indexing is bounds-safe", "[core][json]")
{
  const Json::Value array = Json::Array{Json::Number{7.0}};
  const Json::Value scalar = Json::Number{9.0};

  REQUIRE(array[0u].AsNumber() == 7.0);
  REQUIRE(!array[1u].HasValue());
  REQUIRE(!scalar[0u].HasValue());
}
