#pragma once

inline float2 unpackHalf2(in uint v)
{
  float2 ret;
  ret.x = f16tof32(v & 0xFFFFu);
  ret.y = f16tof32(v >> 16u);
  return ret;
}

inline float3 unpackUnitVector(in uint v)
{
  float3 ret;
  ret.x = (float)((v >> 0u) & 0xFF) / 255.0 * 2 - 1;
  ret.y = (float)((v >> 8u) & 0xFF) / 255.0 * 2 - 1;
  ret.z = (float)((v >> 16u) & 0xFF) / 255.0 * 2 - 1;
  return normalize(ret);
}

// NDC Y conventions per backend (-fvk-invert-y is not used):
//   SPIR-V / Vulkan: Y+ down, UV top-left -> (-1,-1).
//   DXIL / D3D:      Y+ up,  UV top-left -> (-1,+1).
//
// DX mesh/IBL need VD_MESH_CLIP_Y = -1 so viewProjection matches the D3D
// UI space (Wine/vkd3d). Vulkan leaves it at 1.
#ifdef SPIRV
static const float VD_MESH_CLIP_Y = 1.0;
#else
static const float VD_MESH_CLIP_Y = -1.0;
#endif

inline float2 NdcFromUvTopLeft(float2 uv)
{
#ifdef SPIRV
  return uv * 2.0 - 1.0;
#else
  return float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
#endif
}

inline float4 ClipPosFromUvTopLeft(float2 uv)
{
  float2 ndc = NdcFromUvTopLeft(uv);
  return float4(ndc, 0.0, 1.0);
}

inline float4 MeshClipPos(float4 clipPos)
{
  clipPos.y *= VD_MESH_CLIP_Y;
  return clipPos;
}

inline float2 IblNdcFromUv(float2 uv)
{
  float2 ndc = NdcFromUvTopLeft(uv);
  ndc.y *= VD_MESH_CLIP_Y;
  return ndc;
}

inline float IblYaw(float yaw) { return yaw * VD_MESH_CLIP_Y; }
