#pragma once

#include "Scene.hpp"
#include "Window.hpp"

#include <RmlUi/Core.h>
#include <chrono>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

// RmlUi integration for the sample scene inspector. RmlUi's compiled
// geometry and generated textures are retained as GPU resources; Render()
// builds an ordered draw list which is recorded directly into Vuldir's UI
// pass. Assets resolve relative to the sample binary directory.
class Ui
{
public:
  Ui(Window& window, vd::Scene& scene, vd::RenderContext& context);
  ~Ui();

  Ui(const Ui&)            = delete;
  Ui& operator=(const Ui&) = delete;

  // Update RmlUi and collect its draw list before graphics recording begins.
  void update(u32 width, u32 height, u32 frameIndex);

  // Record the collected list inside an active color rendering pass.
  void render(vd::CommandBuffer& cmd, const vd::Rect& renderArea);
  void refreshSceneTree();

  bool isMouseOverUi() const { return m_mouseOverUi; }

  std::function<void(const vd::Str&)> OnLoadModel;
  std::function<void()>               OnLoadCube;
  std::function<void()>               OnClear;

private:
  struct Geometry {
    UPtr<vd::Buffer> vertices;
    UPtr<vd::Buffer> indices;
    u32              indexCount = 0;
  };

  struct Texture {
    UPtr<vd::Image> image;
  };

  struct Draw {
    Geometry*       geometry = nullptr;
    Texture*        texture  = nullptr;
    Rml::Vector2f   translation;
    bool            scissorEnabled = false;
    Rml::Rectanglei scissor;
  };

  struct DrawParam {
    Float2 translation;
    Float2 invViewport;
  };

  struct FrameData {
    UPtr<vd::Buffer> drawParams;
    u64              capacity = 0;
  };

  struct RetiredGeometry {
    u64            retireAfter = 0;
    UPtr<Geometry> resource;
  };

  struct RetiredTexture {
    u64           retireAfter = 0;
    UPtr<Texture> resource;
  };

  class SystemInterface;
  class RenderInterface;
  class EventListener;

  void syncModelFromScene();
  void applyModelToScene();
  void rebuildDataModel();
  void collectRetiredResources();
  void updateDiagnostics();
  void toggleSceneTreeRow(size_t index);
  void openModelPicker();

  Window&            m_window;
  vd::Scene&         m_scene;
  vd::RenderContext& m_renderContext;

  std::unique_ptr<SystemInterface> m_system;
  std::unique_ptr<RenderInterface> m_render;
  std::unique_ptr<EventListener>   m_listener;
  Rml::Context*                    m_context  = nullptr;
  Rml::ElementDocument*            m_document = nullptr;
  Rml::DataModelHandle             m_modelHandle;

  struct SceneTreeRow {
    Rml::String name;
    Rml::String detail;
    int         depth    = 0;
    bool        branch   = false;
    bool        expanded = false;
    bool        visible  = true;
  };

  struct Model {
    bool  panelOpen     = true;
    bool  autoOrbit     = true;
    float orbitYaw      = 0.8f - std::numbers::pi_v<float>;
    float orbitPitch    = 0.25f;
    float orbitDistance = 2.5f;
    float fovYDeg       = 50.f;

    bool  iblEnabled     = true;
    float iblIntensity   = 1.f;
    float iblRotationYaw = 0.f;

    float exposure  = 1.f;
    float gamma     = 2.2f;
    int   debugView = 0;
    int   aaMode    = 2;

    Rml::String modelPath =
      "../../../../sample/assets/DamagedHelmet/glTF/DamagedHelmet.gltf";
    Rml::Vector<SceneTreeRow> sceneTree;
    Rml::String               sceneTreeRml;

    Rml::String fps             = "--";
    Rml::String frameTime       = "--";
    Rml::String fpsLow1         = "--";
    Rml::String fpsLow01        = "--";
    Rml::String processCpu      = "--";
    Rml::String processRam      = "--";
    Rml::String gpuMemory       = "--";
    Rml::String descriptors     = "--";
    int         bufferCount     = 0;
    int         imageCount      = 0;
    int         pipelineCount   = 0;
    int         shaderLoadCount = 0;
    Rml::String frameGraph;
    Rml::String cpuGraph;
  } m_model;

  bool m_mouseOverUi      = false;
  bool m_orbitDrag        = false;
  bool m_fileDialogsReady = false;
  i32  m_mouseX           = 0;
  i32  m_mouseY           = 0;

  u32 m_width            = 0;
  u32 m_height           = 0;
  u32 m_activeFrameIndex = 0;
  u32 m_frameCount       = 1;
  u64 m_frameSerial      = 0;

  bool            m_scissorEnabled = false;
  Rml::Rectanglei m_scissor;

  std::unordered_map<Rml::CompiledGeometryHandle, UPtr<Geometry>>
                                                        m_geometries;
  std::unordered_map<Rml::TextureHandle, UPtr<Texture>> m_textures;
  Arr<Draw>                                             m_draws;
  Arr<FrameData>                                        m_frames;
  Arr<RetiredGeometry> m_retiredGeometry;
  Arr<RetiredTexture>  m_retiredTextures;

  std::chrono::steady_clock::time_point m_startTime;
  std::chrono::steady_clock::time_point m_lastFrameSample =
    std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point m_lastStatsUpdate =
    m_lastFrameSample;
  double   m_previousCpuSeconds = 0.0;
  Arr<f32> m_frameTimesMs;
  Arr<f32> m_cpuHistory;

  friend class RenderInterface;
  friend class EventListener;
};
