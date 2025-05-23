#pragma once

#include "vuldir/api/Common.hpp"

namespace vd {

enum class QueueType { Graphics, Compute, Copy };
static constexpr u32 QueueTypeCount = 3u;

static constexpr SArr<QueueType, QueueTypeCount> QueueTypes = {
  QueueType::Graphics, QueueType::Compute, QueueType::Copy};

enum class MemoryType { Main, Upload, Download };
static constexpr u32 MemoryTypeCount = 3u;

static constexpr SArr<MemoryType, MemoryTypeCount> MemoryTypes = {
  MemoryType::Main, MemoryType::Upload, MemoryType::Download};

enum class SwapchainDep { None, Acquire, Release, AcquireRelease };

struct Viewport {
  Float2 offset;
  Float2 extent;
  Float2 depthExtent;
};

enum class ImageAspect { Color, Depth, DepthStencil };

enum class BindPoint { Graphics, Compute };

enum class ResourceUsage {
  ShaderResource   = 1u << 0,
  UnorderedAccess  = 1u << 1,
  DepthStencil     = 1u << 2,
  RenderTarget     = 1u << 3,
  IndexBuffer      = 1u << 4,
  VertexBuffer     = 1u << 5,
  IndirectArgument = 1u << 6
};
VD_FLAGS(ResourceUsage);

enum class ViewType { CBV, SRV, UAV, DSV, RTV };

enum class Dimension { e1D, e2D, e3D, eCube };

enum class DescriptorType {
  Sampler,
  StorageBuffer,
  SampledImage,
  StorageImage
};

enum class ResourceState {
  None,
  Undefined,

  // D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
  // VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT
  VertexBuffer,

  // D3D12_RESOURCE_STATE_INDEX_BUFFER
  // VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDEX_READ_BIT
  IndexBuffer,

  // D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
  // VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT
  ConstantBuffer,

  // D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT
  // VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT
  IndirectArgument,

  // D3D12_RESOURCE_STATE_RENDER_TARGET
  // VK_ACCESS_SHADER_WRITE_BIT
  // VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
  RenderTarget,

  // D3D12_RESOURCE_STATE_DEPTH_WRITE
  // VK_ACCESS_SHADER_WRITE_BIT
  // VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
  DepthStencilRW,

  // D3D12_RESOURCE_STATE_DEPTH_READ
  // VK_ACCESS_SHADER_READ_BIT
  // VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
  DepthStencilRO,

  // D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
  // D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
  // Buf: VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT
  // Img: VK_ACCESS_SHADER_READ_BIT
  // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
  ShaderResourceGraphics,

  // D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
  // Buf: VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT
  // Img: VK_ACCESS_SHADER_READ_BIT
  // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
  ShaderResourceCompute,

  // D3D12_RESOURCE_STATE_UNORDERED_ACCESS
  // VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
  // VK_IMAGE_LAYOUT_GENERAL
  UnorderedAccess,

  // D3D12_RESOURCE_STATE_COPY_SOURCE
  // VK_ACCESS_TRANSFER_READ_BIT
  // VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
  CopySrc,

  // D3D12_RESOURCE_STATE_COPY_DEST
  // VK_ACCESS_TRANSFER_WRITE_BIT
  // VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
  CopyDst,

