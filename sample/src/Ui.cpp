#include "Ui.hpp"

#include <RmlUi/Core/Input.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <limits>
#include <nfd.h>
#include <numbers>
#include <numeric>
#include <stdexcept>

#ifdef VD_OS_WINDOWS
  #include <psapi.h>
#elif defined(VD_OS_LINUX)
  #include <unistd.h>
#endif

namespace {

u64 getProcessMemoryBytes()
{
#ifdef VD_OS_WINDOWS
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  if(GetProcessMemoryInfo(
       GetCurrentProcess(),
       reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
       sizeof(counters))) {
    return static_cast<u64>(counters.WorkingSetSize);
  }
#elif defined(VD_OS_LINUX)
  u64 totalPages    = 0u;
  u64 residentPages = 0u;
  std::ifstream("/proc/self/statm") >> totalPages >> residentPages;
  const long pageSize = sysconf(_SC_PAGESIZE);
  if(pageSize > 0) return residentPages * static_cast<u64>(pageSize);
#endif
  return 0u;
}

Rml::String formatMemory(u64 bytes)
{
  return Rml::CreateString(
    "%.1f MiB", static_cast<double>(bytes) / (1024.0 * 1024.0));
}

#ifdef VD_OS_LINUX
Rml::Input::KeyIdentifier mapKeySym(u32 keysym)
{
  using KI = Rml::Input::KeyIdentifier;
  switch(keysym) {
    case 0xff08:
      return KI::KI_BACK;
    case 0xff09:
      return KI::KI_TAB;
    case 0xff0d:
      return KI::KI_RETURN;
    case 0xff1b:
      return KI::KI_ESCAPE;
    case 0xffff:
      return KI::KI_DELETE;
    case 0xff51:
      return KI::KI_LEFT;
    case 0xff52:
      return KI::KI_UP;
    case 0xff53:
      return KI::KI_RIGHT;
    case 0xff54:
      return KI::KI_DOWN;
    case 0xff50:
      return KI::KI_HOME;
    case 0xff57:
      return KI::KI_END;
    case 0xff55:
      return KI::KI_PRIOR;
    case 0xff56:
      return KI::KI_NEXT;
    default:
      break;
  }
  if(keysym >= 'a' && keysym <= 'z') {
    return static_cast<KI>(static_cast<int>(KI::KI_A) + (keysym - 'a'));
  }
  if(keysym >= 'A' && keysym <= 'Z') {
    return static_cast<KI>(static_cast<int>(KI::KI_A) + (keysym - 'A'));
  }
  if(keysym >= '0' && keysym <= '9') {
    return static_cast<KI>(static_cast<int>(KI::KI_0) + (keysym - '0'));
  }
  return KI::KI_UNKNOWN;
}
#endif

#ifdef VD_OS_WINDOWS
Rml::Input::KeyIdentifier mapWinVk(u32 vk)
{
  using KI = Rml::Input::KeyIdentifier;
  if(vk >= 'A' && vk <= 'Z') {
    return static_cast<KI>(static_cast<int>(KI::KI_A) + (vk - 'A'));
  }
  if(vk >= '0' && vk <= '9') {
    return static_cast<KI>(static_cast<int>(KI::KI_0) + (vk - '0'));
  }
  switch(vk) {
    case 0x08:
      return KI::KI_BACK;
    case 0x09:
      return KI::KI_TAB;
    case 0x0D:
      return KI::KI_RETURN;
    case 0x1B:
      return KI::KI_ESCAPE;
    case 0x2E:
      return KI::KI_DELETE;
    case 0x25:
      return KI::KI_LEFT;
    case 0x26:
      return KI::KI_UP;
    case 0x27:
      return KI::KI_RIGHT;
    case 0x28:
      return KI::KI_DOWN;
    case 0x24:
      return KI::KI_HOME;
    case 0x23:
      return KI::KI_END;
    case 0x21:
      return KI::KI_PRIOR;
    case 0x22:
      return KI::KI_NEXT;
    default:
      return KI::KI_UNKNOWN;
  }
}
#endif

} // namespace

class Ui::SystemInterface : public Rml::SystemInterface
{
public:
  explicit SystemInterface(Ui& ui): m_ui(ui) {}

  double GetElapsedTime() override
  {
    using namespace std::chrono;
    return duration<double>(steady_clock::now() - m_ui.m_startTime)
      .count();
  }

  bool
  LogMessage(Rml::Log::Type type, const Rml::String& message) override
  {
    const char* level = "I";
    switch(type) {
      case Rml::Log::LT_ERROR:
        level = "E";
        break;
      case Rml::Log::LT_WARNING:
        level = "W";
        break;
      case Rml::Log::LT_ASSERT:
        level = "A";
        break;
      default:
        break;
    }
    std::fprintf(stderr, "[rmlui:%s] %s\n", level, message.c_str());
    return true;
  }

private:
  Ui& m_ui;
};

