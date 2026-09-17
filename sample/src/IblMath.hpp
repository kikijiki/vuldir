#pragma once

#include "vuldir/core/Math.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vd::sample {

inline Float2 DirectionToEquirectUV(Float3 direction)
{
  direction       = mt::Norm(direction);
  const f32 phi   = std::atan2(direction[2], direction[0]);
  const f32 theta = std::asin(std::clamp(direction[1], -1.f, 1.f));
  return {
    phi / (2.f * std::numbers::pi_v<f32>)+0.5f,
    0.5f - theta / std::numbers::pi_v<f32>};
}

inline Float3 EquirectUVToDirection(Float2 uv)
{
  const f32 phi      = (uv[0] - 0.5f) * 2.f * std::numbers::pi_v<f32>;
  const f32 theta    = (0.5f - uv[1]) * std::numbers::pi_v<f32>;
  const f32 cosTheta = std::cos(theta);
  return mt::Norm(
    Float3{
      cosTheta * std::cos(phi), std::sin(theta),
      cosTheta * std::sin(phi)});
}

inline Float3 CameraRay(
  Float3 cameraPosition, Float3 cameraTarget, Float2 ndc, f32 aspect,
  f32 tanHalfFovY)
{
  const Float3 forward =
    mt::Norm(mt::Sub(cameraTarget, cameraPosition));
  const Float3 cameraX =
    mt::Norm(mt::Cross(Float3{0.f, 1.f, 0.f}, forward));
  const Float3 cameraY = mt::Cross(forward, cameraX);
  return mt::Norm(
    mt::Add(
      mt::Add(forward, mt::Mul(cameraX, ndc[0] * aspect * tanHalfFovY)),
      mt::Mul(cameraY, ndc[1] * tanHalfFovY)));
}

} // namespace vd::sample
