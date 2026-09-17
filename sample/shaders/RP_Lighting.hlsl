#include "Common.hlsli"

static const float PI = 3.14159265;

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

float3 FresnelSchlick(float cosTheta, float3 F0)
{
  return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3
FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
  return F0 + (max(1.0 - roughness, F0) - F0) *
                pow(saturate(1.0 - cosTheta), 5.0);
}

float DistributionGGX(float3 N, float3 H, float roughness)
{
  float a      = roughness * roughness;
  float a2     = a * a;
  float NdotH  = max(dot(N, H), 0.0);
  float NdotH2 = NdotH * NdotH;

  float denom = (NdotH2 * (a2 - 1.0) + 1.0);
  return a2 / (PI * denom * denom);
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

// Direction -> top-down equirectangular UV (+Y at V=0).
float2 DirToEquirect(float3 dir)
{
  float3 d     = normalize(dir);
  float  phi   = atan2(d.z, d.x);
  float  theta = asin(clamp(d.y, -1.0, 1.0));
  return float2(phi / (2.0 * PI) + 0.5, 0.5 - theta / PI);
}

float3 RotateY(float3 d, float yaw)
{
  float cy = cos(yaw);
  float sy = sin(yaw);
  return float3(cy * d.x + sy * d.z, d.y, -sy * d.x + cy * d.z);
}

float3 DisplayMap(float3 color, float exposure, float gamma)
{
  color *= max(exposure, 0.001);
  color = color / (color + 1.0);
  return pow(saturate(color), 1.0 / max(gamma, 0.01));
}

float4 MainPS(VSOut input): SV_TARGET
{
  Scene scene = GetScene();

  float4 gb0 = GBuf0.Sample(smpLinearClamp, input.uv0);
  float4 gb1 = GBuf1.Sample(smpLinearClamp, input.uv0);
  float4 gb2 = GBuf2.Sample(smpLinearClamp, input.uv0);
  float4 gb3 = GBuf3.Sample(smpLinearClamp, input.uv0);

  // Unwritten G-buffer: draw the IBL environment behind the model. Rebuild a
  // world-space camera ray from the pixel's clip-space position.
  if(dot(gb1.xyz, gb1.xyz) < 1e-6) {
    float3 background = 0;
    if(
      scene.debugView == 0 && scene.iblEnabled != 0 &&
      scene.environmentMapIdx >= 0) {
      float2 ndc     = IblNdcFromUv(input.uv0);
      float3 forward = normalize(-scene.cameraPosition);
      float3 cameraX = normalize(cross(float3(0.0, 1.0, 0.0), forward));
      float3 cameraY = cross(forward, cameraX);
      float3 ray     = normalize(
        forward +
        cameraX * ndc.x * scene.cameraAspect * scene.cameraTanHalfFovY +
        cameraY * ndc.y * scene.cameraTanHalfFovY);
      ray = RotateY(ray, IblYaw(scene.iblRotationYaw));
      background =
        EnvironmentMap
          .SampleLevel(smpLinearWrap, DirToEquirect(ray), 0.0)
          .rgb *
        scene.iblIntensity;
    }
    return float4(
      DisplayMap(background, scene.exposure, scene.gamma), 1.0);
  }

  float3 worldPos  = gb0.xyz;
  float  roughness = saturate(gb0.w);
  float3 N         = normalize(gb1.xyz);
  float  metallic  = saturate(gb1.w);
  float3 albedo    = gb2.rgb;
  float  ao        = saturate(gb2.a);
  float3 emissive  = gb3.rgb;

  // Debug views (skip lighting)
  if(scene.debugView == 1) return float4(albedo, 1.0);
  if(scene.debugView == 2) return float4(N * 0.5 + 0.5, 1.0);
  if(scene.debugView == 3) return float4(roughness.xxx, 1.0);
  if(scene.debugView == 4) return float4(metallic.xxx, 1.0);

  float3 V     = normalize(scene.cameraPosition - worldPos);
  float  NdotV = max(dot(N, V), 0.0);
  float3 F0    = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

  float3 color = 0;

  // ----- Image-based lighting (split-sum) -----
  if(
    scene.iblEnabled != 0 && scene.irradianceMapIdx >= 0 &&
    scene.brdfLutIdx >= 0 && scene.prefilteredMapIdx >= 0) {
    float3 F  = FresnelSchlickRoughness(NdotV, F0, roughness);
    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    float3 Nibl = RotateY(N, IblYaw(scene.iblRotationYaw));
    float3 irradiance =
      IrradianceMap.Sample(smpLinearWrap, DirToEquirect(Nibl)).rgb;
    float3 diffuse = irradiance * albedo;

    float3 R   = RotateY(reflect(-V, N), IblYaw(scene.iblRotationYaw));
    float  mip = roughness * max(scene.prefilteredMipCount - 1.0, 0.0);
    float3 prefiltered =
      PrefilteredMap.SampleLevel(smpLinearWrap, DirToEquirect(R), mip)
        .rgb;
    float2 brdf =
      BrdfLUT.Sample(smpLinearClamp, float2(NdotV, roughness)).rg;
    float3 specular = prefiltered * (F0 * brdf.x + brdf.y);

    color += (kD * diffuse + specular) * ao * scene.iblIntensity;
  } else {
    // Fallback if IBL maps are missing / disabled.
    color += scene.ambientColor * scene.ambientIntensity * albedo * ao;
  }

  // ----- Directional light -----
  {
    float3 L = -normalize(scene.directionalLightDirection);
    float3 H = normalize(V + L);

    float3 F   = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float  NDF = DistributionGGX(N, H, roughness);
    float  G   = GeometrySmith(N, V, L, roughness);

    float3 numerator  = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    float3 specular   = numerator / max(denominator, 0.001);

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);

    float3 lightColor =
      scene.directionalLightColor * scene.directionalLightIntensity;
    color += (kD * albedo / PI + specular) * lightColor * NdotL;
  }

  color += emissive;

  // Exposure + Reinhard + gamma (swapchain is UNORM / not sRGB).
  color = DisplayMap(color, scene.exposure, scene.gamma);

  return float4(color, 1.0);
}