class Ui::RenderInterface : public Rml::RenderInterface
{
public:
  explicit RenderInterface(Ui& ui): m_ui(ui) {}

  Rml::CompiledGeometryHandle CompileGeometry(
    Rml::Span<const Rml::Vertex> vertices,
    Rml::Span<const int>         indices) override
  {
    static_assert(sizeof(Rml::Vertex) == 20u);
    static_assert(offsetof(Rml::Vertex, colour) == 8u);
    static_assert(offsetof(Rml::Vertex, tex_coord) == 12u);
    static_assert(sizeof(int) == sizeof(u32));

    auto  geometry = std::make_unique<Geometry>();
    auto& dev      = m_ui.m_renderContext.GetDevice();

    geometry->vertices = std::make_unique<Buffer>(
      dev, Buffer::Desc{
             .name        = "RmlUi vertices",
             .usage       = ResourceUsage::ShaderResource,
             .size        = vertices.size() * sizeof(Rml::Vertex),
             .defaultView = ViewType::SRV,
             .memoryType  = MemoryType::Upload});
    geometry->vertices->Write(
      Span<u8 const>{
        reinterpret_cast<const u8*>(vertices.data()),
        vertices.size() * sizeof(Rml::Vertex)});
    geometry->vertices->SetState(ResourceState::ShaderResourceGraphics);

    geometry->indices = std::make_unique<Buffer>(
      dev, Buffer::Desc{
             .name       = "RmlUi indices",
             .usage      = ResourceUsage::IndexBuffer,
             .size       = indices.size() * sizeof(int),
             .memoryType = MemoryType::Upload});
    geometry->indices->Write(
      Span<u8 const>{
        reinterpret_cast<const u8*>(indices.data()),
        indices.size() * sizeof(int)});
    geometry->indices->SetState(ResourceState::IndexBuffer);
    geometry->indexCount = static_cast<u32>(indices.size());

    const auto handle =
      reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.get());
    m_ui.m_geometries.emplace(handle, std::move(geometry));
    return handle;
  }

  void RenderGeometry(
    Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
    Rml::TextureHandle texture) override
  {
    const auto geometryIt = m_ui.m_geometries.find(geometry);
    if(geometryIt == m_ui.m_geometries.end()) return;

    Texture* gpuTexture = nullptr;
    if(texture) {
      const auto textureIt = m_ui.m_textures.find(texture);
      if(textureIt != m_ui.m_textures.end()) {
        gpuTexture = textureIt->second.get();
      }
    }

    m_ui.m_draws.push_back({
      .geometry       = geometryIt->second.get(),
      .texture        = gpuTexture,
      .translation    = translation,
      .scissorEnabled = m_ui.m_scissorEnabled,
      .scissor        = m_ui.m_scissor,
    });
  }

  void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override
  {
    const auto it = m_ui.m_geometries.find(handle);
    if(it == m_ui.m_geometries.end()) return;
    m_ui.m_retiredGeometry.push_back({
      .retireAfter = m_ui.m_frameSerial + m_ui.m_frameCount,
      .resource    = std::move(it->second),
    });
    m_ui.m_geometries.erase(it);
  }

  Rml::TextureHandle LoadTexture(
    Rml::Vector2i&     textureDimensions,
    const Rml::String& source) override
  {
    (void)textureDimensions;
    (void)source;
    return 0;
  }

  Rml::TextureHandle GenerateTexture(
    Rml::Span<const Rml::byte> source,
    Rml::Vector2i              dimensions) override
  {
    if(dimensions.x <= 0 || dimensions.y <= 0 || source.empty())
      return 0;

    auto  texture  = std::make_unique<Texture>();
    auto& dev      = m_ui.m_renderContext.GetDevice();
    texture->image = std::make_unique<Image>(
      dev, Image::Desc{
             .name      = "RmlUi generated texture",
             .usage     = ResourceUsage::ShaderResource,
             .format    = Format::R8G8B8A8_UNORM,
             .dimension = Dimension::e2D,
             .extent =
               {static_cast<u32>(dimensions.x),
                static_cast<u32>(dimensions.y), 1u},
             .defaultView = ViewType::SRV,
             .mips        = 1u});
    if(!m_ui.m_renderContext.Write(
         *texture->image, Span<u8 const>{
                            reinterpret_cast<const u8*>(source.data()),
                            source.size()})) {
      return 0;
    }

    const auto handle =
      reinterpret_cast<Rml::TextureHandle>(texture.get());
    m_ui.m_textures.emplace(handle, std::move(texture));
    return handle;
  }

  void ReleaseTexture(Rml::TextureHandle handle) override
  {
    const auto it = m_ui.m_textures.find(handle);
    if(it == m_ui.m_textures.end()) return;
    m_ui.m_retiredTextures.push_back({
      .retireAfter = m_ui.m_frameSerial + m_ui.m_frameCount,
      .resource    = std::move(it->second),
    });
    m_ui.m_textures.erase(it);
  }

  void EnableScissorRegion(bool enable) override
  {
    m_ui.m_scissorEnabled = enable;
  }

  void SetScissorRegion(Rml::Rectanglei region) override
  {
    m_ui.m_scissor = region;
  }