  // D3D12_RESOURCE_STATE_PRESENT
  // VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
  // VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
  // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
  Present
};

enum class LoadOp { Load, Clear, DontCare };

enum class StoreOp { Store, DontCare };

enum class ResolveMode { None, Zero, Average, Min, Max };

enum class AttachmentType { RenderTarget, ResolveTarget, DepthStencil };

enum class PrimitiveTopology {
  VD_API_VALUE(
    PointList, VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
    D3D_PRIMITIVE_TOPOLOGY_POINTLIST),
  VD_API_VALUE(
    LineList, VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
    D3D_PRIMITIVE_TOPOLOGY_LINELIST),
  VD_API_VALUE(
    LineStrip, VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
    D3D_PRIMITIVE_TOPOLOGY_LINESTRIP),
  VD_API_VALUE(
    TriangleList, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST),
  VD_API_VALUE(
    TriangleStrip, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP),
  VD_API_VALUE(
    LineListAdj, VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY,
    D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ),
  VD_API_VALUE(
    LineStripAdj, VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY,
    D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ),
  VD_API_VALUE(
    TriangleListAdj, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY,
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ),
  VD_API_VALUE(
    TriangleStripAdj,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY,
    D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ)
};
VD_API_VALUE_CONVERTER(
  PrimitiveTopology, VkPrimitiveTopology, D3D_PRIMITIVE_TOPOLOGY);

enum class PolygonMode {
  VD_API_VALUE(Fill, VK_POLYGON_MODE_FILL, D3D12_FILL_MODE_SOLID),
  VD_API_VALUE(Line, VK_POLYGON_MODE_LINE, D3D12_FILL_MODE_WIREFRAME),
};
VD_API_VALUE_CONVERTER(PolygonMode, VkPolygonMode, D3D12_FILL_MODE);

enum class CullMode {
  VD_API_VALUE(None, VK_CULL_MODE_NONE, D3D12_CULL_MODE_NONE),
  VD_API_VALUE(Front, VK_CULL_MODE_FRONT_BIT, D3D12_CULL_MODE_FRONT),
  VD_API_VALUE(Back, VK_CULL_MODE_BACK_BIT, D3D12_CULL_MODE_BACK)
};
VD_API_VALUE_CONVERTER(CullMode, VkCullModeFlags, D3D12_CULL_MODE);

enum class FrontFace { CW, CCW };

inline const char* toString(MemoryType v)
{
  switch(v) {
    case MemoryType::Main:
      return "Main";
    case MemoryType::Upload:
      return "Upload";
    case MemoryType::Download:
      return "Download";
    default:
      throw std::runtime_error("Unsupported memory type");
  }
}

inline constexpr const char* toString(const ViewType v)
{
  switch(v) {
    case ViewType::CBV:
      return "CBV";
    case ViewType::SRV:
      return "SRV";
    case ViewType::UAV:
      return "UAV";
    case ViewType::DSV:
      return "DSV";
    case ViewType::RTV:
      return "RTV";
    default:
      throw std::runtime_error("Invalid view type");
  }
}

enum class Format : u32 {
  VD_API_VALUE(UNDEFINED, VK_FORMAT_UNDEFINED, DXGI_FORMAT_UNKNOWN),

  // Depth
  VD_API_VALUE(D16_UNORM, VK_FORMAT_D16_UNORM, DXGI_FORMAT_D16_UNORM),
  VD_API_VALUE(D32_SFLOAT, VK_FORMAT_D32_SFLOAT, DXGI_FORMAT_D32_FLOAT),

  // Depth + Stencil
  VD_API_VALUE(
    D24_UNORM_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT,
    DXGI_FORMAT_D24_UNORM_S8_UINT),
  VD_API_VALUE(
    D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT,
    DXGI_FORMAT_D32_FLOAT_S8X24_UINT),

  // UV
  VD_API_VALUE(
    R32G32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, DXGI_FORMAT_R32G32_FLOAT),

  // Color: R8
  VD_API_VALUE(R8_UNORM, VK_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM),
  VD_API_VALUE(R8_SNORM, VK_FORMAT_R8_SNORM, DXGI_FORMAT_R8_SNORM),
  VD_API_VALUE(R8_UINT, VK_FORMAT_R8_UINT, DXGI_FORMAT_R8_UINT),
  VD_API_VALUE(R8_SINT, VK_FORMAT_R8_SINT, DXGI_FORMAT_R8_SINT),

  // Color: R16
  VD_API_VALUE(R16_UNORM, VK_FORMAT_R16_UNORM, DXGI_FORMAT_R16_UNORM),
  VD_API_VALUE(R16_SNORM, VK_FORMAT_R16_SNORM, DXGI_FORMAT_R16_SNORM),
  VD_API_VALUE(R16_UINT, VK_FORMAT_R16_UINT, DXGI_FORMAT_R16_UINT),
  VD_API_VALUE(R16_SINT, VK_FORMAT_R16_SINT, DXGI_FORMAT_R16_SINT),
  VD_API_VALUE(R16_SFLOAT, VK_FORMAT_R16_SFLOAT, DXGI_FORMAT_R16_FLOAT),

