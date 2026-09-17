#include "vuldir/api/Binder.hpp"
#include "vuldir/api/CommandBuffer.hpp"
#include "vuldir/api/Device.hpp"
#include "vuldir/api/Pipeline.hpp"
#include "vuldir/api/PhysicalDevice.hpp"
#include "vuldir/api/Shader.hpp"
#include "vuldir/api/vk/VkDispatcher.hpp"
#include "vuldir/api/vk/VkUti.hpp"

using namespace vd;

Pipeline::Pipeline(Device& device, const GraphicsDesc& desc):
  m_device{device},
  m_bindPoint{BindPoint::Graphics},
  m_desc{desc},
  m_handle{}
{
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
  if(desc.conservativeRaster)
    throw std::invalid_argument(
      "Vulkan conservative rasterization is not enabled");
  if(desc.lineAntiAlias)
    throw std::invalid_argument("Vulkan line antialiasing is not enabled");
  if(desc.sampleQuality != 0u)
    throw std::invalid_argument(
      "Vulkan does not expose multisample quality levels");
  if(
    desc.dynamicLineWidth || desc.dynamicDepthBias ||
    desc.dynamicBlendConstants || desc.dynamicDepthBounds ||
    desc.dynamicStencilCompareMask || desc.dynamicStencilWriteMask ||
    desc.dynamicStencilReference)
    throw std::invalid_argument(
      "Pipeline requests a dynamic state with no command API setter");
  if(
    desc.depthBoundsTestEnable &&
    (!std::isfinite(desc.depthMinBounds) ||
     !std::isfinite(desc.depthMaxBounds) || desc.depthMinBounds < 0.f ||
     desc.depthMaxBounds > 1.f ||
     desc.depthMinBounds > desc.depthMaxBounds))
    throw std::invalid_argument("Pipeline depth bounds are invalid");
  if(
    !std::isfinite(desc.depthBiasFactor) ||
    !std::isfinite(desc.depthBiasClamp) ||
    !std::isfinite(desc.depthBiasSlope))
    throw std::invalid_argument("Pipeline depth bias is not finite");
  if(
    !desc.dynamicViewport &&
    (!std::isfinite(desc.viewport.offset[0]) ||
     !std::isfinite(desc.viewport.offset[1]) ||
     !std::isfinite(desc.viewport.extent[0]) ||
     !std::isfinite(desc.viewport.extent[1]) ||
     desc.viewport.extent[0] <= 0.f || desc.viewport.extent[1] == 0.f ||
     !std::isfinite(desc.viewport.depthExtent[0]) ||
     !std::isfinite(desc.viewport.depthExtent[1]) ||
     desc.viewport.depthExtent[0] < 0.f ||
     desc.viewport.depthExtent[1] > 1.f ||
     desc.viewport.depthExtent[0] > desc.viewport.depthExtent[1]))
    throw std::invalid_argument("Static pipeline viewport is invalid");
  if(
    !desc.dynamicScissor &&
    (desc.scissor.extent[0] == 0u || desc.scissor.extent[1] == 0u))
    throw std::invalid_argument("Static pipeline scissor is empty");
  const auto& features = m_device.GetPhysicalDevice().GetFeatures();
  const auto& limits = m_device.GetPhysicalDevice().GetProperties()->limits;
  const VkSampleCountFlags samples =
    static_cast<VkSampleCountFlags>(desc.sampleCount);
  if(
    !desc.colorFormats.empty() &&
    (limits.framebufferColorSampleCounts & samples) == 0u)
    throw std::invalid_argument(
      "Pipeline color sample count is unsupported");
  if(
    desc.depthStencilFormat != Format::UNDEFINED &&
    (limits.framebufferDepthSampleCounts & samples) == 0u)
    throw std::invalid_argument(
      "Pipeline depth sample count is unsupported");
  if(desc.depthClampEnable && !features->depthClamp)
    throw std::invalid_argument("Depth clamp is unsupported");
  if(desc.depthBoundsTestEnable && !features->depthBounds)
    throw std::invalid_argument("Depth bounds testing is unsupported");
  if(desc.polygonMode != PolygonMode::Fill && !features->fillModeNonSolid)
    throw std::invalid_argument("Non-solid polygon modes are unsupported");
  if(desc.lineWidth != 1.f && !features->wideLines)
    throw std::invalid_argument("Wide lines are unsupported");
  if(desc.alphaToOneEnable && !features->alphaToOne)
    throw std::invalid_argument("Alpha-to-one is unsupported");
  if(desc.blendLogicOpEnable && !features->logicOp)
    throw std::invalid_argument("Color logic operations are unsupported");
  if(!desc.VS)
    throw std::invalid_argument("Graphics pipeline requires a vertex shader");
  if(&desc.VS->GetDevice() != &m_device ||
     (desc.PS && &desc.PS->GetDevice() != &m_device))
    throw std::invalid_argument(
      "Pipeline shaders belong to another device");

  Arr<VkPipelineShaderStageCreateInfo> stages;

  if(desc.VS) {
    VkPipelineShaderStageCreateInfo vs{
      .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage  = VK_SHADER_STAGE_VERTEX_BIT,
      .module = desc.VS->GetHandle(),
      .pName  = "MainVS"};
    stages.push_back(vs);
  }

  if(desc.PS) {
    VkPipelineShaderStageCreateInfo ps{
      .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = desc.PS->GetHandle(),
      .pName  = "MainPS"};
    stages.push_back(ps);
  }

  VkPipelineVertexInputStateCreateInfo vertexInputState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{
    .sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .topology = convert(desc.topology)};

  const auto viewport = convert(desc.viewport);
  const auto scissor  = convert(desc.scissor);

  VkPipelineViewportStateCreateInfo viewportState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .viewportCount = 1u,
    .pViewports    = &viewport,
    .scissorCount  = 1u,
    .pScissors     = &scissor};

  VkPipelineRasterizationStateCreateInfo rasterizationState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .depthClampEnable        = desc.depthClampEnable,
    .rasterizerDiscardEnable = desc.discardEnable,
    .polygonMode             = convert(desc.polygonMode),
    .cullMode                = convert(desc.cullMode),
    .frontFace               = convert(desc.frontFace),
    .depthBiasEnable         = desc.depthBiasEnable,
    .depthBiasConstantFactor = desc.depthBiasFactor,
    .depthBiasClamp          = desc.depthBiasClamp,
    .depthBiasSlopeFactor    = desc.depthBiasSlope,
    .lineWidth               = desc.lineWidth};

  VkPipelineMultisampleStateCreateInfo multisampleState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .rasterizationSamples =
      static_cast<VkSampleCountFlagBits>(desc.sampleCount),
    .pSampleMask           = &desc.sampleMask,
    .alphaToCoverageEnable = desc.alphaToCoverageEnable,
    .alphaToOneEnable      = desc.alphaToOneEnable};

  VkPipelineDepthStencilStateCreateInfo depthStencilState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    .depthTestEnable       = desc.depthTestEnable,
    .depthWriteEnable      = desc.depthWriteEnable,
    .depthCompareOp        = convert(desc.depthCompareOp),
    .depthBoundsTestEnable = desc.depthBoundsTestEnable,
    .stencilTestEnable     = desc.stencilTestEnable,
    .front                 = convert(desc.stencilFrontOp),
    .back                  = convert(desc.stencilBackOp),
    .minDepthBounds        = desc.depthMinBounds,
    .maxDepthBounds        = desc.depthMaxBounds};

  Arr<VkPipelineColorBlendAttachmentState> blendAttachments;
  blendAttachments.reserve(std::size(desc.blendAttachments));
  for(const auto& att: desc.blendAttachments) {
    VkPipelineColorBlendAttachmentState vkatt{
      .blendEnable         = att.blendEnable,
      .srcColorBlendFactor = convert(att.srcColorBlendFactor),
      .dstColorBlendFactor = convert(att.dstColorBlendFactor),
      .colorBlendOp        = convert(att.colorBlendOp),
      .srcAlphaBlendFactor = convert(att.srcAlphaBlendFactor),
      .dstAlphaBlendFactor = convert(att.dstAlphaBlendFactor),
      .alphaBlendOp        = convert(att.alphaBlendOp),
      .colorWriteMask      = 0u};

    if(att.writeR) vkatt.colorWriteMask |= VK_COLOR_COMPONENT_R_BIT;
    if(att.writeG) vkatt.colorWriteMask |= VK_COLOR_COMPONENT_G_BIT;
    if(att.writeB) vkatt.colorWriteMask |= VK_COLOR_COMPONENT_B_BIT;
    if(att.writeA) vkatt.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;

    blendAttachments.push_back(vkatt);
  }

  VkPipelineColorBlendStateCreateInfo colorBlendState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .logicOpEnable   = desc.blendLogicOpEnable,
    .logicOp         = convert(desc.blendLogicOp),
    .attachmentCount = vd::size32(blendAttachments),
    .pAttachments    = std::data(blendAttachments)};

  colorBlendState.blendConstants[0] = desc.blendConstants[0];
  colorBlendState.blendConstants[1] = desc.blendConstants[1];
  colorBlendState.blendConstants[2] = desc.blendConstants[2];
  colorBlendState.blendConstants[3] = desc.blendConstants[3];

  Arr<VkDynamicState> vkDynamicStates;
  if(desc.dynamicViewport)
    vkDynamicStates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
  if(desc.dynamicScissor)
    vkDynamicStates.push_back(VK_DYNAMIC_STATE_SCISSOR);
  if(desc.dynamicLineWidth)
    vkDynamicStates.push_back(VK_DYNAMIC_STATE_LINE_WIDTH);
  if(desc.dynamicDepthBias)
    vkDynamicStates.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
  if(desc.dynamicBlendConstants)
    vkDynamicStates.push_back(VK_DYNAMIC_STATE_BLEND_CONSTANTS);
  if(desc.dynamicDepthBounds)
    vkDynamicStates.push_back(VK_DYNAMIC_STATE_DEPTH_BOUNDS);
  if(desc.dynamicStencilCompareMask)
    vkDynamicStates.push_back(VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK);
  if(desc.dynamicStencilWriteMask)
    vkDynamicStates.push_back(VK_DYNAMIC_STATE_STENCIL_WRITE_MASK);
  if(desc.dynamicStencilReference)
    vkDynamicStates.push_back(VK_DYNAMIC_STATE_STENCIL_REFERENCE);

  VkPipelineDynamicStateCreateInfo dynamicState{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .dynamicStateCount = vd::size32(vkDynamicStates),
    .pDynamicStates    = std::data(vkDynamicStates)};

  // https://www.khronos.org/blog/streamlining-render-passes
  // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_KHR_dynamic_rendering.html

  const auto colorFormats = Transform<VkFormat>::ToVec(
    desc.colorFormats, [](auto f) { return convert(f); });

  VkPipelineRenderingCreateInfo renderingCI{
    .sType    = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    .pNext    = nullptr,
    .viewMask = 0u,
    .colorAttachmentCount    = vd::size32(colorFormats),
    .pColorAttachmentFormats = std::data(colorFormats),
    .depthAttachmentFormat   = vd::getVkDepth(desc.depthStencilFormat),
    .stencilAttachmentFormat =
      vd::getVkStencil(desc.depthStencilFormat)};

  VkGraphicsPipelineCreateInfo ci{
    .sType      = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .pNext      = &renderingCI,
    .stageCount = vd::size32(stages),
    .pStages    = std::data(stages),
    .pVertexInputState   = &vertexInputState,
    .pInputAssemblyState = &inputAssemblyState,
    .pTessellationState  = nullptr,
    .pViewportState      = &viewportState,
    .pRasterizationState = &rasterizationState,
    .pMultisampleState   = &multisampleState,
    .pDepthStencilState  = &depthStencilState,
    .pColorBlendState    = &colorBlendState,
    .pDynamicState       = &dynamicState,
    .layout              = m_device.GetBinder().GetPipelineLayout(),
    .renderPass          = VK_NULL_HANDLE,
    .subpass             = 0u,
    .basePipelineHandle  = nullptr,
    .basePipelineIndex   = 0u};

  VDVkTry(m_device.api().CreateGraphicsPipelines(
    nullptr, 1u, &ci, &m_handle));
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

  VkPipelineCreateFlags flags = 0u;

  VkPipelineShaderStageCreateInfo stage{
    .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
    .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
    .module = desc.CS->GetHandle(),
    .pName  = "MainCS"};

  VkComputePipelineCreateInfo ci{
    .sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .flags  = flags,
    .stage  = stage,
    .layout = m_device.GetBinder().GetPipelineLayout()};

  VDVkTry(
    m_device.api().CreateComputePipelines(nullptr, 1u, &ci, &m_handle));
  releaseShaderRefs();
  ++m_device.m_pipelineCount;
}

Pipeline::~Pipeline()
{
  --m_device.m_pipelineCount;
  if(m_handle) {
    m_device.api().DestroyPipeline(m_handle);
    m_handle = nullptr;
  }
}

void Pipeline::Bind(CommandBuffer& cmd)
{
  cmd.requireRecording("Pipeline::Bind");
  VD_MARKER_SCOPED();

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
  m_device.api().CmdBindPipeline(
    cmd.GetHandle(), convert(m_bindPoint), m_handle);
}
