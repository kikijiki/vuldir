#include "Common.hlsli"

struct VSOut {
  float4 pos : SV_POSITION;
  float3 nrm : NORMAL;
  float3 tan : TANGENT;
  float2 uv0 : TEXCOORD0;
};

VSOut MainVS(VSIn input)
{
  VSOut ret = (VSOut)0;

  float4   wpos      = mul(GetMesh().world, input.GetPosition());
  float4x4 worldView = GetMesh().world * GetScene().view;

  ret.pos = mul(GetScene().viewProjection, wpos);
  ret.nrm = mul((float3x3)worldView, input.GetNormal());
  ret.tan = mul((float3x3)worldView, input.GetTangent());
  ret.uv0 = input.GetUV0();

  return ret;
}

struct PSOut {
  float4 g0 : SV_TARGET0; // Position
  float4 g1 : SV_TARGET1; // Normal
  float4 g2 : SV_TARGET2; // Color
};

PSOut MainPS(VSOut input)
{
  PSOut ret = (PSOut)0;

  //  float4 sColor  = ColorTex.Sample(smpLinearWrap, input.uv0);
  //  float3 sNormal = NormalTex.Sample(smpLinearWrap, input.uv0).xyz;
  //
  //  float3   N   = normalize(input.nrm);
  //  float3   T   = normalize(input.tan);
  //  float3   B   = cross(N, T);
  //  float3x3 TBN = float3x3(T, B, N);
  //
  //  float4 color  = sColor;
  //  float3 normal = mul(normalize(sNormal * 2 - 1), TBN);
  //
  //  ret.g0 = input.pos;
  //  ret.g1 = float4(normal, 0);
  //  ret.g2 = float4(color.rgb, 0);

  ret.g0 = float4(1, 0, 0, 1);
  ret.g1 = float4(0, 1, 0, 1);
  ret.g2 = float4(0, 0, 1, 1);

  return ret;
}