private:
  Ui& m_ui;
};

class Ui::EventListener : public Rml::EventListener
{
public:
  explicit EventListener(Ui& ui): m_ui(ui) {}

  void ProcessEvent(Rml::Event& event) override
  {
    if(event.GetType() != "click") return;
    auto* element = event.GetTargetElement();
    if(!element) return;

    const auto id = element->GetId();
    if(id == "btn_clear") {
      if(m_ui.OnClear) m_ui.OnClear();
      else
        m_ui.m_scene.clearModels();
    } else if(id == "btn_reload") {
      m_ui.openModelPicker();
    } else if(id == "btn_cube") {
      if(m_ui.OnLoadCube) m_ui.OnLoadCube();
    }
  }

private:
  Ui& m_ui;
};

Ui::Ui(Window& window, vd::Scene& scene, vd::RenderContext& context):
  m_window(window),
  m_scene(scene),
  m_renderContext(context),
  m_frameCount(context.GetMaxFramesInFlight()),
  m_startTime(std::chrono::steady_clock::now())
{
  syncModelFromScene();
  m_frames.resize(m_frameCount);

  m_fileDialogsReady = NFD_Init() == NFD_OKAY;
  if(!m_fileDialogsReady) {
    std::fprintf(
      stderr, "[vuldir] native file dialog initialization failed: %s\n",
      NFD_GetError());
  }

  m_system = std::make_unique<SystemInterface>(*this);
  m_render = std::make_unique<RenderInterface>(*this);
  Rml::SetSystemInterface(m_system.get());
  Rml::SetRenderInterface(m_render.get());

  if(!Rml::Initialise()) {
    throw std::runtime_error("Rml::Initialise failed");
  }

  if(!Rml::LoadFontFace("ui/LatoLatin-Regular.ttf")) {
    std::fprintf(
      stderr,
      "[vuldir] warning: failed to load ui/LatoLatin-Regular.ttf "
      "(cwd should be the sample binary dir)\n");
  }

  m_context = Rml::CreateContext(
    "main", Rml::Vector2i{
              static_cast<int>(window.GetContentWidth()),
              static_cast<int>(window.GetContentHeight())});
  if(!m_context) throw std::runtime_error("Rml::CreateContext failed");

  m_context->SetDensityIndependentPixelRatio(1.5f);
  rebuildDataModel();

  m_document = m_context->LoadDocument("ui/debug.rml");
  if(!m_document) {
    throw std::runtime_error(
      "Failed to load ui/debug.rml (expected next to the sample "
      "binary)");
  }
  m_document->Show();

  m_listener = std::make_unique<EventListener>(*this);
  m_document->AddEventListener(
    Rml::EventId::Click, m_listener.get(), false);

  m_window.OnMouseMove = [this](i32 x, i32 y, i32 dx, i32 dy) {
    m_mouseX = x;
    m_mouseY = y;

    if(m_orbitDrag) {
      auto& controls     = m_scene.controls();
      controls.autoOrbit = false;
      controls.orbitYaw -= static_cast<f32>(dx) * 0.005f;
      controls.orbitPitch = std::clamp(
        controls.orbitPitch + static_cast<f32>(dy) * 0.005f, -1.55f,
        1.55f);
      m_model.autoOrbit  = false;
      m_model.orbitYaw   = controls.orbitYaw;
      m_model.orbitPitch = controls.orbitPitch;
      if(m_modelHandle) {
        m_modelHandle.DirtyVariable("orbitYaw");
        m_modelHandle.DirtyVariable("orbitPitch");
        m_modelHandle.DirtyVariable("autoOrbit");
      }
      return true;
    }

    m_context->ProcessMouseMove(x, y, 0);
    m_mouseOverUi = m_context->IsMouseInteracting();
    return m_mouseOverUi;
  };

  m_window.OnMouseButton = [this](i32 x, i32 y, i32 button, bool down) {
    m_mouseX = x;
    m_mouseY = y;
    m_context->ProcessMouseMove(x, y, 0);
    m_mouseOverUi = m_context->IsMouseInteracting();

    if(button == 0) {
      if(down && !m_mouseOverUi) {
        m_orbitDrag = true;
        return true;
      }
      if(!down && m_orbitDrag) {
        m_orbitDrag = false;
        return true;
      }
    }

    if(down) m_context->ProcessMouseButtonDown(button, 0);
    else
      m_context->ProcessMouseButtonUp(button, 0);
    m_mouseOverUi = m_context->IsMouseInteracting();
    return m_mouseOverUi;
  };

  m_window.OnMouseWheel = [this](i32 x, i32 y, f32 delta) {
    m_context->ProcessMouseMove(x, y, 0);
    m_mouseOverUi = m_context->IsMouseInteracting();
    if(m_mouseOverUi) {
      m_context->ProcessMouseWheel(Rml::Vector2f{0.f, -delta}, 0);
      return true;
    }

    auto&     controls = m_scene.controls();
    const f32 factor   = std::pow(0.9f, delta);
    controls.orbitDistance =
      std::clamp(controls.orbitDistance * factor, 0.2f, 20.f);
    m_model.orbitDistance = controls.orbitDistance;
    if(m_modelHandle) m_modelHandle.DirtyVariable("orbitDistance");
    return true;
  };

  m_window.OnKey = [this](u32 key, bool down) {
#ifdef VD_OS_WINDOWS
    const auto id = mapWinVk(key);
#else
    const auto id = mapKeySym(key);
#endif
    if(id == Rml::Input::KI_UNKNOWN) return m_mouseOverUi;
    if(down) m_context->ProcessKeyDown(id, 0);
    else
      m_context->ProcessKeyUp(id, 0);
    return m_mouseOverUi;
  };

  m_window.OnTextInput = [this](u32 codepoint) {
    m_context->ProcessTextInput(static_cast<Rml::Character>(codepoint));
    return m_mouseOverUi;
  };
}

