#pragma once

#include "Window.hpp"
#include "vuldir/Vuldir.hpp"

#include <fstream>
#include <numbers>

static Arr<char> ReadFile(char const* path)
{
  std::ifstream ifs(path, std::ios::binary | std::ios::ate);
  if(!ifs) return {};

  const auto pos = ifs.tellg();
  Arr<char>  buf(pos);

  ifs.seekg(0, std::ios::beg);
  ifs.read(buf.data(), pos);

  return buf;
}

UPtr<Shader> LoadShader(Device& device, const char* path)
{
  const auto data = ReadFile(path);
  if(data.empty()) throw std::runtime_error("Could not find the file");

  return std::make_unique<Shader>(device, data);
}

UPtr<Image> LoadImage(Device& device, RenderContext&, const char* path)
{
  DataReader reader;
  const auto data   = reader.ReadImage(path, {});
  const auto extent = {data.size[0], data.size[1], 1u};

  Image::Desc desc{
    .name        = path,
    .usage       = ResourceUsage::ShaderResource,
    .format      = data.format,
    .dimension   = Dimension::e2D,
    .extent      = extent,
    .defaultView = ViewType::SRV};

  return std::make_unique<Image>(device, desc);
}

struct SceneParam {
  Float44 view;
  Float44 viewProjection;
  Float3  cameraPosition;

  i32 gbufIdx0;
  i32 gbufIdx1;
  i32 gbufIdx2;
};

struct ObjectParam {
  Float44 world;

  i32 vbPosNrmIdx;
  i32 vbTanIdx;
  i32 vbUV0Idx;
  i32 materialIdx;
};

struct MaterialParam {
  i32 colIdx;
  i32 nrmIdx;

  f32 metallic;
  f32 roughness;
};

struct EmptyMeta {};
template<typename T, typename M = EmptyMeta>
struct UniformBuffer {
  UPtr<Buffer> buffer;
  T            data;
  M            meta;

  UniformBuffer(Device& device, const Buffer::Desc& desc):
    buffer(std::make_unique<Buffer>(device, desc)), data{}, meta{}
  {}

  ~UniformBuffer() = default;

  void Update() { buffer->Write(data); }
};

enum Pass { PassDepth, PassGBuffer, PassLighting, PassCount };

static constexpr u32 GBufferCount = 3;

static constexpr SArr<Format, GBufferCount> GBufferFormats = {
  Format::R32G32B32A32_SFLOAT, Format::R32G32B32A32_SFLOAT,
  Format::R32G32B32A32_SFLOAT};

struct FrameResources {
  UPtr<UniformBuffer<SceneParam>> scene;

  UPtr<Image>      depthStencil;
  Arr<UPtr<Image>> gbuffers;

  struct {
    Pass                           id;
    Pipeline*                      pipeline;
    CommandBuffer::Attachment      depthStencil;
    Arr<CommandBuffer::Attachment> color;
  } passes[PassCount];
};

struct Resources {
  Arr<UPtr<Pipeline>> pipelines;

  Arr<UPtr<Buffer>> bufMeshes; // TODO
  Arr<UPtr<Buffer>> bufPosNrm;
  Arr<UPtr<Buffer>> bufMaterials;

  Arr<UPtr<UniformBuffer<ObjectParam, u32>>> objects;

  Arr<FrameResources> frames;
};

