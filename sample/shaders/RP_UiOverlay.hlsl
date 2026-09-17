#include "Common.hlsli"

struct UiDrawParam {
  float2 translation;
  float2 invViewport;
};

struct VSOut {
  float4 pos : SV_POSITION;
  float4 color : COLOR0;
  float2 uv0 : TEXCOORD0;
};

VSOut MainVS(uint vertexId : SV_VertexID)
{
  ByteAddressBuffer vertices   = srvBuf[pc.data.x];
  ByteAddressBuffer drawParams = srvBuf[pc.data.z];

  uint        vertexOffset  = vertexId * 20u;
  float2      pixelPosition = asfloat(vertices.Load2(vertexOffset));
  uint        packedColor   = vertices.Load(vertexOffset + 8u);
  float2      uv   = asfloat(vertices.Load2(vertexOffset + 12u));
  UiDrawParam draw = drawParams.Load<UiDrawParam>(pc.data.w * 16u);

  VSOut  ret;
  float2 position =
    (pixelPosition + draw.translation) * draw.invViewport;
  ret.pos   = ClipPosFromUvTopLeft(position);
  ret.color = float4(
                packedColor & 0xffu, (packedColor >> 8u) & 0xffu,
                (packedColor >> 16u) & 0xffu, packedColor >> 24u) /
              255.f;
  ret.uv0 = uv;
  return ret;
}

float4 MainPS(VSOut input): SV_TARGET
{
  float4 color = input.color;
  if(pc.data.y != ~0u)
    color *= srvTex2D[pc.data.y].Sample(smpLinearClamp, input.uv0);
  return color;
}