Ui::~Ui()
{
  if(m_document) {
    m_document->Close();
    m_document = nullptr;
  }
  if(m_context) {
    Rml::RemoveContext(m_context->GetName());
    m_context = nullptr;
  }
  Rml::Shutdown();
  m_render.reset();
  m_system.reset();
  if(m_fileDialogsReady) NFD_Quit();
}

void Ui::openModelPicker()
{
  if(!m_fileDialogsReady) {
    std::fprintf(
      stderr, "[vuldir] native file dialog is unavailable: %s\n",
      NFD_GetError());
    return;
  }

  const nfdu8filteritem_t filters[] = {
    {"glTF scenes", "gltf,glb"},
  };
  nfdopendialogu8args_t args{};
  args.filterList  = filters;
  args.filterCount = 1u;
#ifdef VD_OS_WINDOWS
  args.parentWindow.type   = NFD_WINDOW_HANDLE_TYPE_WINDOWS;
  args.parentWindow.handle = m_window.GetHandle().hWnd;
#elif defined(VD_OS_LINUX) && defined(VD_WINDOW_XCB)
  args.parentWindow.type = NFD_WINDOW_HANDLE_TYPE_X11;
  // NFD stores the X11 Window id in a void* slot.
  // NOLINTNEXTLINE(performance-no-int-to-ptr)
  args.parentWindow.handle = reinterpret_cast<void*>(
    static_cast<uintptr_t>(m_window.GetHandle().window));
#endif
  // Wayland: NFD uses the desktop portal; no native parent handle.

  nfdu8char_t*      path   = nullptr;
  const nfdresult_t result = NFD_OpenDialogU8_With(&path, &args);
  if(result == NFD_OKAY) {
    m_model.modelPath = path;
    if(m_modelHandle) m_modelHandle.DirtyVariable("modelPath");
    if(OnLoadModel) OnLoadModel(Str(path));
    NFD_FreePathU8(path);
  } else if(result == NFD_ERROR) {
    std::fprintf(
      stderr, "[vuldir] native file dialog failed: %s\n",
      NFD_GetError());
  }
}

void Ui::syncModelFromScene()
{
  const auto& controls  = m_scene.controls();
  m_model.autoOrbit     = controls.autoOrbit;
  m_model.orbitYaw      = controls.orbitYaw;
  m_model.orbitPitch    = controls.orbitPitch;
  m_model.orbitDistance = controls.orbitDistance;
  m_model.fovYDeg = controls.fovY * (180.f / std::numbers::pi_v<f32>);
  m_model.iblEnabled     = controls.iblEnabled;
  m_model.iblIntensity   = controls.iblIntensity;
  m_model.iblRotationYaw = controls.iblRotationYaw;
  m_model.exposure       = controls.exposure;
  m_model.gamma          = controls.gamma;
  m_model.debugView      = static_cast<int>(controls.debugView);
  m_model.aaMode         = static_cast<int>(controls.aaMode);
}

