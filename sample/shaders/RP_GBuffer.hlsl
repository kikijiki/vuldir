#include "Common.hlsli"

struct VSOut {
  float4 pos : SV_POSITION;
  float3 nrm : NORMAL;
  float3 tan : TANGENT;
  float tangentSign : TEXCOORD2;
  float2 uv0 : TEXCOORD0;
  float3 worldPos : TEXCOORD1;
};

VSOut MainVS(VSIn input)
{
  VSOut ret = (VSOut)0;

  float4   wpos      = mul(GetPrim().world, input.GetPosition());
  float4x4 worldView = GetPrim().world * GetScene().view;

  ret.pos = MeshClipPos(mul(GetScene().viewProjection, wpos));
  ret.nrm =
    normalize(mul((float3x3)GetPrim().world, input.GetNormal()));
  ret.tan =
    normalize(mul((float3x3)GetPrim().world, input.GetTangent()));
  ret.tangentSign = input.GetTangentSign();
  ret.uv0      = input.GetUV0();
  ret.worldPos = wpos.xyz;

  return ret;
}

struct PSOut {
  float4 g0 : SV_TARGET0; // Position + roughness
  float4 g1 : SV_TARGET1; // Normal + metallic
  float4 g2 : SV_TARGET2; // Albedo + AO
  float4 g3 : SV_TARGET3; // Emissive
};

PSOut MainPS(VSOut input)
{
  PSOut    ret      = (PSOut)0;
  Material material = GetMaterial();

  // glTF: albedo = baseColorFactor * baseColorTexture (or factor alone).
  float4 baseColor = material.baseColorFactor;
  if(HasColorTex()) {
    baseColor *= ColorTex.Sample(smpLinearWrap, input.uv0);
  }

  float3 normal = input.nrm;
  if(HasNormalTex()) {
    float3 T = input.tan;
    // DamagedHelmet (and many glTFs) ship normal maps without tangents.
    // normalize(0) gives NaN and paints the whole mesh gray, skip in that case.
    if(dot(T, T) > 1e-6) {
      float3 sNormal = NormalTex.Sample(smpLinearWrap, input.uv0).xyz;
      float3 N       = normalize(input.nrm);
      T              = normalize(T);
      float3   B     = cross(N, T) * input.tangentSign;
      float3x3 TBN   = float3x3(T, B, N);
      normal         = mul(normalize(sNormal * 2 - 1), TBN);
    }
  }

  // Always start from material factors; multiply by MR texture .bg when present.
  float metallic  = material.metallic;
  float roughness = material.roughness;
  if(HasMetallicRoughnessText()) {
    float2 mr = MRTex.Sample(smpLinearWrap, input.uv0).bg;
    metallic *= mr.x;
    roughness *= mr.y;
  }

  float ao = material.aoStrength;
  if(HasAOTex()) { ao *= AOTex.Sample(smpLinearWrap, input.uv0).r; }

  float3 emissive = material.emissiveFactor;
  if(HasEmissiveTex()) {
    emissive *= EmissiveTex.Sample(smpLinearWrap, input.uv0).rgb;
  }

  ret.g0 = float4(input.worldPos, roughness);
  ret.g1 = float4(normal, metallic);
  ret.g2 = float4(baseColor.rgb, ao);
  ret.g3 = float4(emissive, 1.0);

  return ret;
}
