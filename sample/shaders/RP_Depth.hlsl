#include "Common.hlsli"

struct VSOut {
  float4 pos : SV_POSITION;
};

VSOut MainVS(VSIn input)
{
  VSOut ret = (VSOut)0;

  float4 wpos = mul(GetMesh().world, input.GetPosition());
  ret.pos     = mul(GetScene().viewProjection, wpos);

  return ret;
}