void Ui::applyModelToScene()
{
  auto& controls         = m_scene.controls();
  controls.autoOrbit     = m_model.autoOrbit;
  controls.orbitYaw      = m_model.orbitYaw;
  controls.orbitPitch    = m_model.orbitPitch;
  controls.orbitDistance = std::max(0.1f, m_model.orbitDistance);
  controls.fovY =
    std::max(0.1f, m_model.fovYDeg) * (std::numbers::pi_v<f32> / 180.f);
  controls.iblEnabled     = m_model.iblEnabled;
  controls.iblIntensity   = m_model.iblIntensity;
  controls.iblRotationYaw = m_model.iblRotationYaw;
  controls.exposure       = m_model.exposure;
  controls.gamma          = m_model.gamma;
  controls.debugView =
    static_cast<vd::DebugView>(std::clamp(m_model.debugView, 0, 4));
  controls.aaMode =
    static_cast<vd::AaMode>(std::clamp(m_model.aaMode, 0, 2));
}

void Ui::rebuildDataModel()
{
  refreshSceneTree();

  Rml::DataModelConstructor constructor =
    m_context->CreateDataModel("ctrl");
  if(!constructor) return;

  if(auto row = constructor.RegisterStruct<SceneTreeRow>()) {
    row.RegisterMember("name", &SceneTreeRow::name);
    row.RegisterMember("detail", &SceneTreeRow::detail);
    row.RegisterMember("depth", &SceneTreeRow::depth);
    row.RegisterMember("branch", &SceneTreeRow::branch);
    row.RegisterMember("expanded", &SceneTreeRow::expanded);
    row.RegisterMember("visible", &SceneTreeRow::visible);
  }
  constructor.RegisterArray<Rml::Vector<SceneTreeRow>>();
  constructor.Bind("panelOpen", &m_model.panelOpen);
  constructor.Bind("autoOrbit", &m_model.autoOrbit);
  constructor.Bind("orbitYaw", &m_model.orbitYaw);
  constructor.Bind("orbitPitch", &m_model.orbitPitch);
  constructor.Bind("orbitDistance", &m_model.orbitDistance);
  constructor.Bind("fovYDeg", &m_model.fovYDeg);
  constructor.Bind("iblEnabled", &m_model.iblEnabled);
  constructor.Bind("iblIntensity", &m_model.iblIntensity);
  constructor.Bind("iblRotationYaw", &m_model.iblRotationYaw);
  constructor.Bind("exposure", &m_model.exposure);
  constructor.Bind("gamma", &m_model.gamma);
  constructor.Bind("debugView", &m_model.debugView);
  constructor.Bind("aaMode", &m_model.aaMode);
  constructor.Bind("modelPath", &m_model.modelPath);
  constructor.Bind("sceneTree", &m_model.sceneTree);
  constructor.Bind("sceneTreeRml", &m_model.sceneTreeRml);
  constructor.BindEventCallback(
    "toggle_scene_row", [this](
                          Rml::DataModelHandle, Rml::Event&,
                          const Rml::VariantList& parameters) {
      if(!parameters.empty())
        toggleSceneTreeRow(
          static_cast<size_t>(parameters[0].Get<int>()));
    });
  constructor.BindEventCallback(
    "open_model",
    [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
      openModelPicker();
    });
  constructor.BindEventCallback(
    "load_cube",
    [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
      if(OnLoadCube) OnLoadCube();
    });
  constructor.BindEventCallback(
    "load_helmet",
    [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
      if(OnLoadModel)
        OnLoadModel(
          "../../../../sample/assets/DamagedHelmet/glTF/"
          "DamagedHelmet.gltf");
    });
  constructor.BindEventCallback(
    "clear_scene",
    [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
      if(OnClear) OnClear();
      else
        m_scene.clearModels();
    });
  constructor.Bind("fps", &m_model.fps);
  constructor.Bind("frameTime", &m_model.frameTime);
  constructor.Bind("fpsLow1", &m_model.fpsLow1);
  constructor.Bind("fpsLow01", &m_model.fpsLow01);
  constructor.Bind("processCpu", &m_model.processCpu);
  constructor.Bind("processRam", &m_model.processRam);
  constructor.Bind("gpuMemory", &m_model.gpuMemory);
  constructor.Bind("descriptors", &m_model.descriptors);
  constructor.Bind("bufferCount", &m_model.bufferCount);
  constructor.Bind("imageCount", &m_model.imageCount);
  constructor.Bind("pipelineCount", &m_model.pipelineCount);
  constructor.Bind("shaderLoadCount", &m_model.shaderLoadCount);
  constructor.Bind("frameGraph", &m_model.frameGraph);
  constructor.Bind("cpuGraph", &m_model.cpuGraph);
  m_modelHandle = constructor.GetModelHandle();
}

