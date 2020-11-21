#pragma once

#include "vuldir/Layout.hlsli"
#include "vuldir/Uti.hlsli"

// Push Constants
// 0: scene idx
// 1: mesh idx

struct Scene {
  float4x4 view;
  float4x4 viewProjection;
  float3   cameraPosition;

  int gbufIdx0;
  int gbufIdx1;
  int gbufIdx2;
};

struct Mesh {
  float4x4 world;

  int vbPosNrmIdx;
  int vbTanIdx;
  int vbUV0Idx;
  int materialIdx;
};

struct Material {
  int colIdx;
  int nrmIdx;

  float metallic;
  float roughness;
};

inline Scene    GetScene()    { return srvBuf[pc.data.x].Load<Scene>(0); }
inline Mesh     GetMesh()     { return srvBuf[pc.data.y].Load<Mesh>(0); }
inline Material GetMaterial() { return srvBuf[GetMesh().materialIdx].Load<Material>(0); }

inline bool HasColorTex() { return GetMaterial().colIdx >= 0; }
inline bool HasNormalTex() { return GetMaterial().nrmIdx >= 0; }

#define ColorTex  srvTex2D[GetMaterial().colIdx]
#define NormalTex srvTex2D[GetMaterial().nrmIdx]
#define GBuf0  srvTex2D[GetScene().gbufIdx0]
#define GBuf1  srvTex2D[GetScene().gbufIdx1]
#define GBuf2  srvTex2D[GetScene().gbufIdx2]

struct VSIn {
  uint instanceId : SV_InstanceID;
  uint vertexId   : SV_VertexID;

  float4 GetPosition()
  {
    return float4(srvBuf[GetMesh().vbPosNrmIdx].Load<float3>(vertexId * 16), 1);
  }

  float3 GetNormal()
  {
    return unpackUnitVector(srvBuf[GetMesh().vbPosNrmIdx].Load<uint4>(vertexId * 16).w);
  }

  float3 GetTangent()
  {
    [branch] if(GetMesh().vbTanIdx < 0) return 0;
    return unpackUnitVector(srvBuf[GetMesh().vbTanIdx].Load<uint4>(vertexId * 16).w);
  }

  float2 GetUV0()
  {
    [branch] if(GetMesh().vbUV0Idx < 0) return 0;
    return unpackHalf2(srvBuf[GetMesh().vbUV0Idx].Load<uint>(vertexId * 4));
  }
};
