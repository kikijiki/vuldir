#include "Common.hlsli"

struct VSOut {
  float4 pos : SV_POSITION;
  float3 nrm : NORMAL;
  float3 tan : TANGENT;
  float2 uv0 : TEXCOORD0;
  float3 worldPos : TEXCOORD1;
};

VSOut MainVS(VSIn input)
{
  VSOut ret = (VSOut)0;

  float4   wpos      = mul(GetPrim().world, input.GetPosition());
  float4x4 worldView = GetPrim().world * GetScene().view;

  ret.pos = mul(GetScene().viewProjection, wpos);
  ret.nrm =
    normalize(mul((float3x3)GetPrim().world, input.GetNormal()));
  ret.tan =
    normalize(mul((float3x3)GetPrim().world, input.GetTangent()));
  ret.uv0      = input.GetUV0();
  ret.worldPos = wpos.xyz;

  return ret;
}

struct PSOut {
  float4 g0 : SV_TARGET0; // Position + roughness
  float4 g1 : SV_TARGET1; // Normal + metallic
  float4 g2 : SV_TARGET2; // Albedo
};

PSOut MainPS(VSOut input)
{
  PSOut    ret      = (PSOut)0;
  Material material = GetMaterial();

  // Sample textures
  float4 baseColor =
    HasColorTex() ? ColorTex.Sample(smpLinearWrap, input.uv0) : 1.0;
  float3 normal = input.nrm;

  if(HasNormalTex()) {
    float3   sNormal = NormalTex.Sample(smpLinearWrap, input.uv0).xyz;
    float3   N       = normalize(input.nrm);
    float3   T       = normalize(input.tan);
    float3   B       = cross(N, T);
    float3x3 TBN     = float3x3(T, B, N);
    normal           = mul(normalize(sNormal * 2 - 1), TBN);
  }

  float roughness = 0;
  float metallic  = 0;

  if(HasMetallicRoughnessText()) {
    // The metalness values are sampled from the B channel.
    // The roughness values are sampled from the G channel.
    float2 mr = MRTex.Sample(smpLinearWrap, input.uv0).bg;
    metallic  = mr.x * material.metallic;
    roughness = mr.y * material.roughness;
  }

  ret.g0 = float4(input.worldPos, roughness);
  ret.g1 = float4(normal, metallic);
  ret.g2 = baseColor;

  return ret;
}