void Ui::refreshSceneTree()
{
  m_model.sceneTree.clear();

  const auto& models = m_scene.getModels();
  for(size_t modelIndex = 0u; modelIndex < models.size();
      ++modelIndex) {
    const auto& model = models[modelIndex];
    m_model.sceneTree.push_back(
      {.name   = model.name.empty()
                   ? Rml::CreateString("Model %zu", modelIndex)
                   : Rml::String(model.name.c_str()),
       .detail = Rml::CreateString(
         "%zu meshes | %zu materials | %zu textures",
         model.meshes.size(), model.materials.size(),
         model.textures.size()),
       .depth    = 0,
       .branch   = !model.meshes.empty(),
       .expanded = true,
       .visible  = true});

    for(size_t meshIndex = 0u; meshIndex < model.meshes.size();
        ++meshIndex) {
      const auto& mesh = model.meshes[meshIndex];
      m_model.sceneTree.push_back(
        {.name   = mesh.name.empty()
                     ? Rml::CreateString("Mesh %zu", meshIndex)
                     : Rml::String(mesh.name.c_str()),
         .detail = Rml::CreateString(
           "%zu primitive%s", mesh.primitives.size(),
           mesh.primitives.size() == 1u ? "" : "s"),
         .depth    = 1,
         .branch   = !mesh.primitives.empty(),
         .expanded = false,
         .visible  = true});

      for(size_t primIndex = 0u; primIndex < mesh.primitives.size();
          ++primIndex) {
        const auto& primitive  = mesh.primitives[primIndex];
        Rml::String attributes = "P";
        if(primitive.vbNrm) attributes += " N";
        if(primitive.vbTan) attributes += " T";
        if(primitive.vbUV0) attributes += " UV";
        m_model.sceneTree.push_back(
          {.name   = Rml::CreateString("Primitive %zu", primIndex),
           .detail = Rml::CreateString(
             "%u tris | %u vertices | %s | %s | %s",
             primitive.count / 3u, primitive.vertexCount,
             primitive.indices ? "indexed" : "non-indexed",
             primitive.materialName.c_str(), attributes.c_str()),
           .depth    = 2,
           .branch   = false,
           .expanded = false,
           .visible  = false});
      }
    }
  }

  const auto encodeRml = [](const Rml::String& value) {
    Rml::String encoded;
    encoded.reserve(value.size());
    for(const char character: value) {
      switch(character) {
        case '&':
          encoded += "&amp;";
          break;
        case '<':
          encoded += "&lt;";
          break;
        case '>':
          encoded += "&gt;";
          break;
        case '\"':
          encoded += "&quot;";
          break;
        case '\'':
          encoded += "&#39;";
          break;
        default:
          encoded += character;
          break;
      }
    }
    return encoded;
  };

  Rml::String treeRml;
  if(m_model.sceneTree.empty()) {
    treeRml = "<div class='tree-empty'>No scene loaded</div>";
  } else {
    for(const auto& row: m_model.sceneTree) {
      const char* rowClass    = row.depth == 0 ? " tree-model" : "";
      const char* markerClass = row.depth == 0   ? "model"
                                : row.depth == 1 ? "mesh"
                                                 : "primitive";
      const char* indent =
        row.depth == 1   ? "<span class='tree-indent depth-one'></span>"
        : row.depth >= 2 ? "<span class='tree-indent depth-two'></span>"
                         : "";
      const auto name   = encodeRml(row.name);
      const auto detail = encodeRml(row.detail);
      treeRml += Rml::CreateString(
        "<div class='tree-row%s'>%s<span class='tree-marker %s'></span>"
        "<div class='tree-copy'><b>%s</b><small>%s</small></div></div>",
        rowClass, indent, markerClass, name.c_str(), detail.c_str());
    }
  }
  m_model.sceneTreeRml = std::move(treeRml);

  if(m_modelHandle) {
    m_modelHandle.DirtyVariable("sceneTree");
    m_modelHandle.DirtyVariable("sceneTreeRml");
  }
}

void Ui::toggleSceneTreeRow(size_t index)
{
  if(index >= m_model.sceneTree.size()) return;
  auto& row = m_model.sceneTree[index];
  if(!row.branch) return;

  row.expanded = !row.expanded;
  for(size_t i = index + 1u; i < m_model.sceneTree.size() &&
                             m_model.sceneTree[i].depth > row.depth;
      ++i) {
    auto& descendant = m_model.sceneTree[i];
    descendant.visible =
      row.expanded && descendant.depth == row.depth + 1;
    if(!row.expanded) descendant.expanded = false;
  }
  m_modelHandle.DirtyVariable("sceneTree");
}

