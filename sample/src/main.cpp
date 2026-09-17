#include "Scene.hpp"
#include "Ui.hpp"

#include <filesystem>

using namespace vd;

int main()
{
  try {
    Window window{"Vuldir - DamagedHelmet", 1280u, 800u};

    Device dev{
      {
#if defined(__MINGW32__) || defined(NDEBUG)
        // Release builds omit validation overhead. Wine + vkd3d-proton also
        // cannot provide the D3D12 debug layer or RenderDoc hooks.
        .dbgEnable       = false,
        .dbgUseRenderdoc = false,
#else
        .dbgEnable       = true,
        // RenderDoc capture/replay requirements exclude software Vulkan
        // implementations and are unnecessary unless capture is requested.
        .dbgUseRenderdoc = false,
#endif
        .dbgLogLevel = LogLevel::Verbose},
      Swapchain::Desc{
        .window = window.GetHandle(),
        .size = UInt2{
          window.GetContentWidth(), window.GetContentHeight()}}};

    auto&         sc = dev.GetSwapchain();
    RenderContext ctx(dev, {});

    Scene scene(dev, sc.GetMaxFramesInFlight());
    scene.createSwapchainDependentResources(dev);
    scene.prepare(ctx);
    scene.loadSmaaTextures(ctx);

    // Default scene. Paths are relative to the sample binary directory
    // (_build/<preset>/sample/<Config>/).
    scene.loadGltfModel(
      ctx,
      "../../../../sample/assets/DamagedHelmet/glTF/"
      "DamagedHelmet.gltf");
    scene.loadEnvironment(
      ctx, "../../../../sample/assets/env/studio_small_09_1k.hdr");

    // RmlUi panel; assets are copied next to the binary by CMake.
    Ui         ui(window, scene, ctx);
    const auto setSceneTitle = [&](const Str& scenePath) {
      const Str sceneName =
        std::filesystem::path(scenePath).stem().string();
      window.SetTitle("Vuldir - " + sceneName);
    };
    ui.OnLoadModel = [&](const Str& path) {
      dev.WaitIdle();
      scene.clearModels();
      scene.loadGltfModel(ctx, path);
      ui.refreshSceneTree();
      setSceneTitle(path);
    };
    ui.OnLoadCube = [&]() {
      dev.WaitIdle();
      scene.clearModels();
      scene.loadCubeModel(ctx);
      ui.refreshSceneTree();
      window.SetTitle("Vuldir - Cube");
    };
    ui.OnClear = [&]() {
      dev.WaitIdle();
      scene.clearModels();
      ui.refreshSceneTree();
      window.SetTitle("Vuldir - Empty");
    };

    Viewport viewport{
      .offset = {}, .extent = sc.GetExtentF(), .depthExtent = {0, 1}};
    Rect renderArea{.offset = {0, 0}, .extent = sc.GetExtent()};

    // Defer recreate to the frame loop: the window resize event can arrive
    // before the surface's currentExtent updates, which would bake in the
    // old size. IsSurfaceExtentStale() catches the later update.
    bool resizePending = false;

    auto recreateSwapchain = [&]() {
      const u32 w = window.GetContentWidth();
      const u32 h = window.GetContentHeight();
      if(w == 0u || h == 0u) return;

      dev.WaitIdle();
      sc.Resize(UInt2{w, h});
      if(sc.GetExtent()[0] == 0u || sc.GetExtent()[1] == 0u) return;

      scene.createSwapchainDependentResources(dev);
      viewport.extent   = sc.GetExtentF();
      renderArea.extent = sc.GetExtent();
      sc.ClearNeedsRecreate();
      resizePending = false;

      VDLogV(
        "Swapchain recreate: surface/sc=%ux%u window=%ux%u",
        sc.GetExtent()[0], sc.GetExtent()[1], w, h);
    };

    window.OnResize = [&] { resizePending = true; };

    // Drop .hdr to reload IBL, or .gltf/.glb to load models.
    window.OnFileDrop = [&](const Arr<Str>& files) {
      for(const auto& file: files) {
        if(file.ends_with(".hdr")) {
          dev.WaitIdle();
          scene.loadEnvironment(ctx, file);
          break;
        }
        if(
          file.ends_with(".gltf") || file.ends_with(".glb") ||
          file.ends_with(".GLTF") || file.ends_with(".GLB")) {
          ui.OnLoadModel(file);
          break;
        }
      }
    };

    window.Run([&]() {
      if(
        resizePending || sc.NeedsRecreate() ||
        sc.IsSurfaceExtentStale()) {
        recreateSwapchain();
      }
      if(sc.GetExtent()[0] == 0u || sc.GetExtent()[1] == 0u) {
        return true;
      }
      if(!scene.render(ctx, viewport, renderArea, &ui)) {
        recreateSwapchain();
      }
      return true;
    });

    // Drain the GPU before locals (Ui, Scene, Device) are destroyed in
    // reverse order.
    dev.WaitIdle();
  }
  catch(const std::exception& ex) {
    VDLogE("==================================================");
    VDLogE("Uncaught exception: %s", ex.what());
    VDLogE("==================================================");
    return 1;
  }
  catch(...) {
    VDLogE("==================================================");
    VDLogE("Uncaught exception: UNKNOWN");
    VDLogE("==================================================");
    return 1;
  }

  return 0;
}
