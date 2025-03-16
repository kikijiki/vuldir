#pragma once

#include "vuldir/api/Common.hpp"

namespace vd {

#define VDVkTry(x)                                      \
  {                                                     \
    const auto vktry_result = x;                        \
    if(vktry_result != VK_SUCCESS) {                    \
      auto msg = formatString(                          \
        "EXCEPTION\n- Command: %s\n- Result: %s\n", #x, \
        toString(vktry_result));                        \
      throw std::runtime_error(msg.c_str());            \
    }                                                   \
  }

inline constexpr const char* toString(const VkResult value)
{
  switch(value) {
    case VK_SUCCESS:
      return "Success";
    case VK_NOT_READY:
      return "NotReady";
    case VK_TIMEOUT:
      return "Timeout";
    case VK_EVENT_SET:
      return "EventSet";
    case VK_EVENT_RESET:
      return "EventReset";
    case VK_INCOMPLETE:
      return "Incomplete";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
      return "ErrorOutOfHostMemory";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
      return "ErrorOutOfDeviceMemory";
    case VK_ERROR_INITIALIZATION_FAILED:
      return "ErrorInitializationFailed";
    case VK_ERROR_DEVICE_LOST:
      return "ErrorDeviceLost";
    case VK_ERROR_MEMORY_MAP_FAILED:
      return "ErrorMemoryMapFailed";
    case VK_ERROR_LAYER_NOT_PRESENT:
      return "ErrorLayerNotPresent";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
      return "ErrorExtensionNotPresent";
    case VK_ERROR_FEATURE_NOT_PRESENT:
      return "ErrorFeatureNotPresent";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
      return "ErrorIncompatibleDriver";
    case VK_ERROR_TOO_MANY_OBJECTS:
      return "ErrorTooManyObjects";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
      return "ErrorFormatNotSupported";
    case VK_ERROR_FRAGMENTED_POOL:
      return "ErrorFragmentedPool";
    case VK_ERROR_SURFACE_LOST_KHR:
      return "ErrorSurfaceLostKHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
      return "ErrorNativeWindowInUseKHR";
    case VK_SUBOPTIMAL_KHR:
      return "ErrorSuboptimalKHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
      return "ErrorOutOfDateKHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
      return "ErrorIncompatibleDisplayKHR";
    case VK_ERROR_VALIDATION_FAILED_EXT:
      return "ErrorValidationFailedEXT";
    case VK_ERROR_INVALID_SHADER_NV:
      return "ErrorInvalidShaderNV";
    default:
      return "Invalid result";
  }
}

inline constexpr VkImageAspectFlags getVkAspectFlags(Format fmt)
{
  switch(fmt) {
    case Format::D16_UNORM:
    case Format::D32_SFLOAT:
      return VK_IMAGE_ASPECT_DEPTH_BIT;
    case Format::D24_UNORM_S8_UINT:
    case Format::D32_SFLOAT_S8_UINT:
      return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    default:
      return VK_IMAGE_ASPECT_COLOR_BIT;
  }
}

inline constexpr VkFormat getVkDepth(Format fmt)
{
  switch(fmt) {
    case Format::D16_UNORM:
    case Format::D32_SFLOAT:
    case Format::D24_UNORM_S8_UINT:
    case Format::D32_SFLOAT_S8_UINT:
      return convert(fmt);
    default:
      return VK_FORMAT_UNDEFINED;
  }
}

inline constexpr VkFormat getVkStencil(Format fmt)
{
  switch(fmt) {
    case Format::D24_UNORM_S8_UINT:
    case Format::D32_SFLOAT_S8_UINT:
      return convert(fmt);
    default:
      return VK_FORMAT_UNDEFINED;
  }
}

inline constexpr VkFilter convert(SamplerFilter v)
{
  switch(v) {
    case SamplerFilter::Nearest:
      return VK_FILTER_NEAREST;
    case SamplerFilter::Linear:
      return VK_FILTER_LINEAR;
    default:
      throw std::runtime_error("Invalid filter value");
  }
}

inline constexpr VkClearValue convert(const ClearValue& v)
{
  VkClearValue ret{};

  if(std::holds_alternative<DepthStencil>(v)) {
    auto src                 = std::get<DepthStencil>(v);
    ret.depthStencil.depth   = src.depth;
    ret.depthStencil.stencil = src.stencil;
  } else if(std::holds_alternative<Float4>(v)) {
    auto src             = std::get<Float4>(v);
    ret.color.float32[0] = src[0];
    ret.color.float32[1] = src[1];
    ret.color.float32[2] = src[2];
    ret.color.float32[3] = src[3];
  } else if(std::holds_alternative<UInt4>(v)) {
    auto src            = std::get<UInt4>(v);
    ret.color.uint32[0] = src[0];
    ret.color.uint32[1] = src[1];
    ret.color.uint32[2] = src[2];
    ret.color.uint32[3] = src[3];
  } else if(std::holds_alternative<Int4>(v)) {
    auto src           = std::get<Int4>(v);
    ret.color.int32[0] = src[0];
    ret.color.int32[1] = src[1];
    ret.color.int32[2] = src[2];
    ret.color.int32[3] = src[3];
  }

  return ret;
}

inline constexpr VkDescriptorType convert(DescriptorType v)
{
  switch(v) {
    case vd::DescriptorType::Sampler:
      return VK_DESCRIPTOR_TYPE_SAMPLER;
    // case vd::DescriptorType::UniformBuffer:
    //  return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case vd::DescriptorType::StorageBuffer:
      return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    // case vd::DescriptorType::UniformTexelBuffer:
    //  return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    // case vd::DescriptorType::StorageTexelBuffer:
    //  return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    case vd::DescriptorType::SampledImage:
      return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case vd::DescriptorType::StorageImage:
      return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    default:
      throw std::runtime_error("Invalid descriptor type");
  }
}

inline constexpr VkAccessFlags toAccessFlags(ResourceState v)
{
  switch(v) {
    case ResourceState::None:
      return VK_ACCESS_NONE;
    case ResourceState::Undefined:
      return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    case ResourceState::VertexBuffer:
      return VK_ACCESS_SHADER_READ_BIT |
             VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    case ResourceState::IndexBuffer:
      return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT;
    case ResourceState::ConstantBuffer:
      return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT;
    case ResourceState::IndirectArgument:
      return VK_ACCESS_SHADER_READ_BIT |
             VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    case ResourceState::RenderTarget:
      return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    case ResourceState::DepthStencilRW:
      return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    case ResourceState::DepthStencilRO:
      return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    case ResourceState::ShaderResourceGraphics:
      return VK_ACCESS_SHADER_READ_BIT;
    case ResourceState::ShaderResourceCompute:
      return VK_ACCESS_SHADER_READ_BIT;
    case ResourceState::UnorderedAccess:
      return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    case ResourceState::CopySrc:
      return VK_ACCESS_TRANSFER_READ_BIT;
    case ResourceState::CopyDst:
      return VK_ACCESS_TRANSFER_WRITE_BIT;
    case ResourceState::Present:
      return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    default:
      throw std::runtime_error("Invalid resource state");
  }
}

inline constexpr VkAccessFlags2 toAccessFlags2(ResourceState v)
{
  switch(v) {
    case ResourceState::None:
      return VK_ACCESS_2_NONE;
    case ResourceState::Undefined:
      return VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    case ResourceState::VertexBuffer:
      return VK_ACCESS_2_SHADER_READ_BIT |
             VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    case ResourceState::IndexBuffer:
      return VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
    case ResourceState::ConstantBuffer:
      return VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT;
    case ResourceState::IndirectArgument:
      return VK_ACCESS_2_SHADER_READ_BIT |
             VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    case ResourceState::RenderTarget:
      return VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    case ResourceState::DepthStencilRW:
      return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    case ResourceState::DepthStencilRO:
      return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    case ResourceState::ShaderResourceGraphics:
      return VK_ACCESS_2_SHADER_READ_BIT;
    case ResourceState::ShaderResourceCompute:
      return VK_ACCESS_2_SHADER_READ_BIT;
    case ResourceState::UnorderedAccess:
      return VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    case ResourceState::CopySrc:
      return VK_ACCESS_2_TRANSFER_READ_BIT;
    case ResourceState::CopyDst:
      return VK_ACCESS_2_TRANSFER_WRITE_BIT;
    case ResourceState::Present:
      return VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
             VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    default:
      throw std::runtime_error("Invalid resource state");
  }
}

inline constexpr VkPipelineStageFlags
toPipelineStage(ResourceState state)
{
  switch(state) {
    case ResourceState::None:
    case ResourceState::Undefined:
      return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    case ResourceState::VertexBuffer:
    case ResourceState::IndexBuffer:
      return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;

    case ResourceState::ConstantBuffer:
      return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    case ResourceState::IndirectArgument:
      return VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;

    case ResourceState::RenderTarget:
      return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    case ResourceState::DepthStencilRW:
    case ResourceState::DepthStencilRO:
      return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;

    case ResourceState::ShaderResourceGraphics:
      return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    case ResourceState::ShaderResourceCompute:
    case ResourceState::UnorderedAccess:
      return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

    case ResourceState::CopySrc:
    case ResourceState::CopyDst:
      return VK_PIPELINE_STAGE_TRANSFER_BIT;

    case ResourceState::Present:
      return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    default:
      return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  }
}

inline constexpr VkPipelineStageFlags2
toPipelineStage2(ResourceState state)
{
  switch(state) {
    case ResourceState::None:
    case ResourceState::Undefined:
      return VK_PIPELINE_STAGE_2_NONE;

    case ResourceState::VertexBuffer:
    case ResourceState::IndexBuffer:
      return VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;

    case ResourceState::ConstantBuffer:
      return VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
             VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

    case ResourceState::IndirectArgument:
      return VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;

    case ResourceState::RenderTarget:
      return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    case ResourceState::DepthStencilRW:
    case ResourceState::DepthStencilRO:
      return VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
             VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

    case ResourceState::ShaderResourceGraphics:
      return VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
             VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

    case ResourceState::ShaderResourceCompute:
    case ResourceState::UnorderedAccess:
      return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

    case ResourceState::CopySrc:
    case ResourceState::CopyDst:
      return VK_PIPELINE_STAGE_2_COPY_BIT;

    case ResourceState::Present:
      return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    default:
      return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  }
}

inline constexpr VkImageLayout getVkImageLayout(ResourceState v)
{
  switch(v) {
    case ResourceState::None:
      return VK_IMAGE_LAYOUT_UNDEFINED;
    case ResourceState::Undefined:
      return VK_IMAGE_LAYOUT_UNDEFINED;
    case ResourceState::RenderTarget:
      return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case ResourceState::DepthStencilRW:
      return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case ResourceState::DepthStencilRO:
      return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case ResourceState::ShaderResourceGraphics:
      return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case ResourceState::ShaderResourceCompute:
      return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case ResourceState::UnorderedAccess:
      return VK_IMAGE_LAYOUT_GENERAL;
    case ResourceState::CopySrc:
      return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case ResourceState::CopyDst:
      return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case ResourceState::Present:
      return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    default:
      throw std::runtime_error("Invalid image state");
  }
}

inline constexpr VkFlags getVkFormatAspect(Format format)
{
  switch(format) {
    case Format::UNDEFINED:
      return {};

    case Format::D16_UNORM:
    case Format::D32_SFLOAT:
      return VK_IMAGE_ASPECT_DEPTH_BIT;

    case Format::D24_UNORM_S8_UINT:
    case Format::D32_SFLOAT_S8_UINT:
      return VK_IMAGE_ASPECT_STENCIL_BIT | VK_IMAGE_ASPECT_DEPTH_BIT;

    default:
      return VK_IMAGE_ASPECT_COLOR_BIT;
  }
}

inline constexpr VkPipelineBindPoint convert(const BindPoint v)
{
  switch(v) {
    case BindPoint::Graphics:
      return VK_PIPELINE_BIND_POINT_GRAPHICS;
    case BindPoint::Compute:
      return VK_PIPELINE_BIND_POINT_COMPUTE;
    default:
      throw std::runtime_error("Invalid bind point");
  }
}

inline constexpr VkStencilOpState convert(const StencilOpState& v)
{
  return VkStencilOpState{
    .failOp      = convert(v.failOp),
    .passOp      = convert(v.passOp),
    .depthFailOp = convert(v.depthFailOp),
    .compareOp   = convert(v.compareOp),
    .compareMask = v.compareMask,
    .writeMask   = v.writeMask,
    .reference   = v.reference};
}

inline constexpr VkFrontFace convert(FrontFace v)
{
  switch(v) {
    case FrontFace::CW:
      return VK_FRONT_FACE_CLOCKWISE;
    case FrontFace::CCW:
      return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    default:
      throw std::runtime_error("Invalid front face");
  };
}

inline constexpr VkViewport convert(const Viewport& v)
{
  return {
    .x        = v.offset[0],
    .y        = v.offset[1],
    .width    = v.extent[0],
    .height   = v.extent[1],
    .minDepth = v.depthExtent[0],
    .maxDepth = v.depthExtent[1]};
}

inline constexpr VkRect2D convert(const Rect& v)
{
  VkRect2D ret;
  ret.offset.x      = v.offset[0];
  ret.offset.y      = v.offset[1];
  ret.extent.width  = v.extent[0];
  ret.extent.height = v.extent[1];
  return ret;
}

inline constexpr VkAttachmentLoadOp convert(LoadOp v)
{
  switch(v) {
    case LoadOp::Load:
      return VK_ATTACHMENT_LOAD_OP_LOAD;
    case LoadOp::Clear:
      return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case LoadOp::DontCare:
      return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    default:
      throw std::runtime_error("Invalid load op");
  }
}

inline constexpr VkAttachmentStoreOp convert(StoreOp v)
{
  switch(v) {
    case StoreOp::Store:
      return VK_ATTACHMENT_STORE_OP_STORE;
    case StoreOp::DontCare:
      return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    default:
      throw std::runtime_error("Invalid store op");
  }
}

inline constexpr VkResolveModeFlagBits convert(ResolveMode v)
{
  switch(v) {
    case ResolveMode::None:
      return VK_RESOLVE_MODE_NONE;
    case ResolveMode::Zero:
      return VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
    case ResolveMode::Average:
      return VK_RESOLVE_MODE_AVERAGE_BIT;
    case ResolveMode::Min:
      return VK_RESOLVE_MODE_MIN_BIT;
    case ResolveMode::Max:
      return VK_RESOLVE_MODE_MAX_BIT;
    default:
      throw std::runtime_error("Invalid resolve mode");
  }
}

inline constexpr VkOffset3D toVkOffset(Int3 v)
{
  return {v[0], v[1], v[2]};
}

inline constexpr VkExtent3D toVkExtent3D(UInt3 v)
{
  return {v[0], v[1], v[2]};
}

} // namespace vd