void Ui::updateDiagnostics()
{
  using namespace std::chrono;

  const auto now = steady_clock::now();
  const f32  frameMs =
    duration<f32, std::milli>(now - m_lastFrameSample).count();
  m_lastFrameSample = now;
  if(frameMs > 0.f && frameMs < 1000.f) {
    m_frameTimesMs.push_back(frameMs);
    if(m_frameTimesMs.size() > 2048u)
      m_frameTimesMs.erase(m_frameTimesMs.begin());
  }

  const f32 statsDelta = duration<f32>(now - m_lastStatsUpdate).count();
  if(statsDelta < 0.25f || m_frameTimesMs.empty()) return;

  const size_t averageCount =
    std::min<size_t>(120u, m_frameTimesMs.size());
  const auto averageBegin = m_frameTimesMs.end() - averageCount;
  const f32  averageMs =
    std::accumulate(averageBegin, m_frameTimesMs.end(), 0.f) /
    static_cast<f32>(averageCount);

  Arr<f32> sortedTimes = m_frameTimesMs;
  std::sort(sortedTimes.begin(), sortedTimes.end());
  const auto lowFps = [&](f32 percentile) {
    const size_t index = std::min(
      sortedTimes.size() - 1u,
      static_cast<size_t>(percentile * sortedTimes.size()));
    return 1000.f / std::max(sortedTimes[index], 0.001f);
  };

  m_model.fps       = Rml::CreateString("%.0f", 1000.f / averageMs);
  m_model.frameTime = Rml::CreateString("%.2f ms", averageMs);
  m_model.fpsLow1   = Rml::CreateString("%.0f", lowFps(0.99f));
  m_model.fpsLow01  = Rml::CreateString("%.0f", lowFps(0.999f));

  const double cpuSeconds =
    static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
  f32 cpuPercent = 0.f;
  if(m_previousCpuSeconds > 0.0) {
    cpuPercent = static_cast<f32>(
      100.0 * (cpuSeconds - m_previousCpuSeconds) / statsDelta);
  }
  m_previousCpuSeconds = cpuSeconds;
  m_model.processCpu   = Rml::CreateString("%.1f%%", cpuPercent);
  m_model.processRam   = formatMemory(getProcessMemoryBytes());

  m_cpuHistory.push_back(cpuPercent);
  if(m_cpuHistory.size() > 48u)
    m_cpuHistory.erase(m_cpuHistory.begin());
  m_model.cpuGraph.clear();
  for(const f32 sample: m_cpuHistory) {
    const f32 height = 2.f + 0.3f * std::clamp(sample, 0.f, 100.f);
    m_model.cpuGraph += Rml::CreateString(
      "<span class='graph-bar' style='height: %.1fdp'></span>", height);
  }

  constexpr size_t GraphBars = 48u;
  const size_t     sourceCount =
    std::min<size_t>(480u, m_frameTimesMs.size());
  const size_t sourceStart = m_frameTimesMs.size() - sourceCount;
  const size_t barCount    = std::min(GraphBars, sourceCount);
  m_model.frameGraph.clear();
  for(size_t bar = 0u; bar < barCount; ++bar) {
    const size_t begin = sourceStart + bar * sourceCount / barCount;
    const size_t end =
      sourceStart + (bar + 1u) * sourceCount / barCount;
    f32 bucket = 0.f;
    for(size_t i = begin; i < end; ++i) bucket += m_frameTimesMs[i];
    bucket /= static_cast<f32>(std::max<size_t>(1u, end - begin));
    const f32 height =
      2.f + 30.f * std::clamp(bucket / 33.333f, 0.f, 1.f);
    m_model.frameGraph += Rml::CreateString(
      "<span class='graph-bar' style='height: %.1fdp'></span>", height);
  }

  const auto renderer = m_renderContext.GetDevice().GetStats();
  m_model.gpuMemory   = Rml::CreateString(
    "%.1f / %.0f MiB",
    static_cast<double>(renderer.gpuMemoryUsed) / (1024.0 * 1024.0),
    static_cast<double>(renderer.gpuMemoryCommitted) /
      (1024.0 * 1024.0));
  m_model.descriptors =
    Rml::CreateString("%u active", renderer.descriptorsUsed);
  m_model.bufferCount     = static_cast<int>(renderer.buffers);
  m_model.imageCount      = static_cast<int>(renderer.images);
  m_model.pipelineCount   = static_cast<int>(renderer.pipelines);
  m_model.shaderLoadCount = static_cast<int>(renderer.shadersLoaded);

  constexpr const char* DirtyVariables[] = {
    "fps",         "frameTime",  "fpsLow1",       "fpsLow01",
    "processCpu",  "processRam", "gpuMemory",     "descriptors",
    "bufferCount", "imageCount", "pipelineCount", "shaderLoadCount",
    "frameGraph",  "cpuGraph"};
  for(const char* variable: DirtyVariables)
    m_modelHandle.DirtyVariable(variable);

  m_lastStatsUpdate = now;
}

