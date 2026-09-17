#pragma once

#ifdef __clang__
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wexit-time-destructors"
#endif

#include "vuldir/core/Definitions.hpp"
#include "vuldir/core/Logger.hpp"
#include "vuldir/core/STL.hpp"
#include "vuldir/core/Stream.hpp"
#include "vuldir/core/Types.hpp"
#include "vuldir/core/Uti.hpp"

#include <charconv>

namespace vd::Json {
class Value;

using Null   = std::monostate;
using Bool   = bool;
using Number = f64;
using String = Str;
using Array  = Arr<Value>;
using Object = std::map<String, Value>;

using Any = Var<Null, Bool, Number, String, Array, Object>;

// readValue recurses once per nesting level, so a file of nothing but
// "[[[[..." would otherwise overflow the stack before it ran out of input.
// Far beyond anything a real document nests.
inline constexpr u32 MaxParseDepth = 256u;

class Value : public Any
{
public:
  Value(): Any() {}

  Value(const Value&) = default;
  Value(Value&&)      = default;

  // Constrained, and deliberately the only converting constructor: an
  // unconstrained one is a better match for a non-const Value lvalue than
  // Value(const Value&) is, so copying one went through the variant's
  // converting constructor instead of its copy constructor.
  template<typename T>
    requires(!std::is_base_of_v<Value, std::decay_t<T>>)
  Value(T&& x): Any(std::forward<T>(x))
  {}

  Value& operator=(const Value&) = default;
  Value& operator=(Value&&)      = default;

public:
  bool HasValue() const { return !std::holds_alternative<Null>(*this); }

  const Value& operator[](const String& key) const
  {
    return (*this)[key.c_str()];
  }

  const Value& operator[](const char* key) const
  {
    static const Value NullValue = Null{};

    if(!IsObject()) return NullValue;

    const auto it = AsObject().find(key);
    return (it == AsObject().end()) ? NullValue : it->second;
  }

  const Value& operator[](u64 idx) const
  {
    static const Value NullValue = Null{};
    if(!IsArray() || idx >= AsArray().size()) return NullValue;
    return AsArray()[idx];
  }

  const Value& operator[](u32 idx) const
  {
    return (*this)[static_cast<u64>(idx)];
  }

  bool IsBool() const { return std::holds_alternative<Bool>(*this); }

  bool IsNumber() const
  {
    return std::holds_alternative<Number>(*this);
  }

  bool IsString() const
  {
    return std::holds_alternative<String>(*this);
  }

  bool IsArray() const { return std::holds_alternative<Array>(*this); }

  bool IsObject() const
  {
    return std::holds_alternative<Object>(*this);
  }

  Bool AsBool() const { return std::get<Bool>(*this); }

  Opt<Bool> AsBoolOpt() const
  {
    if(IsBool()) return std::get<Bool>(*this);
    else
      return std::nullopt;
  }

  Bool AsBoolOpt(bool defaultValue) const
  {
    if(IsBool()) return std::get<Bool>(*this);
    else
      return defaultValue;
  }

  Number AsNumber() const { return std::get<Number>(*this); }

  Opt<Number> AsNumberOpt() const
  {
    if(IsNumber()) return std::get<Number>(*this);
    else
      return std::nullopt;
  }

  Number AsNumberOpt(Number defaultValue) const
  {
    if(IsNumber()) return std::get<Number>(*this);
    else
      return defaultValue;
  }

  template<typename T>
  T AsNumber() const
  {
    const Number value = std::get<Number>(*this);
    if(!std::isfinite(value))
      throw std::runtime_error("JSON: number is not finite");

    if constexpr(std::is_integral_v<T>) {
      if(std::trunc(value) != value)
        throw std::runtime_error("JSON: expected an integer");

      constexpr int valueBits = std::numeric_limits<T>::digits;
      const Number upperExclusive = std::ldexp(1.0, valueBits);
      const Number lower = std::is_signed_v<T> ? -upperExclusive : 0.0;
      if(value < lower || value >= upperExclusive)
        throw std::runtime_error("JSON: integer is out of range");
    } else if constexpr(std::is_floating_point_v<T>) {
      if(
        value < static_cast<Number>(std::numeric_limits<T>::lowest()) ||
        value > static_cast<Number>(std::numeric_limits<T>::max()))
        throw std::runtime_error("JSON: number is out of range");
    }

    return static_cast<T>(value);
  }