Resources createResources(Device& dev, u32 frames)
{
  Resources res{};

  res.frames.resize(frames);
  auto depth = std::make_unique<Pipeline>(
    dev, Pipeline::GraphicsDesc{
           .name = "Depth",
           .VS   = LoadShader(dev, "Shaders/RP_Depth.vs.cso").get(),
           .depthStencilFormat = Format::D32_SFLOAT_S8_UINT,
           .colorFormats       = {},
           //.cullMode       = CullMode::Back,
           //.frontFace      = FrontFace::CCW,
           .depthTestEnable  = true,
           .depthWriteEnable = true,
           .depthCompareOp   = CompareOp::Less,
           .dynamicViewport  = true,
           .dynamicScissor   = true});

  for(auto& frame: res.frames) {
    frame.passes[PassDepth].id       = PassDepth;
    frame.passes[PassDepth].pipeline = depth.get();
  }
  res.pipelines.push_back(std::move(depth));

  auto gbuffer = std::make_unique<Pipeline>(
    dev, Pipeline::GraphicsDesc{
           .name = "GBuffer",
           .VS   = LoadShader(dev, "Shaders/RP_GBuffer.vs.cso").get(),
           .PS   = LoadShader(dev, "Shaders/RP_GBuffer.ps.cso").get(),
           .depthStencilFormat = Format::D32_SFLOAT_S8_UINT,
           .colorFormats =
             {std::begin(GBufferFormats), std::end(GBufferFormats)},
           //.cullMode      = CullMode::Back,
           //.frontFace     = FrontFace::CCW,
           .depthTestEnable  = true,
           .depthCompareOp   = CompareOp::LessOrEqual,
           .blendAttachments = {{}, {}, {}},
           .dynamicViewport  = true,
           .dynamicScissor   = true});

  for(auto& frame: res.frames) {
    frame.passes[PassGBuffer].id       = PassGBuffer;
    frame.passes[PassGBuffer].pipeline = gbuffer.get();
  }
  res.pipelines.push_back(std::move(gbuffer));

  auto lighting = std::make_unique<Pipeline>(
    dev, Pipeline::GraphicsDesc{
           .name = "Lighting",
           .VS   = LoadShader(dev, "Shaders/RP_Lighting.vs.cso").get(),
           .PS   = LoadShader(dev, "Shaders/RP_Lighting.ps.cso").get(),
           .depthStencilFormat = Format::D32_SFLOAT_S8_UINT,
           .colorFormats       = {Format::B8G8R8A8_UNORM},
           //.cullMode       = CullMode::Back,
           //.frontFace      = FrontFace::CCW,
           .blendAttachments = {{}},
           .dynamicViewport  = true,
           .dynamicScissor   = true});

  for(auto& frame: res.frames) {
    frame.passes[PassLighting].id       = PassLighting;
    frame.passes[PassLighting].pipeline = lighting.get();
  }
  res.pipelines.push_back(std::move(lighting));

  res.bufPosNrm.push_back(std::make_unique<Buffer>(
    dev, Buffer::Desc{
           .name        = "Mesh vertices",
           .usage       = ResourceUsage::ShaderResource,
           .size        = 36 * sizeof(Float4),
           .defaultView = ViewType::SRV,
           .memoryType  = MemoryType::Upload}));

  res.bufMaterials.push_back(std::make_unique<Buffer>(
    dev, Buffer::Desc{
           .name        = "Material buffer",
           .usage       = ResourceUsage::ShaderResource,
           .size        = 1024u,
           .defaultView = ViewType::SRV,
           .memoryType  = MemoryType::Upload}));

  u32 frameIdx = 0u;
  for(auto& frame: res.frames) {
    frame.scene = std::make_unique<UniformBuffer<SceneParam>>(
      dev, Buffer::Desc{
             .name        = formatString("Scene [frame %u]", frameIdx),
             .usage       = ResourceUsage::ShaderResource,
             .size        = sizeof(SceneParam),
             .defaultView = ViewType::SRV,
             .memoryType  = MemoryType::Upload});
    ++frameIdx;
  }

  return res;
}

void createSwapchainDependentResources(Device& dev, Resources& res)
{
  const auto scExtent = dev.GetSwapchain().GetExtent();

  u32 frameIdx = 0;
  for(auto& frame: res.frames) {
    for(auto& pass: frame.passes) { pass.color.clear(); }

    // Depth create
    frame.depthStencil.reset();
    frame.depthStencil = std::make_unique<Image>(
      dev, Image::Desc{
             .name  = formatString("DepthStencil [frame %u]", frameIdx),
             .usage = ResourceUsage::DepthStencil |
                      ResourceUsage::ShaderResource,
             .format      = Format::D32_SFLOAT_S8_UINT,
             .dimension   = Dimension::e2D,
             .extent      = {scExtent, 1u},
             .defaultView = ViewType::DSV});

    // Depth attachments
    frame.passes[PassDepth].depthStencil = {
      .view       = frame.depthStencil->GetView(),
      .state      = ResourceState::DepthStencilRW,
      .loadOp     = LoadOp::Clear,
      .storeOp    = StoreOp::Store,
      .clearValue = DepthStencil{1.0f, 0},
    };
    frame.passes[PassGBuffer].depthStencil = {
      .view    = frame.depthStencil->GetView(),
      .state   = ResourceState::DepthStencilRO,
      .loadOp  = LoadOp::Load,
      .storeOp = StoreOp::Store,
    };
    frame.passes[PassLighting].depthStencil = {
      .view    = frame.depthStencil->GetView(),
      .state   = ResourceState::DepthStencilRO,
      .loadOp  = LoadOp::Load,
      .storeOp = StoreOp::Store,
    };

    // GBuffer
    frame.gbuffers.clear();
    for(u32 idx = 0u; idx < GBufferCount; ++idx) {
      auto gbuf = std::make_unique<Image>(
        dev,
        Image::Desc{
          .name = formatString("GBuffer #%u [frame %u]", idx, frameIdx),
          .usage =
            ResourceUsage::RenderTarget | ResourceUsage::ShaderResource,
          .format    = GBufferFormats[idx],
          .dimension = Dimension::e2D,
          .extent    = {scExtent, 1u}});

      gbuf->AddView(ViewType::SRV);
      gbuf->AddView(ViewType::RTV);

      frame.passes[PassGBuffer].color.push_back({
        .view    = gbuf->GetView(ViewType::RTV),
        .state   = ResourceState::RenderTarget,
        .loadOp  = LoadOp::Clear,
        .storeOp = StoreOp::Store,
      });

      frame.gbuffers.push_back(std::move(gbuf));
    }

    frame.passes[PassLighting].color.push_back({
      .view    = dev.GetSwapchain().GetView(frameIdx),
      .state   = ResourceState::RenderTarget,
      .loadOp  = LoadOp::Clear,
      .storeOp = StoreOp::Store,
    });

    ++frameIdx;
  }
}

