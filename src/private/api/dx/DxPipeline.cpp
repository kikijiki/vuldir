#include "vuldir/api/Binder.hpp"
#include "vuldir/api/CommandBuffer.hpp"
#include "vuldir/api/Device.hpp"
#include "vuldir/api/Pipeline.hpp"
#include "vuldir/api/Shader.hpp"
#include "vuldir/api/dx/DxUti.hpp"

using namespace vd;

Pipeline::Pipeline(Device& device, const GraphicsDesc& desc):
  m_device{device},
  m_bindPoint{BindPoint::Graphics},
  m_desc{desc},
  m_handle{}
{
  VD_MARKER_SCOPED();

  if(desc.colorFormats.size() > 8u)
    throw std::invalid_argument(
      "Graphics pipeline cannot have more than eight color attachments");
  if(desc.blendAttachments.size() != desc.colorFormats.size())
    throw std::invalid_argument(
      "Color formats and blend attachments must have matching counts");
  for(const auto format: desc.colorFormats) {
    if(
      format == Format::UNDEFINED ||
      getFormatAspect(format) != ImageAspect::Color)
      throw std::invalid_argument("Color attachment format is invalid");
  }
  if(
    desc.depthStencilFormat != Format::UNDEFINED &&
    getFormatAspect(desc.depthStencilFormat) == ImageAspect::Color)
    throw std::invalid_argument("Depth attachment format is invalid");
  if(
    desc.sampleCount == 0u ||
    (desc.sampleCount & (desc.sampleCount - 1u)) != 0u ||
    desc.sampleCount > 64u)
    throw std::invalid_argument("Pipeline sample count is invalid");
  if(!std::isfinite(desc.lineWidth) || desc.lineWidth <= 0.f)
    throw std::invalid_argument("Pipeline line width must be positive");
  if(desc.lineWidth != 1.f)
    throw std::invalid_argument("DX12 does not support wide rasterized lines");
  if(desc.alphaToOneEnable)
    throw std::invalid_argument("DX12 does not support alpha-to-one");
  if(desc.sampleQuality != 0u)
    throw std::invalid_argument(
      "DX12 image sample quality is fixed to zero");
  if(desc.depthBoundsTestEnable)
    throw std::invalid_argument(
      "DX12 depth bounds testing is not implemented");
  if(
    desc.dynamicLineWidth || desc.dynamicDepthBias ||
    desc.dynamicBlendConstants || desc.dynamicDepthBounds ||
    desc.dynamicStencilCompareMask || desc.dynamicStencilWriteMask ||
    desc.dynamicStencilReference)
    throw std::invalid_argument(
      "Pipeline requests a dynamic state with no command API setter");
  if(
    !std::isfinite(desc.depthBiasFactor) ||
    !std::isfinite(desc.depthBiasClamp) ||
    !std::isfinite(desc.depthBiasSlope) ||
    desc.depthBiasFactor < static_cast<f32>(std::numeric_limits<INT>::min()) ||
    desc.depthBiasFactor > static_cast<f32>(std::numeric_limits<INT>::max()))
    throw std::invalid_argument("Pipeline depth bias is invalid");
  if(desc.conservativeRaster) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    if(
      FAILED(m_device.api().CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))) ||
      options.ConservativeRasterizationTier ==
        D3D12_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED)
      throw std::invalid_argument(
        "DX12 conservative rasterization is unsupported");
  }
  const auto validateSamples = [&](Format format) {
    if(format == Format::UNDEFINED) return;
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels{
      .Format = convert(format),
      .SampleCount = desc.sampleCount,
      .Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE};
    if(
      FAILED(m_device.api().CheckFeatureSupport(
        D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels,
        sizeof(levels))) ||
      desc.sampleQuality >= levels.NumQualityLevels)
      throw std::invalid_argument(
        "DX12 pipeline sample count or quality is unsupported");
  };
  for(const auto format: desc.colorFormats) validateSamples(format);
  validateSamples(desc.depthStencilFormat);
  if(!desc.VS)
    throw std::invalid_argument("Graphics pipeline requires a vertex shader");
  if(&desc.VS->GetDevice() != &m_device ||
     (desc.PS && &desc.PS->GetDevice() != &m_device))
    throw std::invalid_argument(
      "Pipeline shaders belong to another device");

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};

  psoDesc.pRootSignature = m_device.GetBinder().GetRootSignature();

  switch(desc.topology) {
    case PrimitiveTopology::PointList:
      psoDesc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
      break;
    case PrimitiveTopology::LineList:
    case PrimitiveTopology::LineStrip:
    case PrimitiveTopology::LineListAdj:
    case PrimitiveTopology::LineStripAdj:
      psoDesc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
      break;
    case PrimitiveTopology::TriangleList:
    case PrimitiveTopology::TriangleStrip:
    case PrimitiveTopology::TriangleListAdj:
    case PrimitiveTopology::TriangleStripAdj:
      psoDesc.PrimitiveTopologyType =
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      break;
    default:
      throw std::runtime_error("Invalid primitive topology");
  }

  if(desc.VS) psoDesc.VS = desc.VS->GetHandle();
  if(desc.PS) psoDesc.PS = desc.PS->GetHandle();

  psoDesc.DSVFormat = convert(desc.depthStencilFormat);

  psoDesc.NumRenderTargets       = vd::size32(desc.colorFormats);

  for(u32 idx = 0u; idx < psoDesc.NumRenderTargets; ++idx)
    psoDesc.RTVFormats[idx] = vd::convert(desc.colorFormats[idx]);

  psoDesc.DepthStencilState.DepthEnable = desc.depthTestEnable;
  psoDesc.DepthStencilState.DepthWriteMask =
    desc.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL
                          : D3D12_DEPTH_WRITE_MASK_ZERO;
  psoDesc.DepthStencilState.DepthFunc = convert(desc.depthCompareOp);
  psoDesc.DepthStencilState.StencilEnable = desc.stencilTestEnable;
  psoDesc.DepthStencilState.StencilReadMask =
    D3D12_DEFAULT_STENCIL_READ_MASK;
  psoDesc.DepthStencilState.StencilWriteMask =
    D3D12_DEFAULT_STENCIL_WRITE_MASK;
  psoDesc.DepthStencilState.FrontFace = convert(desc.stencilFrontOp);
  psoDesc.DepthStencilState.BackFace  = convert(desc.stencilBackOp);

  psoDesc.BlendState.AlphaToCoverageEnable = desc.alphaToCoverageEnable;
  psoDesc.BlendState.IndependentBlendEnable =
    desc.blendAttachments.size() > 1u;

  for(u32 idx = 0; idx < desc.blendAttachments.size(); ++idx) {
    auto& rt  = psoDesc.BlendState.RenderTarget[idx];
    auto& att = desc.blendAttachments[idx];

    rt.BlendEnable    = att.blendEnable;
    rt.LogicOpEnable  = desc.blendLogicOpEnable;
    rt.SrcBlend       = convert(att.srcColorBlendFactor);
    rt.DestBlend      = convert(att.dstColorBlendFactor);
    rt.BlendOp        = convert(att.colorBlendOp);
    rt.SrcBlendAlpha  = convert(att.srcAlphaBlendFactor);
    rt.DestBlendAlpha = convert(att.dstAlphaBlendFactor);
    rt.BlendOpAlpha   = convert(att.alphaBlendOp);
    rt.LogicOp        = convert(desc.blendLogicOp);

    if(att.writeA)
      rt.RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_ALPHA;
    if(att.writeR)
      rt.RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_RED;
    if(att.writeG)
      rt.RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_GREEN;
    if(att.writeB)
      rt.RenderTargetWriteMask |= D3D12_COLOR_WRITE_ENABLE_BLUE;
  }

  psoDesc.RasterizerState.FillMode = convert(desc.polygonMode);
  psoDesc.RasterizerState.CullMode = convert(desc.cullMode);
  psoDesc.RasterizerState.FrontCounterClockwise =
    desc.frontFace == FrontFace::CCW;
  psoDesc.RasterizerState.DepthBias = desc.depthBiasEnable
                                        ? static_cast<INT>(desc.depthBiasFactor)
                                        : D3D12_DEFAULT_DEPTH_BIAS;
  psoDesc.RasterizerState.DepthBiasClamp =
    desc.depthBiasEnable ? desc.depthBiasClamp : D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
  psoDesc.RasterizerState.SlopeScaledDepthBias =
    desc.depthBiasEnable ? desc.depthBiasSlope
                         : D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
  psoDesc.RasterizerState.DepthClipEnable       = !desc.depthClampEnable;
  psoDesc.RasterizerState.MultisampleEnable     = desc.sampleCount > 1u;
  psoDesc.RasterizerState.AntialiasedLineEnable = desc.lineAntiAlias;
  psoDesc.RasterizerState.ForcedSampleCount     = 0u;
  psoDesc.RasterizerState.ConservativeRaster =
    desc.conservativeRaster ? D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON
                            : D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

  psoDesc.IBStripCutValue =
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
  if(desc.discardEnable)
    psoDesc.StreamOutput.RasterizedStream =
      D3D12_SO_NO_RASTERIZED_STREAM;

  psoDesc.SampleDesc.Count   = desc.sampleCount;
  psoDesc.SampleDesc.Quality = desc.sampleQuality;
  psoDesc.SampleMask         = desc.sampleMask;

  VDDxTry(m_device.api().CreateGraphicsPipelineState(
    &psoDesc, IID_PPV_ARGS(&m_handle)));

  if(!desc.name.empty()) m_handle->SetName(widen(desc.name).c_str());
  releaseShaderRefs();
  ++m_device.m_pipelineCount;
}