  // Color: R8G8B8A8
  VD_API_VALUE(
    R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
    DXGI_FORMAT_R8G8B8A8_UNORM),
  VD_API_VALUE(
    R8G8B8A8_SNORM, VK_FORMAT_R8G8B8A8_SNORM,
    DXGI_FORMAT_R8G8B8A8_SNORM),
  VD_API_VALUE(
    R8G8B8A8_UINT, VK_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_UINT),
  VD_API_VALUE(
    R8G8B8A8_SINT, VK_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_SINT),
  VD_API_VALUE(
    R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB,
    DXGI_FORMAT_R8G8B8A8_UNORM_SRGB),

  // Color: R16G16B16A16
  VD_API_VALUE(
    R16G16B16A16_UNORM, VK_FORMAT_R16G16B16A16_UNORM,
    DXGI_FORMAT_R16G16B16A16_UNORM),
  VD_API_VALUE(
    R16G16B16A16_SNORM, VK_FORMAT_R16G16B16A16_SNORM,
    DXGI_FORMAT_R16G16B16A16_SNORM),
  VD_API_VALUE(
    R16G16B16A16_UINT, VK_FORMAT_R16G16B16A16_UINT,
    DXGI_FORMAT_R16G16B16A16_UINT),
  VD_API_VALUE(
    R16G16B16A16_SINT, VK_FORMAT_R16G16B16A16_SINT,
    DXGI_FORMAT_R16G16B16A16_SINT),
  VD_API_VALUE(
    R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT,
    DXGI_FORMAT_R16G16B16A16_FLOAT),

  // Color: B8G8R8A8
  VD_API_VALUE(
    B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM,
    DXGI_FORMAT_B8G8R8A8_UNORM),
  VD_API_VALUE(
    B8G8R8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB,
    DXGI_FORMAT_B8G8R8A8_UNORM_SRGB),

  // Color: R32G32B32
  VD_API_VALUE(
    R32G32B32_UINT, VK_FORMAT_R32G32B32_UINT,
    DXGI_FORMAT_R32G32B32_UINT),
  VD_API_VALUE(
    R32G32B32_SINT, VK_FORMAT_R32G32B32_SINT,
    DXGI_FORMAT_R32G32B32_SINT),
  VD_API_VALUE(
    R32G32B32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT,
    DXGI_FORMAT_R32G32B32_FLOAT),

