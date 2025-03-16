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

// PBR helper functions
float3 FresnelSchlick(float cosTheta, float3 F0)
{
  return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float DistributionGGX(float3 N, float3 H, float roughness)
{
  float a      = roughness * roughness;
  float a2     = a * a;
  float NdotH  = max(dot(N, H), 0.0);
  float NdotH2 = NdotH * NdotH;

  float denom = (NdotH2 * (a2 - 1.0) + 1.0);
  return a2 / (3.14159 * denom * denom);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
  float NdotV = max(dot(N, V), 0.0);
  float NdotL = max(dot(N, L), 0.0);
  float k     = (roughness + 1.0) * (roughness + 1.0) / 8.0;

  float ggx1 = NdotV / (NdotV * (1.0 - k) + k);
  float ggx2 = NdotL / (NdotL * (1.0 - k) + k);

  return ggx1 * ggx2;
}

float4 MainPS(VSOut input): SV_TARGET
{
  // Sample GBuffer
  float4 gb0 =
    GBuf0.Sample(smpLinearWrap, input.uv0); // Position + roughness
  float4 gb1 =
    GBuf1.Sample(smpLinearWrap, input.uv0); // Normal + metallic
  float4 gb2 = GBuf2.Sample(smpLinearWrap, input.uv0); // Albedo

  float3 worldPos  = gb0.xyz;
  float  roughness = gb0.w;
  float3 N         = normalize(gb1.xyz);
  float  metallic  = gb1.w;
  float3 albedo    = gb2.rgb;

  // View direction
  float3 V = normalize(GetScene().cameraPosition - worldPos);

  // Material base reflectivity
  float3 F0 = lerp(0.04, albedo, metallic);

  // Initialize lighting with ambient only
  float3 color = GetScene().ambientColor * albedo;

  // ----- Directional light calculation -----
  {
    // Directional light comes from the opposite direction we're lighting
    // towards
    float3 L = -normalize(GetScene().directionalLightDirection);
    float3 H = normalize(V + L);

    // Calculate light contribution
    float3 F   = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float  NDF = DistributionGGX(N, H, roughness);
    float  G   = GeometrySmith(N, V, L, roughness);

    float3 numerator  = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    float3 specular   = numerator / max(denominator, 0.001);

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);

    float3 lightColor = GetScene().directionalLightColor *
                        GetScene().directionalLightIntensity;
    color += (kD * albedo / 3.14159 + specular) * lightColor * NdotL;
  }

  // Add point light from original code (kept for compatibility)
  // {
  //   float3 lightPos = float3(5, 5, 5);
  //   float3 lightColor = float3(23, 21, 18);
  //   float3 L = normalize(lightPos - worldPos);
  //   float3 H = normalize(V + L);
  //   float distance = length(lightPos - worldPos);
  //   float attenuation = 1.0 / (distance * distance);
  //   float3 radiance = lightColor * attenuation;

  //   float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
  //   float NDF = DistributionGGX(N, H, roughness);
  //   float G = GeometrySmith(N, V, L, roughness);

  //   float3 numerator = NDF * G * F;
  //   float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
  //   float3 specular = numerator / max(denominator, 0.001);

  //   float3 kS = F;
  //   float3 kD = (1.0 - kS) * (1.0 - metallic);

  //   float NdotL = max(dot(N, L), 0.0);
  //   color += (kD * albedo / 3.14159 + specular) * radiance * NdotL;
  // }

  // Gamma correction
  color = color / (color + 1.0);
  color = pow(color, 1.0 / 2.2);

  return float4(color, 1.0);
}
