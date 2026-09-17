#include "Common.hlsli"

// Compact FXAA after FXAA 3.11 quality (luma contrast + edge search).
// Runs on post-tonemap LDR in a single fullscreen pass.

struct VSOut {
  float4 pos : SV_POSITION;
  float2 uv0 : TEXCOORD0;
};

VSOut MainVS(uint index : SV_VertexID)
{
  VSOut ret = (VSOut)0;
  ret.uv0 = float2((index << 1) & 2, index & 2);
  ret.pos = ClipPosFromUvTopLeft(ret.uv0);
  return ret;
}

float Luma(float3 rgb)
{
  return dot(rgb, float3(0.299, 0.587, 0.114));
}

float4 MainPS(VSOut input): SV_TARGET
{
  Scene  scene = GetScene();
  float2 uv    = input.uv0;
  float2 texel = scene.aaInvSize;

  float3 rgbM = SceneColor.SampleLevel(smpLinearClamp, uv, 0).rgb;
  float  lumaM = Luma(rgbM);

  float lumaN =
    Luma(SceneColor.SampleLevel(smpLinearClamp, uv + float2(0, -texel.y), 0).rgb);
  float lumaS =
    Luma(SceneColor.SampleLevel(smpLinearClamp, uv + float2(0, texel.y), 0).rgb);
  float lumaE =
    Luma(SceneColor.SampleLevel(smpLinearClamp, uv + float2(texel.x, 0), 0).rgb);
  float lumaW =
    Luma(SceneColor.SampleLevel(smpLinearClamp, uv + float2(-texel.x, 0), 0).rgb);

  float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
  float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
  float range   = lumaMax - lumaMin;

  // Early out on flat regions (FXAA edge threshold).
  if(range < max(0.0312, lumaMax * 0.125))
    return float4(rgbM, 1.0);

  float lumaNW = Luma(
    SceneColor
      .SampleLevel(smpLinearClamp, uv + float2(-texel.x, -texel.y), 0)
      .rgb);
  float lumaNE = Luma(
    SceneColor
      .SampleLevel(smpLinearClamp, uv + float2(texel.x, -texel.y), 0)
      .rgb);
  float lumaSW = Luma(
    SceneColor
      .SampleLevel(smpLinearClamp, uv + float2(-texel.x, texel.y), 0)
      .rgb);
  float lumaSE = Luma(
    SceneColor
      .SampleLevel(smpLinearClamp, uv + float2(texel.x, texel.y), 0)
      .rgb);

  float2 dir;
  dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
  dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

  float dirReduce = max(
    (lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * 0.5), 1.0 / 128.0);
  float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
  dir = clamp(dir * rcpDirMin, float2(-8.0, -8.0), float2(8.0, 8.0)) *
        texel;

  float3 rgbA =
    0.5 * (SceneColor.SampleLevel(smpLinearClamp, uv + dir * (1.0 / 3.0 - 0.5), 0)
             .rgb +
           SceneColor.SampleLevel(smpLinearClamp, uv + dir * (2.0 / 3.0 - 0.5), 0)
             .rgb);
  float3 rgbB =
    rgbA * 0.5 +
    0.25 * (SceneColor.SampleLevel(smpLinearClamp, uv + dir * -0.5, 0).rgb +
            SceneColor.SampleLevel(smpLinearClamp, uv + dir * 0.5, 0).rgb);

  float lumaB = Luma(rgbB);
  if(lumaB < lumaMin || lumaB > lumaMax)
    return float4(rgbA, 1.0);
  return float4(rgbB, 1.0);
}
