#include "SmaaCommon.hlsli"

struct WeightVSOut {
  float4 pos : SV_POSITION;
  float2 uv0 : TEXCOORD0;
  float2 pixcoord : TEXCOORD1;
  float4 offset[3] : TEXCOORD2;
};

WeightVSOut MainVS(uint index : SV_VertexID)
{
  VSOut base = FullscreenVS(index);
  WeightVSOut ret;
  ret.pos = base.pos;
  ret.uv0 = base.uv0;
  SMAABlendingWeightCalculationVS(ret.uv0, ret.pixcoord, ret.offset);
  return ret;
}

float4 MainPS(WeightVSOut input): SV_TARGET
{
  return SMAABlendingWeightCalculationPS(
    input.uv0, input.pixcoord, input.offset, AaEdges, AaArea, AaSearch,
    float4(0, 0, 0, 0));
}
