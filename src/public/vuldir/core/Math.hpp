#pragma once

#include "vuldir/core/Definitions.hpp"
#include "vuldir/core/STL.hpp"
#include "vuldir/core/Types.hpp"
#include "vuldir/core/Uti.hpp"

namespace vd {

template<typename T, u64 S>
struct Vector {
  using value_type = T;

  SArr<T, S> v;

  constexpr Vector()              = default;
  constexpr Vector(const Vector&) = default;
  constexpr Vector(Vector&&)      = default;

  constexpr Vector(T k) { std::fill(v.begin(), v.end(), k); }
  constexpr Vector(T x, T y)
  {
    static_assert(S == 2);
    v = {x, y};
  }
  constexpr Vector(T x, T y, T z)
  {
    static_assert(S == 3);
    v = {x, y, z};
  }
  constexpr Vector(T x, T y, T z, T w)
  {
    static_assert(S == 4);
    v = {x, y, z, w};
  }
  template<u64 S2>
  constexpr Vector(Vector<T, S2> k)
  {
    if constexpr(S2 >= S) {
      std::copy(k.v.begin(), k.v.begin() + S, v.begin());
    } else {
      v = {};
      std::copy(k.v.begin(), k.v.end(), v.begin());
    }
  }
  template<u64 S2>
  constexpr Vector(T x, Vector<T, S2> k)
  {
    static_assert(S2 + 1 == S);
    if constexpr(S == 3) v = {x, k[0], k[1]};
    else
      v = {x, k[0], k[1], k[2]};
  }
  template<u64 S2>
  constexpr Vector(Vector<T, S2> k, T x)
  {
    static_assert(S2 + 1 == S);
    if constexpr(S == 3) v = {k[0], k[1], x};
    else
      v = {k[0], k[1], k[2], x};
  }
  template<u64 S2>
  constexpr Vector(T x, T y, Vector<T, S2> k)
  {
    static_assert(S == 4 && S2 == 2);
    v = {x, y, k[0], k[1]};
  }
  template<u64 S2>
  constexpr Vector(T x, Vector<T, S2> k, T y)
  {
    static_assert(S == 4 && S2 == 2);
    v = {x, k[0], k[1], y};
  }
  template<u64 S2>
  constexpr Vector(Vector<T, S2> k, T x, T y)
  {
    static_assert(S == 4 && S2 == 2);
    v = {k[0], k[1], x, y};
  }
  constexpr Vector(std::initializer_list<T> k)
  {
    v = {};
    std::copy(k.begin(), k.begin() + std::min(S, k.size()), v.begin());
  }

  constexpr Vector& operator=(const Vector&) = default;
  constexpr Vector& operator=(Vector&&)      = default;

  constexpr T  operator[](const u64 idx) const { return v[idx]; }
  constexpr T& operator[](const u64 idx) { return v[idx]; }

  constexpr u64 size() const { return S; }

  // template<u64 S2>
  // constexpr operator Vector<T, S2>() const
  //{
  //  Vector<T, S2> ret;
  //  if constexpr(S >= S2) {
  //    for(u64 i = 0u; i < S2; ++i)
  //      ret[i] = v[i];
  //  } else {
  //    ret = {};
  //    for(u64 i = 0u; i < S; ++i)
  //      ret[i] = v[i];
  //  }
  //  return ret;
  //}