  // Color: R32G32B32A32
  VD_API_VALUE(
    R32G32B32A32_UINT, VK_FORMAT_R32G32B32A32_UINT,
    DXGI_FORMAT_R32G32B32A32_UINT),
  VD_API_VALUE(
    R32G32B32A32_SINT, VK_FORMAT_R32G32B32A32_SINT,
    DXGI_FORMAT_R32G32B32A32_SINT),
  VD_API_VALUE(
    R32G32B32A32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT,
    DXGI_FORMAT_R32G32B32A32_FLOAT),
};
VD_API_VALUE_CONVERTER(Format, VkFormat, DXGI_FORMAT);

enum class ColorSpace {
  VD_API_VALUE(
    SRGB_NONLINEAR, VK_COLORSPACE_SRGB_NONLINEAR_KHR,
    DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709),
  VD_API_VALUE(
    HDR10_ST2084, VK_COLOR_SPACE_HDR10_ST2084_EXT,
    DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020),
  VD_API_VALUE(
    HDR10_HLG, VK_COLOR_SPACE_HDR10_HLG_EXT,
    DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020),
};
VD_API_VALUE_CONVERTER(
  ColorSpace, VkColorSpaceKHR, DXGI_COLOR_SPACE_TYPE);

enum class PipelineStage {
  Vertex,
  Pixel,
  Compute,
};

enum class VertexAttribute {
  Position,
  Normal,
  Tangent,
  TexCoord,
  Color
};

enum class SamplerFilter { Nearest, Linear };

enum class SamplerAddressMode {
  VD_API_VALUE(
    Repeat, VK_SAMPLER_ADDRESS_MODE_REPEAT,
    D3D12_TEXTURE_ADDRESS_MODE_WRAP),
  VD_API_VALUE(
    Mirror, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
    D3D12_TEXTURE_ADDRESS_MODE_MIRROR),
  VD_API_VALUE(
    Clamp, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
  VD_API_VALUE(
    Border, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
    D3D12_TEXTURE_ADDRESS_MODE_BORDER)
};
VD_API_VALUE_CONVERTER(
  SamplerAddressMode, VkSamplerAddressMode, D3D12_TEXTURE_ADDRESS_MODE);

enum class SamplerBorderColor {
  VD_API_VALUE(
    FloatTransparentBlack, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
    D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK),
  VD_API_VALUE(
    IntTransparentBlack, VK_BORDER_COLOR_INT_TRANSPARENT_BLACK,
    D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK),
  VD_API_VALUE(
    FloatOpaqueBlack, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
    D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK),
  VD_API_VALUE(
    IntOpaqueBlack, VK_BORDER_COLOR_INT_OPAQUE_BLACK,
    D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK),
  VD_API_VALUE(
    FloatOpaqueWhite, VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
    D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE),
  VD_API_VALUE(
    IntOpaqueWhite, VK_BORDER_COLOR_INT_OPAQUE_WHITE,
    D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE)
};
VD_API_VALUE_CONVERTER(
  SamplerBorderColor, VkBorderColor, D3D12_STATIC_BORDER_COLOR);

enum class AlphaMode { Opaque, Mask, Blend };

enum class CompareOp {
  VD_API_VALUE(Never, VK_COMPARE_OP_NEVER, D3D12_COMPARISON_FUNC_NEVER),
  VD_API_VALUE(Less, VK_COMPARE_OP_LESS, D3D12_COMPARISON_FUNC_LESS),
  VD_API_VALUE(Equal, VK_COMPARE_OP_EQUAL, D3D12_COMPARISON_FUNC_EQUAL),
  VD_API_VALUE(
    LessOrEqual, VK_COMPARE_OP_LESS_OR_EQUAL,
    D3D12_COMPARISON_FUNC_LESS_EQUAL),
  VD_API_VALUE(
    Greater, VK_COMPARE_OP_GREATER, D3D12_COMPARISON_FUNC_GREATER),
  VD_API_VALUE(
    NotEqual, VK_COMPARE_OP_NOT_EQUAL, D3D12_COMPARISON_FUNC_NOT_EQUAL),
  VD_API_VALUE(
    GreaterOrEqual, VK_COMPARE_OP_GREATER_OR_EQUAL,
    D3D12_COMPARISON_FUNC_GREATER_EQUAL),
  VD_API_VALUE(
    Always, VK_COMPARE_OP_ALWAYS, D3D12_COMPARISON_FUNC_ALWAYS)
};
VD_API_VALUE_CONVERTER(CompareOp, VkCompareOp, D3D12_COMPARISON_FUNC);

enum class StencilOp {
  VD_API_VALUE(Keep, VK_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP),
  VD_API_VALUE(Zero, VK_STENCIL_OP_ZERO, D3D12_STENCIL_OP_ZERO),
  VD_API_VALUE(
    Replace, VK_STENCIL_OP_REPLACE, D3D12_STENCIL_OP_REPLACE),
  VD_API_VALUE(
    IncrementClamp, VK_STENCIL_OP_INCREMENT_AND_CLAMP,
    D3D12_STENCIL_OP_INCR_SAT),
  VD_API_VALUE(
    DecrementClamp, VK_STENCIL_OP_DECREMENT_AND_CLAMP,
    D3D12_STENCIL_OP_DECR_SAT),
  VD_API_VALUE(Invert, VK_STENCIL_OP_INVERT, D3D12_STENCIL_OP_INVERT),
  VD_API_VALUE(
    IncrementWrap, VK_STENCIL_OP_INCREMENT_AND_WRAP,
    D3D12_STENCIL_OP_INCR),
  VD_API_VALUE(
    DecrementWrap, VK_STENCIL_OP_DECREMENT_AND_WRAP,
    D3D12_STENCIL_OP_DECR),
};
VD_API_VALUE_CONVERTER(StencilOp, VkStencilOp, D3D12_STENCIL_OP);

enum class LogicOp {
  VD_API_VALUE(CLEAR, VK_LOGIC_OP_CLEAR, D3D12_LOGIC_OP_CLEAR),
  VD_API_VALUE(AND, VK_LOGIC_OP_AND, D3D12_LOGIC_OP_AND),
  VD_API_VALUE(
    AND_REVERSE, VK_LOGIC_OP_AND_REVERSE, D3D12_LOGIC_OP_AND_REVERSE),
  VD_API_VALUE(COPY, VK_LOGIC_OP_COPY, D3D12_LOGIC_OP_COPY),
  VD_API_VALUE(
    AND_INVERTED, VK_LOGIC_OP_AND_INVERTED,
    D3D12_LOGIC_OP_AND_INVERTED),
  VD_API_VALUE(NO_OP, VK_LOGIC_OP_NO_OP, D3D12_LOGIC_OP_NOOP),
  VD_API_VALUE(XOR, VK_LOGIC_OP_XOR, D3D12_LOGIC_OP_XOR),
  VD_API_VALUE(OR, VK_LOGIC_OP_OR, D3D12_LOGIC_OP_OR),
  VD_API_VALUE(NOR, VK_LOGIC_OP_NOR, D3D12_LOGIC_OP_NOR),
  VD_API_VALUE(
    EQUIVALENT, VK_LOGIC_OP_EQUIVALENT, D3D12_LOGIC_OP_EQUIV),
  VD_API_VALUE(INVERT, VK_LOGIC_OP_INVERT, D3D12_LOGIC_OP_INVERT),
  VD_API_VALUE(
    OR_REVERSE, VK_LOGIC_OP_OR_REVERSE, D3D12_LOGIC_OP_OR_REVERSE),
  VD_API_VALUE(
    COPY_INVERTED, VK_LOGIC_OP_COPY_INVERTED,
    D3D12_LOGIC_OP_COPY_INVERTED),
  VD_API_VALUE(
    OR_INVERTED, VK_LOGIC_OP_OR_INVERTED, D3D12_LOGIC_OP_OR_INVERTED),
  VD_API_VALUE(NAND, VK_LOGIC_OP_NAND, D3D12_LOGIC_OP_NAND),
  VD_API_VALUE(SET, VK_LOGIC_OP_SET, D3D12_LOGIC_OP_SET),
};
VD_API_VALUE_CONVERTER(LogicOp, VkLogicOp, D3D12_LOGIC_OP);

enum class BlendFactor {
  VD_API_VALUE(ZERO, VK_BLEND_FACTOR_ZERO, D3D12_BLEND_ZERO),
  VD_API_VALUE(ONE, VK_BLEND_FACTOR_ONE, D3D12_BLEND_ONE),
  VD_API_VALUE(
    SRC_COLOR, VK_BLEND_FACTOR_SRC_COLOR, D3D12_BLEND_SRC_COLOR),
  VD_API_VALUE(
    ONE_MINUS_SRC_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    D3D12_BLEND_INV_SRC_COLOR),
  VD_API_VALUE(
    DST_COLOR, VK_BLEND_FACTOR_DST_COLOR, D3D12_BLEND_DEST_COLOR),
  VD_API_VALUE(
    ONE_MINUS_DST_COLOR, VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
    D3D12_BLEND_INV_DEST_COLOR),
  VD_API_VALUE(
    SRC_ALPHA, VK_BLEND_FACTOR_SRC_ALPHA, D3D12_BLEND_SRC_ALPHA),
  VD_API_VALUE(
    ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    D3D12_BLEND_INV_SRC_ALPHA),
  VD_API_VALUE(
    DST_ALPHA, VK_BLEND_FACTOR_DST_ALPHA, D3D12_BLEND_DEST_ALPHA),
  VD_API_VALUE(
    ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
    D3D12_BLEND_INV_DEST_ALPHA),
  VD_API_VALUE(
    SRC1_COLOR, VK_BLEND_FACTOR_SRC1_COLOR, D3D12_BLEND_SRC1_COLOR),
  VD_API_VALUE(
    ONE_MINUS_SRC1_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR,
    D3D12_BLEND_INV_SRC1_COLOR),
  VD_API_VALUE(
    SRC1_ALPHA, VK_BLEND_FACTOR_SRC1_ALPHA, D3D12_BLEND_SRC1_ALPHA),
  VD_API_VALUE(
    ONE_MINUS_SRC1_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA,
    D3D12_BLEND_INV_SRC1_ALPHA),
};
VD_API_VALUE_CONVERTER(BlendFactor, VkBlendFactor, D3D12_BLEND);

enum class BlendOp {
  VD_API_VALUE(ADD, VK_BLEND_OP_ADD, D3D12_BLEND_OP_ADD),
  VD_API_VALUE(SUBTRACT, VK_BLEND_OP_SUBTRACT, D3D12_BLEND_OP_SUBTRACT),
  VD_API_VALUE(
    REVERSE_SUBTRACT, VK_BLEND_OP_REVERSE_SUBTRACT,
    D3D12_BLEND_OP_REV_SUBTRACT),
  VD_API_VALUE(MIN, VK_BLEND_OP_MIN, D3D12_BLEND_OP_MIN),
  VD_API_VALUE(MAX, VK_BLEND_OP_MAX, D3D12_BLEND_OP_MAX),
};
VD_API_VALUE_CONVERTER(BlendOp, VkBlendOp, D3D12_BLEND_OP);

struct ViewRange {
  u64 offset = 0u;
  u64 size   = MaxU64;

