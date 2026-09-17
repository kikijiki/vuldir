#pragma once

#include "Common.hlsli"

// Jimenez SMAA 1x (HIGH), adapted to vuldir bindless static samplers.
#define SMAA_RT_METRICS float4(GetScene().aaInvSize, GetScene().aaSize)
#define SMAA_PRESET_HIGH
#define SMAA_CUSTOM_SL

#define SMAATexture2D(tex) Texture2D<float4> tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) \
  tex.SampleLevel(smpLinearClamp, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) \
  tex.SampleLevel(smpNearestClamp, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) \
  tex.SampleLevel(                           \
    smpLinearClamp, coord + float2(offset) * SMAA_RT_METRICS.xy, 0)
#define SMAASample(tex, coord) tex.Sample(smpLinearClamp, coord)
#define SMAASamplePoint(tex, coord) tex.Sample(smpNearestClamp, coord)
#define SMAASampleOffset(tex, coord, offset) \
  tex.Sample(smpLinearClamp, coord + float2(offset) * SMAA_RT_METRICS.xy)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]

#include "SMAA.hlsli"

struct VSOut {
  float4 pos : SV_POSITION;
  float2 uv0 : TEXCOORD0;
};

VSOut FullscreenVS(uint index : SV_VertexID)
{
  VSOut ret = (VSOut)0;
  ret.uv0 = float2((index << 1) & 2, index & 2);
  ret.pos = ClipPosFromUvTopLeft(ret.uv0);
  return ret;
}