  const T* data() const { return std::data(v); }
};

template<typename T, u64 Size>
struct Matrix {
  Vector<T, Size>                  v[Size];
  constexpr const Vector<T, Size>& operator[](const u64 idx) const
  {
    return v[idx];
  }
  constexpr Vector<T, Size>& operator[](const u64 idx)
  {
    return v[idx];
  }
};

using Float2 = Vector<f32, 2>;
using Float3 = Vector<f32, 3>;
using Float4 = Vector<f32, 4>;

using UInt2 = Vector<u32, 2>;
using UInt3 = Vector<u32, 3>;
using UInt4 = Vector<u32, 4>;

using Int2 = Vector<i32, 2>;
using Int3 = Vector<i32, 3>;
using Int4 = Vector<i32, 4>;

using Float22 = Matrix<f32, 2>;
using Float33 = Matrix<f32, 3>;
using Float44 = Matrix<f32, 4>;
using UInt22  = Matrix<u32, 2>;
using UInt33  = Matrix<u32, 3>;
using UInt44  = Matrix<u32, 4>;
using Int22   = Matrix<i32, 2>;
using Int33   = Matrix<i32, 3>;
using Int44   = Matrix<i32, 4>;

struct Rect {
  Int2  offset;
  UInt2 extent;
};

struct RectF {
  Float2 offset;
  Float2 extent;
};

struct Range32 {
  u32 offset;
  u32 size;
};

// Should it be pass by const ref or pass by value?
// Need to profile.

#define VD_VEC_PARAM  const Vector<T, S>&
#define VD_VEC2_PARAM const Vector<T, 2>&
#define VD_VEC3_PARAM const Vector<T, 3>&
#define VD_VEC4_PARAM const Vector<T, 4>&

#define VD_MTX_PARAM  const Matrix<T, S>&
#define VD_MTX2_PARAM const Matrix<T, 2>&
#define VD_MTX3_PARAM const Matrix<T, 3>&
#define VD_MTX4_PARAM const Matrix<T, 4>&

#define VD_FLOAT2_PARAM const Float2&
#define VD_FLOAT3_PARAM const Float3&
#define VD_FLOAT4_PARAM const Float4&

#define VD_FLOAT22_PARAM const Float22&
#define VD_FLOAT33_PARAM const Float33&
#define VD_FLOAT44_PARAM const Float44&

namespace mt {

template<typename T, u64 S>
inline constexpr Vector<T, S> Add(VD_VEC_PARAM v, T s)
{
  Vector<T, S> ret;
  for(u64 i = 0u; i < S; ++i) ret[i] = v[i] + s;
  return ret;
}

template<typename T, u64 S>
inline constexpr Vector<T, S> Add(VD_VEC_PARAM v1, VD_VEC_PARAM v2)
{
  Vector<T, S> ret;
  for(u64 i = 0u; i < S; ++i) ret[i] = v1[i] + v2[i];
  return ret;
}

template<typename T, u64 S>
inline constexpr Matrix<T, S> Add(VD_MTX_PARAM m, T s)
{
  Matrix<T, S> ret;
  for(u64 i = 0u; i < S; ++i)
    for(u64 j = 0u; j < S; ++j) ret[i][j] = m[i] + s;

  return ret;
}

template<typename T, u64 S>
inline constexpr Matrix<T, S> Add(VD_MTX_PARAM m1, VD_MTX_PARAM m2)
{
  Matrix<T, S> ret;
  for(u64 i = 0u; i < S; ++i)
    for(u64 j = 0u; j < S; ++j) ret[i][j] = m1[i] + m2[j];

  return ret;
}

template<typename T, u64 S>
inline constexpr Vector<T, S> Sub(VD_VEC_PARAM v, T s)
{
  Vector<T, S> ret;
  for(u64 i = 0u; i < S; ++i) ret[i] = v[i] - s;
  return ret;
}

template<typename T, u64 S>
inline constexpr Vector<T, S> Sub(VD_VEC_PARAM v1, VD_VEC_PARAM v2)
{
  Vector<T, S> ret;
  for(u64 i = 0u; i < S; ++i) ret[i] = v1[i] - v2[i];
  return ret;
}

template<typename T, u64 S>
inline constexpr Matrix<T, S> Sub(VD_MTX_PARAM m, T s)
{
  Matrix<T, S> ret;
  for(u64 i = 0u; i < S; ++i)
    for(u64 j = 0u; j < S; ++j) ret[i][j] = m[i] - s;

  return ret;
}

template<typename T, u64 S>
inline constexpr Matrix<T, S> Sub(VD_MTX_PARAM m1, VD_MTX_PARAM m2)
{
  Matrix<T, S> ret;
  for(u64 i = 0u; i < S; ++i)
    for(u64 j = 0u; j < S; ++j) ret[i][j] = m1[i] - m2[j];

  return ret;
}

template<typename T, u64 S>
inline constexpr Vector<T, S> Mul(VD_VEC_PARAM v, T s)
{
  Vector<T, S> ret;
  for(u64 i = 0u; i < S; ++i) ret[i] = v[i] * s;
  return ret;
}

template<typename T, u64 S>
inline constexpr Vector<T, S> Mul(T s, VD_VEC_PARAM v)
{
  return Mul(v, s);
}

template<typename T, u64 S>
inline constexpr Matrix<T, S> Mul(VD_MTX_PARAM m, T s)
{
  Matrix<T, S> ret;
  for(u64 i = 0u; i < S; ++i)
    for(u64 j = 0u; j < S; ++j) ret[i][j] = m[i] * s;
  return ret;
}

template<typename T, u64 S>
inline constexpr Matrix<T, S> Mul(T s, VD_MTX_PARAM m)
{
  return Mul(m, s);
}

template<typename T, u64 S>
inline constexpr Vector<T, S> Mul(VD_MTX_PARAM m, VD_VEC_PARAM v)
{
  Vector<T, S> ret = {};
  for(u64 i = 0u; i < S; ++i)
    for(u64 j = 0u; j < S; ++j) ret[i] += m[i][j] * v[j];
  return ret;
}

template<typename T, u64 S>
inline constexpr Matrix<T, S> Mul(VD_MTX_PARAM m1, VD_MTX_PARAM m2)
{
  Matrix<T, S> ret = {};
  for(u64 i = 0u; i < S; ++i)
    for(u64 j = 0u; j < S; ++j)
      for(u64 k = 0; k < S; k++) ret[i][j] += m1[i][k] * m2[k][j];
  return ret;
}

template<typename T, u64 S>
inline constexpr Matrix<T, S>
Mul(VD_MTX_PARAM m1, VD_MTX_PARAM m2, VD_MTX_PARAM m3)
{
  return Mul(Mul(m1, m2), m3);
}

template<typename T, u64 S>
inline constexpr Matrix<T, S>
Mul(VD_MTX_PARAM m1, VD_MTX_PARAM m2, VD_MTX_PARAM m3, VD_MTX_PARAM m4)
{
  return Mul(Mul(Mul(m1, m2), m3), m4);
}

template<typename T, u64 S>
inline constexpr Vector<T, S> Div(VD_VEC_PARAM v, T s)
{
  Vector<T, S> ret;
  const auto   inv = static_cast<T>(1) / s;
  for(u64 i = 0u; i < S; ++i) ret[i] = v[i] * inv;
  return ret;
}

template<typename T, u64 S>
inline constexpr Vector<T, S> Div(T s, VD_VEC_PARAM v)
{
  return Div(v, s);
}

template<typename T, u64 S>
inline constexpr Matrix<T, S> Div(VD_MTX_PARAM m, T s)
{
  Matrix<T, S> ret;
  const auto   inv = static_cast<T>(1) / s;
  for(u64 i = 0u; i < S; ++i)
    for(u64 j = 0u; j < S; ++j) ret[i][j] = m[i] * inv;
  return ret;
}

template<typename T, u64 S>
inline constexpr T Dot(VD_VEC_PARAM v1, VD_VEC_PARAM v2)
{
  T ret = 0;
  for(u64 i = 0u; i < S; ++i) ret += v1[i] * v2[i];
  return ret;
}

template<typename T>
inline constexpr Vector<T, 3> Cross(VD_VEC3_PARAM v1, VD_VEC3_PARAM v2)
{
  return {
    v1[1] * v2[2] - v1[2] * v2[1], v1[2] * v2[0] - v1[0] * v2[2],
    v1[0] * v2[1] - v1[1] * v2[0]};
}

template<typename T>
inline constexpr Vector<T, 2> Splat2(T v)
{
  return {v, v};
}

template<typename T>
inline constexpr Vector<T, 3> Splat3(T v)
{
  return {v, v, v};
}

template<typename T>
inline constexpr Vector<T, 4> Splat4(T v)
{
  return {v, v, v, v};
}

template<typename T>
inline constexpr Vector<T, 2> Zero2()
{
  return {0, 0};
}

template<typename T>
inline constexpr Vector<T, 3> Zero3()
{
  return {0, 0, 0};
}

template<typename T>
inline constexpr Vector<T, 4> Zero4()
{
  return {0, 0, 0, 0};
}

template<typename T>
inline constexpr Matrix<T, 2> Zero22()
{
  return {Zero2<T>(), Zero2<T>()};
}

template<typename T>
inline constexpr Matrix<T, 3> Zero33()
{
  return {Zero3<T>(), Zero3<T>(), Zero3<T>()};
}

template<typename T>
inline constexpr Matrix<T, 4> Zero44()
{
  return {Zero4<T>(), Zero4<T>(), Zero4<T>(), Zero4<T>()};
}

template<typename T>
inline constexpr Vector<T, 2> One2()
{
  return {1, 1};
}

template<typename T>
inline constexpr Vector<T, 3> One3()
{
  return {1, 1, 1};
}

template<typename T>
inline constexpr Vector<T, 4> One4()
{
  return {1, 1, 1, 1};
}

template<typename T>
inline constexpr Matrix<T, 2> One22()
{
  return {One2<T>(), One2<T>()};
}

template<typename T>
inline constexpr Matrix<T, 3> One33()
{
  return {One3<T>(), Zero3<T>(), Zero3<T>()};
}

template<typename T>
inline constexpr Matrix<T, 4> One44()
{
  return {One4<T>(), One4<T>(), One4<T>(), One4<T>()};
}

template<typename T>
inline constexpr Vector<T, 2> X2()
{
  return {1, 0};
}

template<typename T>
inline constexpr Vector<T, 3> X3()
{
  return {1, 0, 0};
}

template<typename T>
inline constexpr Vector<T, 4> X4()
{
  return {1, 0, 0, 0};
}

template<typename T>
inline constexpr Vector<T, 2> Y2()
{
  return {0, 1};
}

template<typename T>
inline constexpr Vector<T, 3> Y3()
{
  return {0, 1, 0};
}

template<typename T>
inline constexpr Vector<T, 4> Y4()
{
  return {0, 1, 0, 0};
}

template<typename T>
inline constexpr Vector<T, 3> Z3()
{
  return {0, 0, 1};
}

template<typename T>
inline constexpr Vector<T, 4> Z4()
{
  return {0, 0, 1, 0};
}

template<typename T>
inline constexpr Vector<T, 2> W4()
{
  return {0, 0, 0, 1};
}

template<typename T, u64 S>
inline constexpr Vector<T, S> Negate(VD_VEC_PARAM v)
{
  return Mul(v, -1);
}

template<typename T, u64 S>
inline constexpr Vector<T, S> Abs(VD_VEC_PARAM v)
{
  Vector<T, S> ret;
  for(u64 i = 0u; i < S; ++i) ret[i] = std::abs(v[i]);
  return ret;
}

template<typename T, u64 S>
inline constexpr Matrix<T, S> Abs(VD_MTX_PARAM v)
{
  Matrix<T, S> ret;
  for(u64 i = 0u; i < S; ++i)
    for(u64 j = 0u; j < S; ++j) ret[i][j] = std::abs(v[i][j]);
  return ret;
}

template<typename T, u64 S>
inline constexpr T LengthSq(VD_VEC_PARAM v)
{
  T len = 0;
  for(u64 i = 0u; i < S; ++i) len += v[i] * v[i];
  return len;
}

template<typename T, u64 S>
inline constexpr T Length(VD_VEC_PARAM v)
{
  return std::sqrt(LengthSq(v));
}

template<typename T, u64 S>
inline constexpr Vector<T, S> Norm(VD_VEC_PARAM v)
{
  return Div(v, Length(v));
}

template<typename T>
inline constexpr T Identity()
{
  return T{};
}

template<>
inline constexpr Float22 Identity<Float22>()
{
  return {{{1, 0}, {0, 1}}};
}

template<>
inline constexpr Float33 Identity<Float33>()
{
  return {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
}

template<>
inline constexpr Float44 Identity<Float44>()
{
  return {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
}

inline constexpr Float44
LookAt(VD_FLOAT3_PARAM eye, VD_FLOAT3_PARAM at, VD_FLOAT3_PARAM up)
{
  const auto z = Norm(Sub(at, eye));
  const auto x = Norm(Cross(up, z));
  const auto y = Cross(z, x);

  auto ret  = Identity<Float44>();
  ret[0][0] = x[0];
  ret[1][0] = x[1];
  ret[2][0] = x[2];
  ret[0][1] = y[0];
  ret[1][1] = y[1];
  ret[2][1] = y[2];
  ret[0][2] = -z[0];
  ret[1][2] = -z[1];
  ret[2][2] = -z[2];
  ret[3][0] = -Dot(x, eye);
  ret[3][1] = -Dot(y, eye);
  ret[3][2] = Dot(z, eye);
  return ret;
}

inline /*constexpr*/ Float44
PerspectiveFov(f32 fov, f32 aspect, f32 znear, f32 zfar)
{
  const auto height = std::cos(fov * .5f) / std::sin(fov * .5f);
  const auto width  = height / aspect;

  Float44 ret = {};
  ret[0][0]   = width;
  ret[1][1]   = height;
  ret[2][2]   = -zfar / (zfar - znear);
  ret[2][3]   = -1.0f;
  ret[3][2]   = -(zfar * znear) / (zfar - znear);
  return ret;
}

inline constexpr Float44 Translation(Float3 v)
{
  Float44 ret = Identity<Float44>();
  ret[3][0]   = v[0];
  ret[3][1]   = v[1];
  ret[3][2]   = v[2];
  return ret;
}

inline constexpr Float44 Scaling(Float3 v)
{
  Float44 ret = Identity<Float44>();
  ret[0][0]   = v[0];
  ret[1][1]   = v[1];
  ret[2][2]   = v[2];
  return ret;
}

inline constexpr Float44 Scaling(f32 v) { return Scaling({v, v, v}); }

inline /*constexpr*/ Float44 RotationX(f32 v)
{
  Float44 ret = Identity<Float44>();

  ret[1][1] = std::cos(v);
  ret[1][2] = -std::sin(v);
  ret[2][1] = std::sin(v);
  ret[2][2] = std::cos(v);

  return ret;
}

inline /*constexpr*/ Float44 RotationY(f32 v)
{
  Float44 ret = Identity<Float44>();

  ret[0][0] = std::cos(v);
  ret[0][2] = std::sin(v);
  ret[2][0] = -std::sin(v);
  ret[2][2] = std::cos(v);

  return ret;
}

inline /*constexpr*/ Float44 RotationZ(f32 v)
{
  Float44 ret = Identity<Float44>();

  ret[0][0] = std::cos(v);
  ret[0][1] = -std::sin(v);
  ret[1][0] = std::sin(v);
  ret[1][1] = std::cos(v);

  return ret;
}

inline /*constexpr*/ Float44 Rotation(Float3 v)
{
  auto rx = RotationX(v[0]);
  auto ry = RotationY(v[1]);
  auto rz = RotationZ(v[2]);

  return Mul(rx, ry, rz);
}

// IEEE-754 float16 conversion functions
inline u16 Float32ToFloat16(f32 f)
{
  u32 x        = *reinterpret_cast<u32*>(&f);
  u32 sign     = (x >> 31) & 0x1;
  u32 exp      = (x >> 23) & 0xFF;
  u32 mantissa = x & 0x7FFFFF;

  if(exp == 0xFF) {     // Handle infinity and NaN
    if(mantissa == 0) { // Infinity
      return static_cast<u16>((sign << 15) | 0x7C00);
    } else { // NaN
      return static_cast<u16>((sign << 15) | 0x7C00 | (mantissa >> 13));
    }
  }

  // Convert normalized numbers
  int newExp = exp - 127 + 15;
  if(newExp >= 31) { // Overflow to infinity
    return static_cast<u16>((sign << 15) | 0x7C00);
  } else if(newExp <= 0) { // Underflow to 0 or denormal
    if(newExp < -10) {
      return static_cast<u16>(sign << 15); // Zero
    }
    // Denormal
    mantissa = (mantissa | 0x800000) >> (14 - newExp);
    return static_cast<u16>((sign << 15) | mantissa);
  }

  // Regular normalized number
  return static_cast<u16>(
    (sign << 15) | (newExp << 10) | (mantissa >> 13));
}

inline f32 Float16ToFloat32(u16 h)
{
  u32 sign     = (h >> 15) & 0x1;
  u32 exp      = (h >> 10) & 0x1F;
  u32 mantissa = h & 0x3FF;

  u32 f = 0;
  if(exp == 0) { // Zero or denormal
    if(mantissa == 0) {
      f = sign << 31;
    } else { // Denormal
      exp = 0;
      while((mantissa & 0x400) == 0) {
        mantissa <<= 1;
        exp++;
      }
      mantissa &= 0x3FF;
      f = (sign << 31) | ((127 - 15 - exp) << 23) | (mantissa << 13);
    }
  } else if(exp == 0x1F) { // Infinity or NaN
    f = (sign << 31) | (0xFF << 23) | (mantissa << 13);
  } else { // Normalized number
    f = (sign << 31) | ((exp + (127 - 15)) << 23) | (mantissa << 13);
  }

  return *reinterpret_cast<f32*>(&f);
}

} // namespace mt
} // namespace vd