  template<typename T>
  Opt<T> AsNumberOpt() const
  {
    if(IsNumber()) return AsNumber<T>();
    else
      return std::nullopt;
  }

  template<typename T>
  T AsNumberOpt(T defaultValue) const
  {
    if(IsNumber()) return AsNumber<T>();
    else
      return defaultValue;
  }

  Strv AsString() const { return std::get<String>(*this); }

  Opt<Strv> AsStringOpt() const
  {
    if(IsString()) return std::get<String>(*this);
    else
      return std::nullopt;
  }

  Strv AsStringOpt(Strv defaultValue) const
  {
    if(IsString()) return std::get<String>(*this);
    else
      return defaultValue;
  }

  const Array& AsArray() const { return std::get<Array>(*this); }

  template<typename T>
  T AsVector() const
  {
    if(!IsArray()) return {};

    T           ret = {};
    const auto& src = AsArray();
    for(u32 idx = 0u; idx < std::min(std::size(ret), src.size());
        ++idx) {
      ret[idx] = src[idx].AsNumber<typename T::value_type>();
    }
    return ret;
  }

  template<typename T>
  Opt<T> AsVectorOpt() const
  {
    if(IsArray()) return AsVector<T>();
    else
      return std::nullopt;
  }

  template<typename T>
  T AsVectorOpt(T defaultValue) const
  {
    if(IsArray()) return AsVector<T>();
    else
      return defaultValue;
  }

  const Object& AsObject() const { return std::get<Object>(*this); }

  void Read(const fs::path& path)
  {
    std::ifstream src(path);
    if(!src)
      throw std::runtime_error("JSON: unable to open input file");
    Read(src);
  }

  void Read(std::istream& src)
  {
    readValue(src, 0u);
    src >> std::ws;
    if(src.peek() != std::char_traits<char>::eof())
      throw std::runtime_error("JSON: trailing data");
  }

private:
  [[noreturn]] static void parseError(const char* message)
  {
    throw std::runtime_error(message);
  }

  static void expect(std::istream& src, char expected)
  {
    src >> std::ws;
    if(src.get() != expected) parseError("JSON: unexpected character");
  }

  static u32 readHexQuad(std::istream& src)
  {
    u32 value = 0u;
    for(u32 idx = 0u; idx < 4u; ++idx) {
      const int c = src.get();
      if(c == std::char_traits<char>::eof())
        parseError("JSON: truncated Unicode escape");

      value <<= 4u;
      if(c >= '0' && c <= '9') value |= static_cast<u32>(c - '0');
      else if(c >= 'a' && c <= 'f')
        value |= static_cast<u32>(c - 'a' + 10);
      else if(c >= 'A' && c <= 'F')
        value |= static_cast<u32>(c - 'A' + 10);
      else
        parseError("JSON: invalid Unicode escape");
    }
    return value;
  }