  u32 mipOffset   = 0u;
  u32 mipCount    = MaxU32;
  u32 layerOffset = 0u;
  u32 layerCount  = MaxU32;
};

struct DescriptorBinding {
  DescriptorType type;
  u32            index;
};

struct StencilOpState {
  StencilOp failOp;
  StencilOp passOp;
  StencilOp depthFailOp;
  CompareOp compareOp;
  u32       compareMask;
  u32       writeMask;
  u32       reference;
};

inline u32 getFormatSize(Format fmt)
{
  switch(fmt) {
    case vd::Format::UNDEFINED:
      return 0u;
    case vd::Format::R8_UNORM:
    case vd::Format::R8_SNORM:
    case vd::Format::R8_UINT:
    case vd::Format::R8_SINT:
      return 1u;
    case vd::Format::D16_UNORM:
      return 2u;
    case vd::Format::R16_UNORM:
    case vd::Format::R16_SNORM:
    case vd::Format::R16_UINT:
    case vd::Format::R16_SINT:
    case vd::Format::R16_SFLOAT:
      return 2u;
    case vd::Format::D32_SFLOAT:
    case vd::Format::D24_UNORM_S8_UINT:
      return 4u;
    case vd::Format::R8G8B8A8_UNORM:
    case vd::Format::R8G8B8A8_SNORM:
    case vd::Format::R8G8B8A8_UINT:
    case vd::Format::R8G8B8A8_SINT:
    case vd::Format::R8G8B8A8_SRGB:
      return 4u;
    case vd::Format::B8G8R8A8_UNORM:
    case vd::Format::B8G8R8A8_SRGB:
      return 4u;
    case vd::Format::D32_SFLOAT_S8_UINT:
      return 5u;
    case vd::Format::R32G32_SFLOAT:
      return 8u;
    case vd::Format::R16G16B16A16_UNORM:
    case vd::Format::R16G16B16A16_SNORM:
    case vd::Format::R16G16B16A16_UINT:
    case vd::Format::R16G16B16A16_SINT:
    case vd::Format::R16G16B16A16_SFLOAT:
      return 8u;
    case vd::Format::R32G32B32_UINT:
    case vd::Format::R32G32B32_SINT:
    case vd::Format::R32G32B32_SFLOAT:
      return 12u;
    case vd::Format::R32G32B32A32_UINT:
    case vd::Format::R32G32B32A32_SINT:
    case vd::Format::R32G32B32A32_SFLOAT:
      return 16u;
    default:
      throw std::runtime_error("Invalid format");
  }
}

inline constexpr bool isStencilFormat(Format v)
{
  switch(v) {
    case Format::D24_UNORM_S8_UINT:
    case Format::D32_SFLOAT_S8_UINT:
      return true;
    default:
      return false;
  }
}

inline constexpr ImageAspect getFormatAspect(Format fmt)
{
  switch(fmt) {
    case Format::D16_UNORM:
    case Format::D32_SFLOAT:
      return ImageAspect::Depth;
    case Format::D24_UNORM_S8_UINT:
    case Format::D32_SFLOAT_S8_UINT:
      return ImageAspect::DepthStencil;
    default:
      return ImageAspect::Color;
  }
}

struct DepthStencil {
  f32 depth;
  u32 stencil;
};

using ClearValue = std::variant<Float4, UInt4, Int4, DepthStencil>;

enum class IndexType {
  VD_API_VALUE(U16, VK_INDEX_TYPE_UINT16, DXGI_FORMAT_R16_UINT),
  VD_API_VALUE(U32, VK_INDEX_TYPE_UINT32, DXGI_FORMAT_R32_UINT)
};
VD_API_VALUE_CONVERTER_ONEWAY(IndexType, VkIndexType, DXGI_FORMAT);

} // namespace vd
