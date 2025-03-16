#include "Scene.hpp"

#include <fstream>
#include <numbers>

using namespace vd;

// Utility functions
Arr<char> ReadFile(char const* path)
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

UPtr<Image> LoadImage(Device& device, const char* path)
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

namespace vd {

Scene::Scene(Device& dev, u32 frameCount)
{
  // Initialize frame resources
  frames.resize(frameCount);

  // Create depth pass pipeline
  auto depth = std::make_unique<Pipeline>(
    dev, Pipeline::GraphicsDesc{
           .name = "Depth",
           .VS   = LoadShader(dev, "Shaders/RP_Depth.vs.cso").get(),
           .depthStencilFormat = Format::D32_SFLOAT_S8_UINT,
           .colorFormats       = {},
           .depthTestEnable    = true,
           .depthWriteEnable   = true,
           .depthCompareOp     = CompareOp::Less,
           .dynamicViewport    = true,
           .dynamicScissor     = true});

  for(auto& frame: frames) {
    frame.passes[PassDepth].id       = PassDepth;
    frame.passes[PassDepth].pipeline = depth.get();
  }
  pipelines.push_back(std::move(depth));

  Pipeline::AttachmentBlendDesc gbufferBlend{
    .blendEnable = false,
    .writeR      = true,
    .writeG      = true,
    .writeB      = true,
    .writeA      = true};

  // Create GBuffer pass pipeline
  auto gbuffer = std::make_unique<Pipeline>(
    dev,
    Pipeline::GraphicsDesc{
      .name = "GBuffer",
      .VS   = LoadShader(dev, "Shaders/RP_GBuffer.vs.cso").get(),
      .PS   = LoadShader(dev, "Shaders/RP_GBuffer.ps.cso").get(),
      .depthStencilFormat = Format::D32_SFLOAT_S8_UINT,
      .colorFormats =
        {std::begin(GBufferFormats), std::end(GBufferFormats)},
      .depthTestEnable  = true,
      .depthCompareOp   = CompareOp::LessOrEqual,
      .blendAttachments = {gbufferBlend, gbufferBlend, gbufferBlend},
      .dynamicViewport  = true,
      .dynamicScissor   = true});

  for(auto& frame: frames) {
    frame.passes[PassGBuffer].id       = PassGBuffer;
    frame.passes[PassGBuffer].pipeline = gbuffer.get();
  }
  pipelines.push_back(std::move(gbuffer));

  auto lighting = std::make_unique<Pipeline>(
    dev, Pipeline::GraphicsDesc{
           .name = "Lighting",
           .VS   = LoadShader(dev, "Shaders/RP_Lighting.vs.cso").get(),
           .PS   = LoadShader(dev, "Shaders/RP_Lighting.ps.cso").get(),
           .depthStencilFormat = Format::D32_SFLOAT_S8_UINT,
           .colorFormats       = {Format::B8G8R8A8_UNORM},
           .blendAttachments   = {{}},
           .dynamicViewport    = true,
           .dynamicScissor     = true});

  for(auto& frame: frames) {
    frame.passes[PassLighting].id       = PassLighting;
    frame.passes[PassLighting].pipeline = lighting.get();
  }
  pipelines.push_back(std::move(lighting));

  u32 frameIdx = 0u;
  for(auto& frame: frames) {
    frame.scene = std::make_unique<UniformBuffer<SceneParam>>(
      dev, Buffer::Desc{
             .name        = formatString("Scene [frame %u]", frameIdx),
             .usage       = ResourceUsage::ShaderResource,
             .size        = sizeof(SceneParam),
             .defaultView = ViewType::SRV,
             .memoryType  = MemoryType::Upload});
    ++frameIdx;
  }
}

void Scene::createSwapchainDependentResources(Device& dev)
{
  const auto scExtent = dev.GetSwapchain().GetExtent();

  u32 frameIdx = 0;
  for(auto& frame: frames) {
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

void Scene::prepare(RenderContext&)
{
  // Camera setup
  const auto cameraPos  = Float3{1, 1, 1};
  const auto cameraTgt  = Float3{0, 0, 0};
  const auto cameraUp   = Float3{0, 1, 0};
  const auto cameraView = mt::LookAt(cameraPos, cameraTgt, cameraUp);
  const auto cameraProj =
    mt::PerspectiveFov(std::numbers::pi_v<f32> * .5f, 1.f, 1.f, 1000.f);

  for(auto& frame: frames) {
    frame.scene->data = {
      .view           = cameraView,
      .viewProjection = mt::Mul(cameraView, cameraProj),
      .cameraPosition = cameraPos,
      .gbufIdx0       = static_cast<i32>(
        frame.gbuffers[0]->GetView(ViewType::SRV)->binding.index),
      .gbufIdx1 = static_cast<i32>(
        frame.gbuffers[1]->GetView(ViewType::SRV)->binding.index),
      .gbufIdx2 = static_cast<i32>(
        frame.gbuffers[2]->GetView(ViewType::SRV)->binding.index),
      .ambientColor              = {0.1f, 0.1f, 0.1f},
      .directionalLightDirection = mt::Norm(Float3{1.0f, -1.0f, 0.5f}),
      .directionalLightColor     = {1.0f, 1.0f, 1.0f},
      .directionalLightIntensity = 1.0f,
    };
    frame.scene->Update();
  }
}

void Scene::loadGltfModel(RenderContext& ctx, const Str& file)
{
  auto& dev = ctx.GetDevice();

  auto reader = DataReader(DataReader::Desc{});
  auto data   = reader.ReadModel(file, {});

  f32 scale = 40.0;

  // Create model
  Model model;

  // Load textures
  for(const auto& image: data.images) {
    auto tex = std::make_unique<Image>(
      dev, Image::Desc{
             .name        = image.uri ? *image.uri : "gltf texture",
             .usage       = ResourceUsage::ShaderResource,
             .format      = image.format,
             .dimension   = Dimension::e2D,
             .extent      = {image.size[0], image.size[1], 1u},
             .defaultView = ViewType::SRV});

    ctx.Write(*tex, image.texels);
    model.textures.push_back(std::move(tex));
  }

  // Create materials upfront
  for(u32 matIdx = 0; matIdx < data.materials.size(); ++matIdx) {
    auto material = std::make_unique<UniformBuffer<MaterialParam>>(
      dev, Buffer::Desc{
             .name        = "Material buffer",
             .usage       = ResourceUsage::ShaderResource,
             .size        = sizeof(MaterialParam),
             .defaultView = ViewType::SRV,
             .memoryType  = MemoryType::Upload});

    const auto& mat = data.materials[matIdx];
    if(
      const auto* pbrMat =
        std::get_if<data::PbrMetallicRoughness>(&mat.model)) {
      material->data.metallic  = pbrMat->metallicFactor;
      material->data.roughness = pbrMat->roughnessFactor;
      material->data.nrmIdx    = -1;
      material->data.colIdx    = -1;
      material->data.mrIdx     = -1;

      if(mat.normalTexture) {
        const auto idx = mat.normalTexture->textureIndex;
        if(idx < model.textures.size()) {
          material->data.nrmIdx =
            model.textures[idx]->GetView(ViewType::SRV)->binding.index;
        }
      }

      if(pbrMat->baseColorTexture) {
        const auto idx = pbrMat->baseColorTexture->textureIndex;
        if(idx < model.textures.size()) {
          material->data.colIdx = static_cast<i32>(
            model.textures[idx]->GetView(ViewType::SRV)->binding.index);
        }
      }

      if(pbrMat->metallicRoughnessTexture) {
        const auto idx = pbrMat->metallicRoughnessTexture->textureIndex;
        if(idx < model.textures.size()) {
          material->data.mrIdx =
            model.textures[idx]->GetView(ViewType::SRV)->binding.index;
        }
      }
    }

    material->Update();
    model.materials.push_back(std::move(material));
  }

  // Load all meshes
  for(const auto& dataMesh: data.meshes) {
    Mesh mesh;

    // For each primitive in the mesh
    for(const auto& prim: dataMesh.primitives) {
      if(!prim.material || *prim.material >= model.materials.size()) {
        continue;
      }

      Prim primitive;

      primitive.param = std::make_unique<UniformBuffer<PrimParam>>(
        dev, Buffer::Desc{
               .name        = "Primitive parameters",
               .usage       = ResourceUsage::ShaderResource,
               .size        = sizeof(PrimParam),
               .defaultView = ViewType::SRV,
               .memoryType  = MemoryType::Upload});

      primitive.param->data.world    = mt::Scaling(scale);
      primitive.param->data.vbPosIdx = -1;
      primitive.param->data.vbNrmIdx = -1;
      primitive.param->data.vbTanIdx = -1;
      primitive.param->data.vbUV0Idx = -1;
      primitive.param->data.materialIdx =
        static_cast<i32>(model.materials[*prim.material]
                           ->buffer->GetView(ViewType::SRV)
                           ->binding.index);

      for(const auto& attr: prim.attributes) {
        if(attr.type == VertexAttribute::Position) {
          Arr<Float4> vertices;

          const auto& acc    = data.accessors[attr.accessorIndex];
          const auto& view   = data.bufferViews[acc.bufferViewIndex];
          const auto& buffer = data.buffers[view.bufferIndex];

          // Read vertex positions
          const f32* positions = reinterpret_cast<const f32*>(
            buffer.data.data() + view.offset + acc.offset);

          for(u32 i = 0; i < acc.count; i++) {
            f32 x = positions[i * 3];
            f32 y = positions[i * 3 + 1];
            f32 z = positions[i * 3 + 2];

            vertices.push_back({x, y, z, 1.f});
          }

          if(vertices.empty()) continue;

          primitive.vbPos = std::make_unique<Buffer>(
            dev, Buffer::Desc{
                   .name        = "Mesh vertices",
                   .usage       = ResourceUsage::ShaderResource,
                   .size        = vertices.size() * sizeof(Float4),
                   .defaultView = ViewType::SRV,
                   .memoryType  = MemoryType::Main});

          ctx.Write(*primitive.vbPos, vertices);
          primitive.param->data.vbPosIdx = static_cast<i32>(
            primitive.vbPos->GetView(ViewType::SRV)->binding.index);

          // Handle indices if present
          if(prim.indicesAccessorIndex) {
            Arr<u16> indices;

            const auto& acc =
              data.accessors[*prim.indicesAccessorIndex];
            const auto& view   = data.bufferViews[acc.bufferViewIndex];
            const auto& buffer = data.buffers[view.bufferIndex];

            const u16* indexData = reinterpret_cast<const u16*>(
              buffer.data.data() + view.offset + acc.offset);
            for(u32 i = 0; i < acc.count; i++) {
              indices.push_back(indexData[i]);
            }

            primitive.indices = std::make_unique<Buffer>(
              dev, Buffer::Desc{
                     .name       = "Model indices",
                     .usage      = ResourceUsage::IndexBuffer,
                     .size       = indices.size() * sizeof(u16),
                     .memoryType = MemoryType::Main});

            ctx.Write(*primitive.indices, indices);
            primitive.count = indices.size();
          } else {
            primitive.count = vertices.size();
          }
        }

        if(attr.type == VertexAttribute::TexCoord) {
          Arr<u32>
            uv0; // Changed from Float2 to u32 since we'll pack two half floats into one u32

          const auto& acc    = data.accessors[attr.accessorIndex];
          const auto& view   = data.bufferViews[acc.bufferViewIndex];
          const auto& buffer = data.buffers[view.bufferIndex];

          // Read texture coordinates
          const f32* texCoords = reinterpret_cast<const f32*>(
            buffer.data.data() + view.offset + acc.offset);

          for(u32 i = 0; i < acc.count; i++) {
            f32 u = texCoords[i * 2];
            f32 v = texCoords[i * 2 + 1];

            // Pack two f32s into half floats in a single u32
            u16 uHalf  = mt::Float32ToFloat16(u);
            u16 vHalf  = mt::Float32ToFloat16(v);
            u32 packed = (u32)uHalf | ((u32)vHalf << 16);

            uv0.push_back(packed);
          }

          primitive.vbUV0 = std::make_unique<Buffer>(
            dev,
            Buffer::Desc{
              .name  = "Mesh texture coordinates",
              .usage = ResourceUsage::ShaderResource,
              .size  = uv0.size() *
                      sizeof(u32), // Size is now u32 instead of Float2
              .defaultView = ViewType::SRV,
              .memoryType  = MemoryType::Main});

          ctx.Write(*primitive.vbUV0, uv0);
          primitive.param->data.vbUV0Idx = static_cast<i32>(
            primitive.vbUV0->GetView(ViewType::SRV)->binding.index);
        }

        if(attr.type == VertexAttribute::Normal) {
          Arr<u32> normals;

          const auto& acc    = data.accessors[attr.accessorIndex];
          const auto& view   = data.bufferViews[acc.bufferViewIndex];
          const auto& buffer = data.buffers[view.bufferIndex];

          // Read vertex normals
          const f32* normalData = reinterpret_cast<const f32*>(
            buffer.data.data() + view.offset + acc.offset);

          for(u32 i = 0; i < acc.count; i++) {
            f32 x = normalData[i * 3];
            f32 y = normalData[i * 3 + 1];
            f32 z = normalData[i * 3 + 2];

            // Pack normalized components into bytes
            u32 packed = 0;
            packed |= (u32)((x * 0.5f + 0.5f) * 255.0f) << 0;
            packed |= (u32)((y * 0.5f + 0.5f) * 255.0f) << 8;
            packed |= (u32)((z * 0.5f + 0.5f) * 255.0f) << 16;

            normals.push_back(packed);
          }

          primitive.vbNrm = std::make_unique<Buffer>(
            dev, Buffer::Desc{
                   .name        = "Mesh normals",
                   .usage       = ResourceUsage::ShaderResource,
                   .size        = normals.size() * sizeof(u32),
                   .defaultView = ViewType::SRV,
                   .memoryType  = MemoryType::Main});

          ctx.Write(*primitive.vbNrm, normals);
          primitive.param->data.vbNrmIdx = static_cast<i32>(
            primitive.vbNrm->GetView(ViewType::SRV)->binding.index);
        }

        if(attr.type == VertexAttribute::Tangent) {
          Arr<u32> tangents;

          const auto& acc    = data.accessors[attr.accessorIndex];
          const auto& view   = data.bufferViews[acc.bufferViewIndex];
          const auto& buffer = data.buffers[view.bufferIndex];

          // Read vertex tangents
          const f32* tangentData = reinterpret_cast<const f32*>(
            buffer.data.data() + view.offset + acc.offset);

          for(u32 i = 0; i < acc.count; i++) {
            f32 x = tangentData[i * 3];
            f32 y = tangentData[i * 3 + 1];
            f32 z = tangentData[i * 3 + 2];

            // Pack normalized components into bytes
            u32 packed = 0;
            packed |= (u32)((x * 0.5f + 0.5f) * 255.0f) << 0;
            packed |= (u32)((y * 0.5f + 0.5f) * 255.0f) << 8;
            packed |= (u32)((z * 0.5f + 0.5f) * 255.0f) << 16;

            tangents.push_back(packed);
          }

          primitive.vbTan = std::make_unique<Buffer>(
            dev, Buffer::Desc{
                   .name        = "Mesh tangents",
                   .usage       = ResourceUsage::ShaderResource,
                   .size        = tangents.size() * sizeof(u32),
                   .defaultView = ViewType::SRV,
                   .memoryType  = MemoryType::Main});

          ctx.Write(*primitive.vbTan, tangents);
          primitive.param->data.vbTanIdx = static_cast<i32>(
            primitive.vbTan->GetView(ViewType::SRV)->binding.index);
        }
      }

      primitive.param->Update();
      mesh.primitives.push_back(std::move(primitive));
    }

    model.meshes.push_back(std::move(mesh));
  }

  models.push_back(std::move(model));
}

void Scene::loadCubeModel(RenderContext& ctx)
{
  auto& dev = ctx.GetDevice();

  Model model;
  Mesh  mesh;
  Prim  primitive;

  // Create material
  auto material = std::make_unique<UniformBuffer<MaterialParam>>(
    dev, Buffer::Desc{
           .name        = "Cube material",
           .usage       = ResourceUsage::ShaderResource,
           .size        = sizeof(MaterialParam),
           .defaultView = ViewType::SRV,
           .memoryType  = MemoryType::Upload});

  material->data = {
    .colIdx    = -1,
    .nrmIdx    = -1,
    .mrIdx     = -1,
    .metallic  = 0.f,
    .roughness = 0.f,
  };
  material->Update();
  model.materials.push_back(std::move(material));

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

  primitive.vbPos = std::make_unique<Buffer>(
    dev, Buffer::Desc{
           .name        = "Cube vertices",
           .usage       = ResourceUsage::ShaderResource,
           .size        = sizeof(vertices),
           .defaultView = ViewType::SRV,
           .memoryType  = MemoryType::Main});

  ctx.Write(*primitive.vbPos, vertices);

  primitive.param = std::make_unique<UniformBuffer<PrimParam>>(
    dev, Buffer::Desc{
           .name        = "Cube parameters",
           .usage       = ResourceUsage::ShaderResource,
           .size        = sizeof(PrimParam),
           .defaultView = ViewType::SRV,
           .memoryType  = MemoryType::Upload});

  primitive.param->data = {
    .world    = mt::Identity<Float44>(),
    .vbPosIdx = static_cast<i32>(
      primitive.vbPos->GetView(ViewType::SRV)->binding.index),
    .vbNrmIdx    = -1,
    .vbTanIdx    = -1,
    .vbUV0Idx    = -1,
    .materialIdx = static_cast<i32>(model.materials[0]
                                      ->buffer->GetView(ViewType::SRV)
                                      ->binding.index)};
  primitive.param->Update();

  primitive.count = 36; // Cube vertex count
  mesh.primitives.push_back(std::move(primitive));
  model.meshes.push_back(std::move(mesh));
  models.push_back(std::move(model));
}

void Scene::render(
  RenderContext& ctx, const Viewport& viewport, const Rect& renderArea)
{
  auto&      dev        = ctx.GetDevice();
  auto&      sc         = dev.GetSwapchain();
  auto&      backbuffer = sc.AcquireNextImage(true);
  const auto frameIdx   = sc.GetFrameIndex();
  auto&      frame      = frames[frameIdx];

  ctx.Reset();

  // Update camera position
  angle += .01f;
  const auto radius = 2.0f;
  const auto height = 1.0f;
  const auto cameraPos =
    Float3{radius * std::cos(angle), height, radius * std::sin(angle)};
  const auto cameraTgt = Float3{0, 0, 0};
  const auto cameraUp  = Float3{0, 1, 0};
  const auto view      = mt::LookAt(cameraPos, cameraTgt, cameraUp);
  const auto proj =
    mt::PerspectiveFov(std::numbers::pi_v<f32> * .5f, 1.f, 1.f, 1000.f);

  frame.scene->data.view           = view;
  frame.scene->data.viewProjection = mt::Mul(view, proj);
  frame.scene->data.cameraPosition = cameraPos;
  frame.scene->Update();

  auto& cmd = ctx.GetCmd(QueueType::Graphics);

  cmd.Begin();

  cmd.SetViewport(viewport);
  cmd.SetScissor(renderArea);

  // Reset resources state
  for(u32 idx = 0; idx < GBufferCount; ++idx) {
    cmd.AddBarrier(*frame.gbuffers[idx], ResourceState::RenderTarget);
  }
  cmd.AddBarrier(*frame.depthStencil, ResourceState::DepthStencilRW);
  cmd.AddBarrier(backbuffer, ResourceState::RenderTarget);

  cmd.FlushBarriers();

  // Execute render passes
  for(auto& pass: frame.passes) {
    pass.pipeline->Bind(cmd);

    if(pass.id == PassLighting) {
      // Lighting pass - fullscreen triangle
      SArr<u32, 4> push{
        frame.scene->buffer->GetView(ViewType::SRV)->binding.index, 0u,
        0u, 0u};
      cmd.PushConstants(push);

      cmd.BeginRendering(pass.color, &pass.depthStencil);
      cmd.Draw(3u); // Fullscreen triangle
      cmd.EndRendering();
    } else {
      // Geometry passes - draw all models, meshes and primitives
      for(auto& model: models) {
        // Set textures to shader resource state
        if(!model.textures.empty()) {
          for(auto& tex: model.textures) {
            cmd.AddBarrier(*tex, ResourceState::ShaderResourceGraphics);
          }
          cmd.FlushBarriers();
        }

        for(auto& mesh: model.meshes) {
          for(auto& prim: mesh.primitives) {
            SArr<u32, 4> push{
              frame.scene->buffer->GetView(ViewType::SRV)
                ->binding.index,
              prim.param->buffer->GetView(ViewType::SRV)->binding.index,
              0u, 0u};
            cmd.PushConstants(push);

            if(prim.indices) {
              cmd.BindIndexBuffer(*prim.indices, IndexType::U16);
              cmd.BeginRendering(pass.color, &pass.depthStencil);
              cmd.DrawIndexed(prim.count);
            } else {
              cmd.BeginRendering(pass.color, &pass.depthStencil);
              cmd.Draw(prim.count);
            }

            cmd.EndRendering();
          }
        }
      }
    }

    // Handle resource state transitions
    if(pass.id == PassDepth) {
      cmd.AddBarrier(
        *frame.depthStencil, ResourceState::DepthStencilRO);
      cmd.FlushBarriers();
    }

    if(pass.id == PassGBuffer) {
      for(u32 idx = 0; idx < GBufferCount; ++idx) {
        cmd.AddBarrier(
          *frame.gbuffers[idx], ResourceState::ShaderResourceGraphics);
      }
      cmd.FlushBarriers();
    }
  }

  // Transition backbuffer for present
  cmd.AddBarrier(backbuffer, ResourceState::Present);
  cmd.FlushBarriers();

  // Submit work and present
  cmd.End();
  ctx.Submit({&cmd}, {}, {}, SwapchainDep::AcquireRelease);
  sc.Present();
  sc.NextFrame();
}

} // namespace vd
