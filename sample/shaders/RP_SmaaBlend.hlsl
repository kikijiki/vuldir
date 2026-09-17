#include "SmaaCommon.hlsli"

struct BlendVSOut {
  float4 pos : SV_POSITION;
  float2 uv0 : TEXCOORD0;
  float4 offset : TEXCOORD1;
};

BlendVSOut MainVS(uint index : SV_VertexID)
{
  VSOut base = FullscreenVS(index);
  BlendVSOut ret;
  ret.pos = base.pos;
  ret.uv0 = base.uv0;
  SMAANeighborhoodBlendingVS(ret.uv0, ret.offset);
  return ret;
}

float4 MainPS(BlendVSOut input): SV_TARGET
{
  return SMAANeighborhoodBlendingPS(
    input.uv0, input.offset, SceneColor, AaBlend);
}
