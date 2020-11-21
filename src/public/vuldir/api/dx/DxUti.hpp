#pragma once

#include "vuldir/api/Common.hpp"

namespace vd {

#define VDDxTry(x)                                            \
  {                                                           \
    const auto result = x;                                    \
    if(FAILED(result)) {                                      \
      auto resultStr = formatHRESULT(result);                 \
      auto msg       = formatString(                          \
        "EXCEPTION\n- Command: %s\n- Result: %s\n", #x, \
        resultStr.c_str());                             \
      throw std::runtime_error(msg.c_str());                  \
    }                                                         \
  }

#define VDDxTryBlob(blob, x)                                           \
  {                                                                    \
    const auto result = x;                                             \
    if(FAILED(result)) {                                               \
      auto resultStr = formatHRESULT(result);                          \
      auto errorStr =                                                  \
        blob ? reinterpret_cast<const char*>(blob->GetBufferPointer()) \
             : "unknown";                                              \
      auto msg = formatString(                                         \
        "EXCEPTION\n- Command: %s\n- Result: %s\n- Error: %s\n", #x,   \
        resultStr.c_str(), errorStr);                                  \
      throw std::runtime_error(msg.c_str());                           \
    }                                                                  \
  }

#define VDDxTryDev(dev, x)                                                  \
  {                                                                         \
    const auto result = x;                                                  \
    if(FAILED(result)) {                                                    \
      auto resultStr = formatHRESULT(result);                               \
      auto reason    = dev->GetDeviceRemovedReason();                       \
      auto reasonStr = formatHRESULT(reason);                               \
      auto msg       = formatString(                                        \
        "EXCEPTION\n- Command: %s\n- Result: %s\n- Reason: %s\n", #x, \
        resultStr.c_str(), reasonStr.c_str());                        \
      throw std::runtime_error(msg.c_str());                                \
    }                                                                       \
  }

inline D3D12_FILTER_TYPE convert(SamplerFilter v)
{
  switch(v) {
    case SamplerFilter::Nearest:
      return D3D12_FILTER_TYPE_POINT;
    case SamplerFilter::Linear:
      return D3D12_FILTER_TYPE_LINEAR;
    default:
      throw std::runtime_error("Invalid sampler filter");
  }
}

inline D3D12_FILTER
convert(SamplerFilter min, SamplerFilter mag, SamplerFilter mip)
{
  return D3D12_ENCODE_BASIC_FILTER(
    convert(min), convert(mag), convert(mip),
    D3D12_FILTER_REDUCTION_TYPE_STANDARD);
}

inline D3D12_CLEAR_VALUE convert(const ClearValue& v)
{
  D3D12_CLEAR_VALUE ret{};

  if(std::holds_alternative<DepthStencil>(v)) {
    auto src                 = std::get<DepthStencil>(v);
    ret.DepthStencil.Depth   = src.depth;
    ret.DepthStencil.Stencil = static_cast<u8>(src.stencil);
  } else if(std::holds_alternative<Float4>(v)) {
    auto src     = std::get<Float4>(v);
    ret.Color[0] = src[0];
    ret.Color[1] = src[1];
    ret.Color[2] = src[2];
    ret.Color[3] = src[3];
  } else if(std::holds_alternative<UInt4>(v)) {
    auto src     = std::get<UInt4>(v);
    ret.Color[0] = static_cast<f32>(src[0]) / 255.0f;
    ret.Color[1] = static_cast<f32>(src[1]) / 255.0f;
    ret.Color[2] = static_cast<f32>(src[2]) / 255.0f;
    ret.Color[3] = static_cast<f32>(src[3]) / 255.0f;
  } else if(std::holds_alternative<Int4>(v)) {
    auto src     = std::get<Int4>(v);
    ret.Color[0] = static_cast<f32>(src[0]) / 255.0f;
    ret.Color[1] = static_cast<f32>(src[1]) / 255.0f;
    ret.Color[2] = static_cast<f32>(src[2]) / 255.0f;
    ret.Color[3] = static_cast<f32>(src[3]) / 255.0f;
  }

  return ret;
}

inline D3D12_DEPTH_STENCILOP_DESC convert(const StencilOpState& v)
{
  return D3D12_DEPTH_STENCILOP_DESC{
    .StencilFailOp      = convert(v.failOp),
    .StencilDepthFailOp = convert(v.depthFailOp),
    .StencilPassOp      = convert(v.passOp),
    .StencilFunc        = convert(v.compareOp)};
}

inline D3D12_RESOURCE_STATES convert(ResourceState v)
{
  switch(v) {
    case ResourceState::Undefined:
      return D3D12_RESOURCE_STATE_COMMON;
    case ResourceState::VertexBuffer:
      return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case ResourceState::IndexBuffer:
      return D3D12_RESOURCE_STATE_INDEX_BUFFER;
    case ResourceState::ConstantBuffer:
      return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case ResourceState::IndirectArgument:
      return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    case ResourceState::RenderTarget:
      return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case ResourceState::DepthStencilRW:
      return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case ResourceState::DepthStencilRO:
      return D3D12_RESOURCE_STATE_DEPTH_READ;
    case ResourceState::ShaderResourceGraphics:
      return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case ResourceState::ShaderResourceCompute:
      return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case ResourceState::UnorderedAccess:
      return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case ResourceState::CopySrc:
      return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case ResourceState::CopyDst:
      return D3D12_RESOURCE_STATE_COPY_DEST;
    case ResourceState::Present:
      return D3D12_RESOURCE_STATE_PRESENT;
    default:
      throw std::runtime_error("Invalid resource state");
  }
}

inline constexpr D3D12_VIEWPORT convert(const Viewport& v)
{
  return {
    .TopLeftX = v.offset[0],
    .TopLeftY = v.offset[1],
    .Width    = v.extent[0],
    .Height   = v.extent[1],
    .MinDepth = v.depthExtent[0],
    .MaxDepth = v.depthExtent[1]};
}

inline constexpr D3D12_RECT convert(const Rect& v)
{
  return {
    .left   = static_cast<LONG>(v.offset[0]),
    .top    = static_cast<LONG>(v.offset[1]),
    .right  = static_cast<LONG>(v.offset[0] + v.extent[0]),
    .bottom = static_cast<LONG>(v.offset[1] + v.extent[1])};
}

} // namespace vd
