#include "Common.hlsli"

struct VSOut {
  float4 pos : SV_POSITION;
  float2 uv0 : TEXCOORD0;
};

VSOut MainVS(uint index : SV_VertexID)
{
  VSOut ret = (VSOut)0;

  ret.uv0 = float2((index << 1) & 2, index & 2);
  ret.pos = float4(ret.uv0 * 2.f - 1.f, 0.f, 1.f);

  return ret;
}

float4 MainPS(VSOut input): SV_TARGET
{
  float4 ret = 0;

  // Temp
  float4 gb0 = GBuf0.Sample(smpLinearWrap, input.uv0);
  float4 gb1 = GBuf1.Sample(smpLinearWrap, input.uv0);
  float4 gb2 = GBuf2.Sample(smpLinearWrap, input.uv0);

  if(input.uv0.x < 0.25) {
    ret = float4(1, 0, 0, 1);
  } else if(input.uv0.x < 0.50) {
    ret = float4(gb0.rgb, 1);
  } else if(input.uv0.x < 0.75) {
    ret = float4(gb1.rgb, 1);
  } else {
    ret = float4(input.uv0, 0, 1);
  }

  return ret;
}
