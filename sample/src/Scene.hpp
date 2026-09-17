#pragma once

#include "Window.hpp"
#include "vuldir/Vuldir.hpp"

#include <numbers>

Arr<char>    ReadFile(char const* path);
UPtr<Shader> LoadShader(Device& device, const char* path);

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

// G-buffer / lighting debug visualization modes (CPU + shader).
enum class DebugView : i32 {
  Lit       = 0,
  Albedo    = 1,
  Normals   = 2,
  Roughness = 3,
  Metallic  = 4,
};

// Post-tonemap spatial AA (no TAA).
enum class AaMode : i32 {
  Off  = 0,
  Fxaa = 1,
  Smaa = 2,
};

// Scene parameters for rendering. Keep in sync with Common.hlsli
struct SceneParam {
  Float44 view;
  Float44 viewProjection;
  Float3  cameraPosition;
  f32     cameraAspect;

  i32 gbufIdx0;
  i32 gbufIdx1;
  i32 gbufIdx2;
  i32 gbufIdx3;

  Float3 ambientColor;
  f32    ambientIntensity;

  Float3 directionalLightDirection;
  i32    irradianceMapIdx;
  Float3 directionalLightColor;
  i32    prefilteredMapIdx;
  f32    directionalLightIntensity;
  i32    brdfLutIdx;
  f32    prefilteredMipCount;
  f32    iblIntensity;

  i32 iblEnabled;
  f32 iblRotationYaw;
  f32 exposure;
  f32 gamma;

  i32 debugView;
  f32 cameraTanHalfFovY;
  i32 environmentMapIdx;
  i32 sceneColorIdx;

  Float2 aaInvSize;
  Float2 aaSize;

  i32 aaEdgesIdx;
  i32 aaBlendIdx;
  i32 aaAreaIdx;
  i32 aaSearchIdx;
};

// UI-tunable scene state. Applied into SceneParam each frame.
struct SceneControls {
  bool autoOrbit     = true;
  f32  orbitYaw      = 0.8f - std::numbers::pi_v<f32>;
  f32  orbitPitch    = 0.25f;
  f32  orbitDistance = 2.5f;
  f32  fovY          = std::numbers::pi_v<f32> * .5f * (50.f / 90.f);

  Float3 ambientColor     = {0.03f, 0.03f, 0.03f};
  f32    ambientIntensity = 1.f;

  Float3 lightDirection = {1.0f, -1.0f, 0.5f};
  Float3 lightColor     = {1.0f, 1.0f, 1.0f};
  f32    lightIntensity = 1.f;

  bool iblEnabled     = true;
  f32  iblIntensity   = 1.f;
  f32  iblRotationYaw = 0.f;

  f32       exposure  = 1.f;
  f32       gamma     = 2.2f;
  DebugView debugView = DebugView::Lit;
  AaMode    aaMode    = AaMode::Smaa;
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

// Material parameters. Keep in sync with Common.hlsli
struct MaterialParam {
  i32 colIdx;
  i32 nrmIdx;
  i32 mrIdx;
  i32 aoIdx;

  i32 emissiveIdx;
  f32 metallic;
  f32 roughness;
  f32 aoStrength;

  Float4 baseColorFactor;
  Float3 emissiveFactor;
  f32    _pad0;
};

struct Prim {
  u32 count; // Vertex or index count
  u32 vertexCount   = 0u;
  u32 materialIndex = MaxU32;
  Str materialName;

  UPtr<Buffer> indices;
  UPtr<Buffer> vbPos;
  UPtr<Buffer> vbNrm;
  UPtr<Buffer> vbTan;
  UPtr<Buffer> vbUV0;

  UPtr<UniformBuffer<PrimParam>> param;
};

struct Mesh {
  Str       name;
  Arr<Prim> primitives;
};

struct Model {
  Str                                     name;
  Str                                     source;
  Arr<Str>                                materialNames;
  Arr<Mesh>                               meshes;
  Arr<UPtr<Image>>                        textures;
  Arr<UPtr<UniformBuffer<MaterialParam>>> materials;
};

// Render passes (AA is selected at runtime; see pipelines below).
enum Pass { PassDepth, PassGBuffer, PassLighting, PassUi, PassCount };

enum class AaPipeline : u32 {
  Resolve = 0,
  Fxaa,
  SmaaEdge,
  SmaaWeights,
  SmaaBlend,
  Count
};

// GBuffer configuration
static constexpr u32 GBufferCount = 4;

static constexpr SArr<Format, GBufferCount> GBufferFormats = {
  Format::R32G32B32A32_SFLOAT, Format::R32G32B32A32_SFLOAT,
  Format::R32G32B32A32_SFLOAT, Format::R32G32B32A32_SFLOAT};

// Per-frame resources
struct FrameResources {
  UPtr<UniformBuffer<SceneParam>> scene;

  UPtr<Image>      depthStencil;
  Arr<UPtr<Image>> gbuffers;
  UPtr<Image>      sceneColor;
  UPtr<Image>      smaaEdges;
  UPtr<Image>      smaaBlend;

  struct {
    Pass                           id;
    Pipeline*                      pipeline;
    CommandBuffer::Attachment      depthStencil;
    Arr<CommandBuffer::Attachment> color;
  } passes[PassCount];
};

} // namespace vd

class Ui;

namespace vd {

// Owns pipelines, models, IBL and per-frame render resources.
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
  void clearModels();

  // HDR / procedural environment for IBL. Empty path generates a sky fallback.
  void loadEnvironment(RenderContext& ctx, const Str& path = {});

  // SMAA area/search LUTs (once per device).
  void loadSmaaTextures(RenderContext& ctx);

  // Rendering. Returns false if the swapchain must be recreated.
  // Optional UI draws its persistent GPU geometry after lighting.
  bool render(
    RenderContext& ctx, const Viewport& viewport,
    const Rect& renderArea, Ui* ui = nullptr);

  SceneControls&       controls() { return m_controls; }
  const SceneControls& controls() const { return m_controls; }
  const Arr<Model>&    getModels() const { return models; }

  // Prefer controls().
  f32& angle() { return m_controls.orbitYaw; }

protected:
  void bindIblToFrames();

  // Pipeline::GraphicsDesc borrows Shader*, so the Shader must outlive the
  // construction of every pipeline built from it. Declared before pipelines
  // so it is destroyed after them.
  Shader* addShader(Device& dev, const char* path);

  SceneControls       m_controls;
  Arr<UPtr<Shader>>   shaders;
  Arr<UPtr<Pipeline>> pipelines;
  Arr<UPtr<Pipeline>> aaPipelines;
  Arr<Model>          models;
  Arr<FrameResources> frames;

  // IBL maps (equirectangular 2D + BRDF LUT)
  UPtr<Image> irradianceMap;
  UPtr<Image> prefilteredMap;
  UPtr<Image> environmentMap;
  UPtr<Image> brdfLut;
  f32         prefilteredMipCount = 1.f;

  // SMAA LUTs (shared across frames)
  UPtr<Image> smaaArea;
  UPtr<Image> smaaSearch;
};

} // namespace vd