Pipeline::Pipeline(Device& device, const ComputeDesc& desc):
  m_device{device},
  m_bindPoint{BindPoint::Compute},
  m_desc{desc},
  m_handle{}
{
  if(!desc.CS)
    throw std::invalid_argument("Compute pipeline requires a shader");
  if(&desc.CS->GetDevice() != &m_device)
    throw std::invalid_argument(
      "Pipeline shader belongs to another device");

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
  psoDesc.pRootSignature = m_device.GetBinder().GetRootSignature();
  psoDesc.CS             = desc.CS->GetHandle();
  VDDxTry(m_device.api().CreateComputePipelineState(
    &psoDesc, IID_PPV_ARGS(&m_handle)));
  releaseShaderRefs();
  ++m_device.m_pipelineCount;
}

Pipeline::~Pipeline() { --m_device.m_pipelineCount; }

void Pipeline::Bind(CommandBuffer& cmd)
{
  VD_MARKER_SCOPED();
  cmd.requireRecording("Pipeline::Bind");

  if(&cmd.m_device != &m_device)
    throw std::invalid_argument(
      "Pipeline belongs to another device");
  if(
    m_bindPoint == BindPoint::Graphics &&
    cmd.GetQueueType() != QueueType::Graphics)
    throw std::invalid_argument(
      "Graphics pipelines require a graphics command buffer");
  if(cmd.GetQueueType() == QueueType::Copy)
    throw std::invalid_argument(
      "Pipelines cannot be bound to a copy command buffer");

  m_device.GetBinder().Bind(cmd, m_bindPoint);
  cmd.m_bindPoint     = m_bindPoint;
  cmd.m_pipelineBound = true;

  // TODO: set other stuff like input assembler etc.
  if(std::holds_alternative<GraphicsDesc>(m_desc)) {
    auto& desc = std::get<GraphicsDesc>(m_desc);
    cmd.GetHandle().IASetPrimitiveTopology(convert(desc.topology));
  }

  cmd.GetHandle().SetPipelineState(m_handle.Get());
}
