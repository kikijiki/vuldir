#include "SmaaCommon.hlsli"

struct EdgeVSOut {
  float4 pos : SV_POSITION;
  float2 uv0 : TEXCOORD0;
  float4 offset[3] : TEXCOORD1;
};

EdgeVSOut MainVS(uint index : SV_VertexID)
{
  VSOut base = FullscreenVS(index);
  EdgeVSOut ret;
  ret.pos = base.pos;
  ret.uv0 = base.uv0;
  SMAAEdgeDetectionVS(ret.uv0, ret.offset);
  return ret;
}

float4 MainPS(EdgeVSOut input): SV_TARGET
{
  float2 edges = SMAALumaEdgeDetectionPS(
    input.uv0, input.offset, SceneColor);
  return float4(edges, 0.0, 1.0);
}