  static void appendUtf8(String& value, u32 codepoint)
  {
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

  static String readString(std::istream& src)
  {
    if(src.get() != '"') parseError("JSON: expected string");

    String value;
    for(;;) {
      const int c = src.get();
      if(c == std::char_traits<char>::eof())
        parseError("JSON: unterminated string");
      if(c == '"') return value;
      if(static_cast<unsigned char>(c) < 0x20u)
        parseError("JSON: unescaped control character");
      if(c != '\\') {
        value.push_back(static_cast<char>(c));
        continue;
      }

      const int escape = src.get();
      switch(escape) {
        case '"':
        case '\\':
        case '/':
          value.push_back(static_cast<char>(escape));
          break;
        case 'b':
          value.push_back('\b');
          break;
        case 'f':
          value.push_back('\f');
          break;
        case 'n':
          value.push_back('\n');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case 't':
          value.push_back('\t');
          break;
        case 'u': {
          u32 codepoint = readHexQuad(src);
          if(codepoint >= 0xd800u && codepoint <= 0xdbffu) {
            if(src.get() != '\\' || src.get() != 'u')
              parseError("JSON: missing low surrogate");
            const u32 low = readHexQuad(src);
            if(low < 0xdc00u || low > 0xdfffu)
              parseError("JSON: invalid low surrogate");
            codepoint = 0x10000u +
                        ((codepoint - 0xd800u) << 10u) +
                        (low - 0xdc00u);
          } else if(codepoint >= 0xdc00u && codepoint <= 0xdfffu) {
            parseError("JSON: unexpected low surrogate");
          }
          appendUtf8(value, codepoint);
        } break;
        default:
          parseError("JSON: invalid string escape");
      }
    }
  }

  static Number readNumber(std::istream& src)
  {
    String token;
    auto take = [&] { token.push_back(static_cast<char>(src.get())); };
    auto isDigit = [](int c) { return c >= '0' && c <= '9'; };

    if(src.peek() == '-') take();
    if(src.peek() == '0') {
      take();
      if(isDigit(src.peek())) parseError("JSON: invalid leading zero");
    } else {
      if(src.peek() < '1' || src.peek() > '9')
        parseError("JSON: invalid number");
      do take(); while(isDigit(src.peek()));
    }

    if(src.peek() == '.') {
      take();
      if(!isDigit(src.peek()))
        parseError("JSON: missing fractional digits");
      do take(); while(isDigit(src.peek()));
    }

    if(src.peek() == 'e' || src.peek() == 'E') {
      take();
      if(src.peek() == '+' || src.peek() == '-') take();
      if(!isDigit(src.peek()))
        parseError("JSON: missing exponent digits");
      do take(); while(isDigit(src.peek()));
    }

    Number value = 0.0;
    const auto parsed = std::from_chars(
      token.data(), token.data() + token.size(), value);
    if(
      parsed.ec != std::errc{} ||
      parsed.ptr != token.data() + token.size() ||
      !std::isfinite(value)) {
      parseError("JSON: number is out of range");
    }
    return value;
  }

  void readValue(std::istream& src, u32 depth)
  {
    if(depth > MaxParseDepth) parseError("JSON: nesting is too deep");

    src >> std::ws;
    const int next = src.peek();
    if(next == std::char_traits<char>::eof())
      parseError("JSON: unexpected end of input");

    switch(next) {
      case '"': {
        *this = readString(src);
      } break;
      case '[': {
        Array value;
        src.get();
        src >> std::ws;
        if(src.peek() == ']') {
          src.get();
          *this = std::move(value);
          return;
        }

        for(;;) {
          value.emplace_back().readValue(src, depth + 1u);
          src >> std::ws;
          const int delimiter = src.get();
          if(delimiter == ']') break;
          if(delimiter != ',')
            parseError("JSON: expected ',' or ']'");
        }
        *this = std::move(value);
      } break;
      case '{': {
        Object value;
        src.get();
        src >> std::ws;
        if(src.peek() == '}') {
          src.get();
          *this = std::move(value);
          return;
        }

        for(;;) {
          String key;
          src >> std::ws;
          if(src.peek() != '"')
            parseError("JSON: expected object key");
          key = readString(src);
          expect(src, ':');
          if(value.contains(key))
            parseError("JSON: duplicate object key");
          value[std::move(key)].readValue(src, depth + 1u);
          src >> std::ws;
          const int delimiter = src.get();
          if(delimiter == '}') break;
          if(delimiter != ',')
            parseError("JSON: expected ',' or '}'");
        }
        *this = std::move(value);
      } break;
      default: {
        if(
          std::isdigit(static_cast<unsigned char>(next)) || next == '-') {
          *this = readNumber(src);
        } else if(std::isalpha(static_cast<unsigned char>(next))) {
          String value;
          while(
            src.peek() != std::char_traits<char>::eof() &&
            std::isalpha(static_cast<unsigned char>(src.peek()))) {
            value.push_back(static_cast<char>(src.peek()));
            src.get();
          }

          if(value == "null") *this = {};
          else if(value == "true")
            *this = true;
          else if(value == "false")
            *this = false;
          else
            parseError("JSON: invalid literal");
        } else
          parseError("JSON: invalid value");
      } break;
    }
  }
};

} // namespace vd::Json

#ifdef __clang__
  #pragma clang diagnostic pop
#endif
