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

namespace vd::Json {
class Value;

using Null   = std::monostate;
using Bool   = bool;
using Number = f64;
using String = Str;
using Array  = Arr<Value>;
using Object = std::map<String, Value>;

using Any = Var<Null, Bool, Number, String, Array, Object>;

class Value : public Any
{
public:
  Value(): Any() {}

  Value(const Value&) = default;
  Value(Value&&)      = default;

  template<typename T>
  Value(const T& x): Any(x)
  {}

  template<typename T>
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
    if(IsArray()) return AsArray()[idx];
    else
      return *this;
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
    return static_cast<T>(std::get<Number>(*this));
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
    Read(src);
  }

  void Read(std::istream& src)
  {
    switch((src >> std::ws).peek()) {
      case '"': {
        String value;
        src >> std::quoted(value);
        *this = std::move(value);
      } break;
      case '[': {
        Array value;
        for(src.ignore(1); (src >> std::ws).peek() != ']';) {
          value.emplace_back().Read(src);
          if((src >> std::ws).peek() == ',') src.ignore(1);
        }
        src.ignore(1);
        *this = std::move(value);
      } break;
      case '{': {
        Object value;
        for(src.ignore(1); (src >> std::ws).peek() != '}';) {
          std::stringbuf key;
          src.ignore(std::numeric_limits<std::streamsize>::max(), '"')
            .get(key, '"')
            .ignore(std::numeric_limits<std::streamsize>::max(), ':');
          value[key.str()].Read(src);
          if((src >> std::ws).peek() == ',') src.ignore(1);
        }
        src.ignore(1);
        *this = std::move(value);
      } break;
      default: {
        if(
          std::isdigit(src.peek()) || src.peek() == '.' ||
          src.peek() == '-') {
          Number value;
          src >> value;
          *this = value;
        } else if(std::isalpha(src.peek())) {
          String value;
          for(; isalpha(src.peek()); src.ignore())
            value.push_back(static_cast<char>(src.peek()));

          if(value == "null") *this = {};
          else if(value == "true")
            *this = true;
          else if(value == "false")
            *this = false;
          else
            *this = value;
        } else
          *this = {};
      } break;
    }
  }
};

} // namespace vd::Json

#ifdef __clang__
  #pragma clang diagnostic pop
#endif
