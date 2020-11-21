#include "vuldir/api/Binder.hpp"
#include "vuldir/api/CommandBuffer.hpp"
#include "vuldir/api/Device.hpp"
#include "vuldir/api/Pipeline.hpp"
#include "vuldir/api/Shader.hpp"
#include "vuldir/api/dx/DxUti.hpp"

using namespace vd;

Pipeline::Pipeline(Device& device, const GraphicsDesc& desc):
  m_device{device}, m_desc{desc}, m_handle{}
{
  VD_MARKER_SCOPED();

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
  const auto maxNumRenderTargets = vd::countOf32(psoDesc.RTVFormats);
  if(psoDesc.NumRenderTargets >= maxNumRenderTargets) {
    VDLogW(
      "Trying to use %ul render targets, but the maximum is %ul!"
      " Will truncate.",
      desc.colorFormats.size(), maxNumRenderTargets);
    psoDesc.NumRenderTargets = maxNumRenderTargets;
  }

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
  psoDesc.BlendState.IndependentBlendEnable = false;

  for(u32 idx = 0; idx < desc.blendAttachments.size(); ++idx) {
    auto& rt  = psoDesc.BlendState.RenderTarget[idx];
    auto& att = desc.blendAttachments[idx];

    if(idx > 0 && att.blendEnable)
      psoDesc.BlendState.IndependentBlendEnable = true;

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
  psoDesc.RasterizerState.DepthBias =
    static_cast<u32>(desc.depthBiasFactor);
  psoDesc.RasterizerState.DepthBiasClamp        = desc.depthBiasClamp;
  psoDesc.RasterizerState.SlopeScaledDepthBias  = desc.depthBiasSlope;
  psoDesc.RasterizerState.DepthClipEnable       = desc.depthClampEnable;
  psoDesc.RasterizerState.MultisampleEnable     = desc.sampleCount > 1u;
  psoDesc.RasterizerState.AntialiasedLineEnable = desc.lineAntiAlias;
  psoDesc.RasterizerState.ForcedSampleCount     = 0u;
  psoDesc.RasterizerState.ConservativeRaster =
    desc.conservativeRaster ? D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON
                            : D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

  psoDesc.IBStripCutValue =
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;

  psoDesc.SampleDesc.Count   = desc.sampleCount;
  psoDesc.SampleDesc.Quality = desc.sampleQuality;
  psoDesc.SampleMask         = desc.sampleMask;

  VDDxTry(m_device.api().CreateGraphicsPipelineState(
    &psoDesc, IID_PPV_ARGS(&m_handle)));

  if(!desc.name.empty()) m_handle->SetName(widen(desc.name).c_str());
}

Pipeline::Pipeline(Device& device, const ComputeDesc& desc):
  m_device{device}, m_desc{desc}, m_handle{}
{}

Pipeline::~Pipeline() {}

void Pipeline::Bind(CommandBuffer& cmd)
{
  VD_MARKER_SCOPED();

  cmd.GetHandle().SetGraphicsRootSignature(
    m_device.GetBinder().GetRootSignature());

  // TODO: set other stuff like input assembler etc.
  if(std::holds_alternative<GraphicsDesc>(m_desc)) {
    auto& desc = std::get<GraphicsDesc>(m_desc);
    cmd.GetHandle().IASetPrimitiveTopology(convert(desc.topology));
  }

  cmd.GetHandle().SetPipelineState(m_handle.Get());
}
