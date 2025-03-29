#pragma once

#include "Window.hpp"
#include "vuldir/Vuldir.hpp"

// Keep utility functions outside namespace
Arr<char>    ReadFile(char const* path);
UPtr<Shader> LoadShader(Device& device, const char* path);
UPtr<Image>  LoadImage(Device& device, const char* path);

namespace vd {

// Empty metadata struct for UniformBuffer
struct EmptyMeta {};

// Generic uniform buffer with optional metadata.
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

// Scene parameters for rendering
struct SceneParam {
  Float44 view;
  Float44 viewProjection;
  Float3  cameraPosition;
  i32     _pad0;

  i32 gbufIdx0;
  i32 gbufIdx1;
  i32 gbufIdx2;
  i32 _pad1;

  // Ambient light
  Float3 ambientColor;
  i32    _pad2;

  // Directional light parameters
  Float3 directionalLightDirection;
  i32    _pad3;
  Float3 directionalLightColor;
  i32    _pad4;
  float  directionalLightIntensity;
};

// Per-object parameters
struct PrimParam {
  Float44 world;

  i32 vbPosIdx;
  i32 vbNrmIdx;
  i32 vbTanIdx;
  i32 vbUV0Idx;
  i32 materialIdx;
};

// Material parameters
struct MaterialParam {
  i32 colIdx;
  i32 nrmIdx;
  i32 mrIdx;

  f32 metallic;
  f32 roughness;
};

struct Prim {
  u32 count; // Vertex or index count

  UPtr<Buffer> indices;
  UPtr<Buffer> vbPos;
  UPtr<Buffer> vbNrm;
  UPtr<Buffer> vbTan;
  UPtr<Buffer> vbUV0;

  UPtr<UniformBuffer<PrimParam>> param;
};

struct Mesh {
  Arr<Prim> primitives;
};

struct Model {
  Arr<Mesh>                               meshes;
  Arr<UPtr<Image>>                        textures;
  Arr<UPtr<UniformBuffer<MaterialParam>>> materials;
};

// Render passes
enum Pass { PassDepth, PassGBuffer, PassLighting, PassCount };

// GBuffer configuration
static constexpr u32 GBufferCount = 3;

static constexpr SArr<Format, GBufferCount> GBufferFormats = {
  Format::R32G32B32A32_SFLOAT, Format::R32G32B32A32_SFLOAT,
  Format::R32G32B32A32_SFLOAT};

// Per-frame resources
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

// Scene management class that handles all rendering and resource management
class Scene
{
public:
  Scene(Device& dev, u32 frames);
  ~Scene() = default;

  // Resource management
  void createSwapchainDependentResources(Device& dev);
  void prepare(RenderContext& ctx);

  // Model loading
  void loadGltfModel(RenderContext& ctx, const Str& file);
  void loadCubeModel(RenderContext& ctx);

  // Rendering
  void render(
    RenderContext& ctx, const Viewport& viewport,
    const Rect& renderArea);

  // Scene state
  f32 angle = .0f;

protected:
  // Scene resources
  Arr<UPtr<Pipeline>> pipelines;
  Arr<Model>          models; // Changed from meshes to models
  Arr<FrameResources> frames;
};

} // namespace vd
