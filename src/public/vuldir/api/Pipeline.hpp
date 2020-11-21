#pragma once

#include "vuldir/api/Common.hpp"

namespace vd {

class Device;
class Shader;
class CommandBuffer;

class Pipeline
{
public:
  struct AttachmentBlendDesc {
    bool blendEnable = true;

    BlendFactor srcColorBlendFactor = BlendFactor::SRC_ALPHA;
    BlendFactor dstColorBlendFactor = BlendFactor::ONE_MINUS_SRC_ALPHA;
    BlendOp     colorBlendOp        = BlendOp::ADD;

    BlendFactor srcAlphaBlendFactor = BlendFactor::ONE;
    BlendFactor dstAlphaBlendFactor = BlendFactor::ZERO;
    BlendOp     alphaBlendOp        = BlendOp::ADD;

    bool writeR = true;
    bool writeG = true;
    bool writeB = true;
    bool writeA = true;
  };

  struct GraphicsDesc {
    Str name;

    Shader* VS = nullptr;
    Shader* PS = nullptr;

    PrimitiveTopology topology = PrimitiveTopology::TriangleList;

    Viewport viewport = {};
    Rect     scissor  = {};

    Format      depthStencilFormat;
    Arr<Format> colorFormats;

    PolygonMode polygonMode = PolygonMode::Fill;
    CullMode    cullMode    = CullMode::None;
    FrontFace   frontFace   = FrontFace::CCW;

    bool depthClampEnable   = false;
    bool discardEnable      = false;
    bool conservativeRaster = false;

    bool depthBiasEnable = false;
    f32  depthBiasFactor = 0.f;
    f32  depthBiasClamp  = 0.f;
    f32  depthBiasSlope  = 0.f;

    f32  lineWidth     = 1.f;
    bool lineAntiAlias = false;

    u32 sampleCount   = 1u;
    u32 sampleQuality = 0u;
    u32 sampleMask    = 0xffffffff;

    bool alphaToCoverageEnable = false;
    bool alphaToOneEnable      = false;

    bool      depthTestEnable       = false;
    bool      depthWriteEnable      = false;
    CompareOp depthCompareOp        = CompareOp::Always;
    bool      depthBoundsTestEnable = false;
    float     depthMinBounds        = 0.f;
    float     depthMaxBounds        = 0.f;

    bool           stencilTestEnable = false;
    StencilOpState stencilFrontOp    = {};
    StencilOpState stencilBackOp     = {};

    bool                     blendLogicOpEnable = false;
    LogicOp                  blendLogicOp       = LogicOp::COPY;
    Arr<AttachmentBlendDesc> blendAttachments   = {};
    Float4                   blendConstants     = {};

    bool dynamicViewport           = false;
    bool dynamicScissor            = false;
    bool dynamicLineWidth          = false;
    bool dynamicDepthBias          = false;
    bool dynamicBlendConstants     = false;
    bool dynamicDepthBounds        = false;
    bool dynamicStencilCompareMask = false;
    bool dynamicStencilWriteMask   = false;
    bool dynamicStencilReference   = false;
  };

  struct ComputeDesc {
    Shader* CS;
  };

public:
  VD_NONMOVABLE(Pipeline);

  Pipeline(Device& device, const GraphicsDesc& desc);
  Pipeline(Device& device, const ComputeDesc& desc);
  ~Pipeline();

  void Bind(CommandBuffer& cmd);

private:
  void createGraphicsPipeline();
  void createComputePipeline();

private:
  Device& m_device;

  BindPoint                      m_bindPoint;
  Var<GraphicsDesc, ComputeDesc> m_desc;

#ifdef VD_API_VK
  VkPipeline m_handle;
#endif

#ifdef VD_API_DX
  ComPtr<ID3D12PipelineState> m_handle;
#endif
};

} // namespace vd
