#include "Common.hlsli"

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

float4 MainPS(VSOut input): SV_TARGET
{
  Scene scene = GetScene();
  return SceneColor.SampleLevel(smpNearestClamp, input.uv0, 0);
}
