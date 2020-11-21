#pragma once

// This stuff needs to match the definitions in VkBinder.cpp and DxBinder.cpp.

#ifdef SPIRV
#define VD_PUSHCONSTANT [[vk::push_constant]]
#define VD_BINDING(X,Y) [[vk::binding(X,Y)]]
#else
#define VD_PUSHCONSTANT
#define VD_BINDING(X,Y)
#endif

struct PC {
  uint4 data;
};

///////////////////////////////////////////////////////////////////////
// Default

VD_PUSHCONSTANT ConstantBuffer<PC> pc : register(b0, space0);

VD_BINDING(0, 0) SamplerState smpNearestWrap   : register(s0, space0);
VD_BINDING(1, 0) SamplerState smpLinearWrap    : register(s1, space0);
VD_BINDING(2, 0) SamplerState smpNearestMirror : register(s2, space0);
VD_BINDING(3, 0) SamplerState smpLinearMirror  : register(s3, space0);
VD_BINDING(4, 0) SamplerState smpNearestClamp  : register(s4, space0);
VD_BINDING(5, 0) SamplerState smpLinearClamp   : register(s5, space0);

///////////////////////////////////////////////////////////////////////
// Bindless

VD_BINDING(6, 0) SamplerState samplers[] : register(s0, space1);

VD_BINDING(7, 0) ByteAddressBuffer   srvBuf[] : register(t0, space1);
VD_BINDING(7, 0) RWByteAddressBuffer uavBuf[] : register(u0, space1);

VD_BINDING(8, 0) Texture1D<float4>        srvTex1D[]   : register(t0, space2);
VD_BINDING(8, 0) Texture2D<float4>        srvTex2D[]   : register(t0, space3);
VD_BINDING(8, 0) Texture3D<float4>        srvTex3D[]   : register(t0, space4);
VD_BINDING(8, 0) TextureCube<float4>      srvTexCB[]   : register(t0, space5);
VD_BINDING(8, 0) Texture1DArray<float4>   srvTex1DAR[] : register(t0, space6);
VD_BINDING(8, 0) Texture2DArray<float4>   srvTex2DAR[] : register(t0, space7);
VD_BINDING(8, 0) TextureCubeArray<float4> srvTexCBAR[] : register(t0, space8);

VD_BINDING(9, 0) RWTexture1D<float4>      uavTex1D[]   : register(u0, space2);
VD_BINDING(9, 0) RWTexture2D<float4>      uavTex2D[]   : register(u0, space3);
VD_BINDING(9, 0) RWTexture3D<float4>      uavTex3D[]   : register(u0, space4);
VD_BINDING(9, 0) RWTexture1DArray<float4> uavTex1DAR[] : register(u0, space5);
VD_BINDING(9, 0) RWTexture2DArray<float4> uavTex2DAR[] : register(u0, space6);