void prepareScene(RenderContext& ctx, Resources& res)
{
  [[maybe_unused]] auto s = sizeof(SceneParam);

  ctx.BeginTransfers();

  MaterialParam material{
    .colIdx = -1,
    .nrmIdx = -1,

    .metallic  = 0.f,
    .roughness = 0.f,
  };
  ctx.Write(*res.bufMaterials[0], material);

  const Float4 vertices[] = {
    {-1.0f, -1.0f, -1.0f, .0f}, {-1.0f, -1.0f, 1.0f, .0f},
    {-1.0f, 1.0f, 1.0f, .0f},   {-1.0f, 1.0f, 1.0f, .0f},
    {-1.0f, 1.0f, -1.0f, .0f},  {-1.0f, -1.0f, -1.0f, .0f},

    {-1.0f, -1.0f, -1.0f, .0f}, {1.0f, 1.0f, -1.0f, .0f},
    {1.0f, -1.0f, -1.0f, .0f},  {-1.0f, -1.0f, -1.0f, .0f},
    {-1.0f, 1.0f, -1.0f, .0f},  {1.0f, 1.0f, -1.0f, .0f},

    {-1.0f, -1.0f, -1.0f, .0f}, {1.0f, -1.0f, -1.0f, .0f},
    {1.0f, -1.0f, 1.0f, .0f},   {-1.0f, -1.0f, -1.0f, .0f},
    {1.0f, -1.0f, 1.0f, .0f},   {-1.0f, -1.0f, 1.0f, .0f},

    {-1.0f, 1.0f, -1.0f, .0f},  {-1.0f, 1.0f, 1.0f, .0f},
    {1.0f, 1.0f, 1.0f, .0f},    {-1.0f, 1.0f, -1.0f, .0f},
    {1.0f, 1.0f, 1.0f, .0f},    {1.0f, 1.0f, -1.0f, .0f},

    {1.0f, 1.0f, -1.0f, .0f},   {1.0f, 1.0f, 1.0f, .0f},
    {1.0f, -1.0f, 1.0f, .0f},   {1.0f, -1.0f, 1.0f, .0f},
    {1.0f, -1.0f, -1.0f, .0f},  {1.0f, 1.0f, -1.0f, .0f},

    {-1.0f, 1.0f, 1.0f, .0f},   {-1.0f, -1.0f, 1.0f, .0f},
    {1.0f, 1.0f, 1.0f, .0f},    {-1.0f, -1.0f, 1.0f, .0f},
    {1.0f, -1.0f, 1.0f, .0f},   {1.0f, 1.0f, 1.0f, .0f},
  };
  res.bufPosNrm[0]->Write(vertices);

  const auto cameraPos = vd::Float3{0, 3, 5};
  const auto cameraTgt = vd::Float3{0, 0, 0};
  const auto cameraUp  = vd::Float3{0, 1, 0};
  const auto cameraView =
    vd::mt::LookAt(cameraPos, cameraTgt, cameraUp);
  const auto cameraProj = vd::mt::PerspectiveFov(
    std::numbers::pi_v<f32> * .5f, 1.f, 1.f, 1000.f);

  for(auto& frame: res.frames) {
    frame.scene->data = {
      .view           = cameraView,
      .viewProjection = vd::mt::Mul(cameraView, cameraProj),
      .cameraPosition = cameraPos,
      .gbufIdx0       = static_cast<i32>(
        frame.gbuffers[0]->GetView(ViewType::SRV)->binding.index),
      .gbufIdx1 = static_cast<i32>(
        frame.gbuffers[1]->GetView(ViewType::SRV)->binding.index),
      .gbufIdx2 = static_cast<i32>(
        frame.gbuffers[2]->GetView(ViewType::SRV)->binding.index),
    };
    frame.scene->Update();
  }

  auto object = std::make_unique<UniformBuffer<ObjectParam, u32>>(
    ctx.GetDevice(), Buffer::Desc{
                       .name        = "Cube",
                       .usage       = ResourceUsage::ShaderResource,
                       .size        = sizeof(ObjectParam),
                       .defaultView = ViewType::SRV,
                       .memoryType  = MemoryType::Upload});

  object->data = {
    .world = vd::mt::Identity<Float44>(),

    .vbPosNrmIdx = static_cast<i32>(
      res.bufPosNrm[0]->GetView(ViewType::SRV)->binding.index),
    .vbTanIdx    = -1,
    .vbUV0Idx    = -1,
    .materialIdx = 0};

  object->meta = countOf32(vertices);

  object->Update();
  res.objects.push_back(std::move(object));

  ctx.EndTransfers();
}
