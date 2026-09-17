#pragma once

#include "vuldir/Layout.hlsli"
#include "vuldir/Uti.hlsli"

// Push Constants
// 0: scene idx
// 1: prim idx (geometry) OR ui tex idx (UI overlay)

struct Scene {
  float4x4 view;
  float4x4 viewProjection;
  float3 cameraPosition;
  float cameraAspect;

  int gbufIdx0;
  int gbufIdx1;
  int gbufIdx2;
  int gbufIdx3;

  float3 ambientColor;
  float ambientIntensity;

  float3 directionalLightDirection;
  int irradianceMapIdx;
  float3 directionalLightColor;
  int prefilteredMapIdx;
  float directionalLightIntensity;
  int brdfLutIdx;
  float prefilteredMipCount;
  float iblIntensity;

  int iblEnabled;
  float iblRotationYaw;
  float exposure;
  float gamma;

  int debugView;
  float cameraTanHalfFovY;
  int environmentMapIdx;
  int sceneColorIdx;

  float2 aaInvSize;
  float2 aaSize;

  int aaEdgesIdx;
  int aaBlendIdx;
  int aaAreaIdx;
  int aaSearchIdx;
};

struct Prim {
  float4x4 world;

  int vbPosIdx;
  int vbNrmIdx;
  int vbTanIdx;
  int vbUV0Idx;
  int materialIdx;
};

struct Material {
  int colIdx;
  int nrmIdx;
  int mrIdx;
  int aoIdx;

  int emissiveIdx;
  float metallic;
  float roughness;
  float aoStrength;

  float4 baseColorFactor;
  float3 emissiveFactor;
  float _pad0;
};

inline Scene GetScene() { return srvBuf[pc.data.x].Load<Scene>(0); }
inline Prim GetPrim() { return srvBuf[pc.data.y].Load<Prim>(0); }
inline Material GetMaterial() {
  return srvBuf[GetPrim().materialIdx].Load<Material>(0);
}

inline bool HasColorTex() { return GetMaterial().colIdx >= 0; }
inline bool HasNormalTex() { return GetMaterial().nrmIdx >= 0; }
inline bool HasMetallicRoughnessText() { return GetMaterial().mrIdx >= 0; }
inline bool HasAOTex() { return GetMaterial().aoIdx >= 0; }
inline bool HasEmissiveTex() { return GetMaterial().emissiveIdx >= 0; }

#define ColorTex srvTex2D[GetMaterial().colIdx]
#define NormalTex srvTex2D[GetMaterial().nrmIdx]
#define MRTex srvTex2D[GetMaterial().mrIdx]
#define AOTex srvTex2D[GetMaterial().aoIdx]
#define EmissiveTex srvTex2D[GetMaterial().emissiveIdx]
#define GBuf0 srvTex2D[GetScene().gbufIdx0]
#define GBuf1 srvTex2D[GetScene().gbufIdx1]
#define GBuf2 srvTex2D[GetScene().gbufIdx2]
#define GBuf3 srvTex2D[GetScene().gbufIdx3]
#define IrradianceMap srvTex2D[GetScene().irradianceMapIdx]
#define PrefilteredMap srvTex2D[GetScene().prefilteredMapIdx]
#define EnvironmentMap srvTex2D[GetScene().environmentMapIdx]
#define BrdfLUT srvTex2D[GetScene().brdfLutIdx]
#define SceneColor srvTex2D[GetScene().sceneColorIdx]
#define AaEdges srvTex2D[GetScene().aaEdgesIdx]
#define AaBlend srvTex2D[GetScene().aaBlendIdx]
#define AaArea srvTex2D[GetScene().aaAreaIdx]
#define AaSearch srvTex2D[GetScene().aaSearchIdx]

struct VSIn {
  uint instanceId : SV_InstanceID;
  uint vertexId : SV_VertexID;

  float4 GetPosition() {
    return srvBuf[GetPrim().vbPosIdx].Load<float4>(vertexId * 16);
  }

  float3 GetNormal() {
    [branch] if (GetPrim().vbNrmIdx < 0) return 0;
    return unpackUnitVector(
        srvBuf[GetPrim().vbNrmIdx].Load<uint>(vertexId * 4));
  }

  float3 GetTangent() {
    [branch] if (GetPrim().vbTanIdx < 0) return 0;
    return unpackUnitVector(
        srvBuf[GetPrim().vbTanIdx].Load<uint>(vertexId * 4));
  }

  float GetTangentSign() {
    [branch] if (GetPrim().vbTanIdx < 0) return 1;
    uint value = srvBuf[GetPrim().vbTanIdx].Load<uint>(vertexId * 4);
    return (float)((value >> 24u) & 0xFFu) / 255.0 * 2 - 1;
  }

  float2 GetUV0() {
    [branch] if (GetPrim().vbUV0Idx < 0) return 0;
    return unpackHalf2(srvBuf[GetPrim().vbUV0Idx].Load<uint>(vertexId * 4));
  }
};