void Ui::collectRetiredResources()
{
  std::erase_if(m_retiredGeometry, [this](const RetiredGeometry& item) {
    return item.retireAfter <= m_frameSerial;
  });
  std::erase_if(m_retiredTextures, [this](const RetiredTexture& item) {
    return item.retireAfter <= m_frameSerial;
  });
}

void Ui::update(u32 width, u32 height, u32 frameIndex)
{
  updateDiagnostics();
  ++m_frameSerial;
  collectRetiredResources();

  if(width == 0 || height == 0) return;
  if(width != m_width || height != m_height) {
    m_width  = width;
    m_height = height;
    m_context->SetDimensions(
      Rml::Vector2i{static_cast<int>(width), static_cast<int>(height)});
  }

  // While auto-orbit is active the scene owns yaw. Not marked dirty, to
  // avoid recompiling geometry every frame.
  if(m_model.autoOrbit) m_model.orbitYaw = m_scene.controls().orbitYaw;
  applyModelToScene();

  m_draws.clear();
  m_scissorEnabled = false;
  m_context->Update();
  m_context->Render();

  m_activeFrameIndex = frameIndex % m_frameCount;
  if(m_draws.empty()) return;

  Arr<DrawParam> params;
  params.reserve(m_draws.size());
  const Float2 invViewport{
    1.f / static_cast<f32>(m_width), 1.f / static_cast<f32>(m_height)};
  for(const Draw& draw: m_draws) {
    params.push_back({
      .translation = {draw.translation.x, draw.translation.y},
      .invViewport = invViewport,
    });
  }

  FrameData& frame    = m_frames[m_activeFrameIndex];
  const u64  required = params.size() * sizeof(DrawParam);
  if(!frame.drawParams || frame.capacity < required) {
    frame.capacity = 256u;
    while(frame.capacity < required) frame.capacity *= 2u;
    frame.drawParams = std::make_unique<Buffer>(
      m_renderContext.GetDevice(),
      Buffer::Desc{
        .name        = "RmlUi draw parameters",
        .usage       = ResourceUsage::ShaderResource,
        .size        = frame.capacity,
        .defaultView = ViewType::SRV,
        .memoryType  = MemoryType::Upload,
      });
    frame.drawParams->SetState(ResourceState::ShaderResourceGraphics);
  }
  frame.drawParams->Write(params);
}

void Ui::render(vd::CommandBuffer& cmd, const vd::Rect& renderArea)
{
  if(m_draws.empty()) return;
  const FrameData& frame = m_frames[m_activeFrameIndex];
  if(!frame.drawParams) return;

  const i32 areaLeft = renderArea.offset[0];
  const i32 areaTop  = renderArea.offset[1];
  const i32 areaRight =
    areaLeft + static_cast<i32>(renderArea.extent[0]);
  const i32 areaBottom =
    areaTop + static_cast<i32>(renderArea.extent[1]);
  const u32 paramsIdx =
    frame.drawParams->GetView(ViewType::SRV)->binding.index;

  for(u32 drawIndex = 0; drawIndex < m_draws.size(); ++drawIndex) {
    const Draw& draw = m_draws[drawIndex];
    if(!draw.geometry) continue;

    Rect scissor = renderArea;
    if(draw.scissorEnabled) {
      const i32 left   = std::max(areaLeft, draw.scissor.Left());
      const i32 top    = std::max(areaTop, draw.scissor.Top());
      const i32 right  = std::min(areaRight, draw.scissor.Right());
      const i32 bottom = std::min(areaBottom, draw.scissor.Bottom());
      if(right <= left || bottom <= top) continue;
      scissor = {
        .offset = {left, top},
        .extent =
          {static_cast<u32>(right - left),
           static_cast<u32>(bottom - top)},
      };
    }
    cmd.SetScissor(scissor);

    const u32 textureIdx =
      draw.texture && draw.texture->image
        ? draw.texture->image->GetView(ViewType::SRV)->binding.index
        : std::numeric_limits<u32>::max();
    const u32 vertexIdx =
      draw.geometry->vertices->GetView(ViewType::SRV)->binding.index;
    const SArr<u32, 4> push{
      vertexIdx, textureIdx, paramsIdx, drawIndex};
    cmd.PushConstants(push);
    cmd.BindIndexBuffer(*draw.geometry->indices, IndexType::U32);
    cmd.DrawIndexed(draw.geometry->indexCount);
  }

  cmd.SetScissor(renderArea);
}
