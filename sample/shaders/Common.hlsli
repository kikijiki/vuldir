#pragma once

#include "vuldir/Layout.hlsli"
#include "vuldir/Uti.hlsli"

// Push Constants
// 0: scene idx
// 1: prim idx

struct Scene {
  float4x4 view;
  float4x4 viewProjection;
  float3 cameraPosition;
  int _pad0;

  int gbufIdx0;
  int gbufIdx1;
  int gbufIdx2;
  int _pad1;

  float3 ambientColor;
  int _pad2;

  float3 directionalLightDirection;
  int _pad3;
  float3 directionalLightColor;
  int _pad4;
  float directionalLightIntensity;
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

  float metallic;
  float roughness;
};

inline Scene GetScene() { return srvBuf[pc.data.x].Load<Scene>(0); }
inline Prim GetPrim() { return srvBuf[pc.data.y].Load<Prim>(0); }
inline Material GetMaterial() {
  return srvBuf[GetPrim().materialIdx].Load<Material>(0);
}

inline bool HasColorTex() { return GetMaterial().colIdx >= 0; }
inline bool HasNormalTex() { return GetMaterial().nrmIdx >= 0; }
inline bool HasMetallicRoughnessText() { return GetMaterial().mrIdx >= 0; }

#define ColorTex srvTex2D[GetMaterial().colIdx]
#define NormalTex srvTex2D[GetMaterial().nrmIdx]
#define MRTex srvTex2D[GetMaterial().mrIdx]
#define GBuf0 srvTex2D[GetScene().gbufIdx0]
#define GBuf1 srvTex2D[GetScene().gbufIdx1]
#define GBuf2 srvTex2D[GetScene().gbufIdx2]

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

  float2 GetUV0() {
    [branch] if (GetPrim().vbUV0Idx < 0) return 0;
    return unpackHalf2(srvBuf[GetPrim().vbUV0Idx].Load<uint>(vertexId * 4));
  }
};
