#include "Scene.hpp"

using namespace vd;

//#ifdef VD_OS_WINDOWS
//INT WINAPI WinMain(
//  HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/,
//  PSTR /*lpCmdLine*/, INT /*nCmdShow*/)
//#else
int main()
//#endif
{
  try {
    Window window{"Vuldir Sample", 400u, 400u};

    Device dev{
      {.dbgEnable = true,
       //.dbgUseSoftwareRenderer = true,
       //.dbgUseDRED      = true,
       .dbgUseRenderdoc = true,
       .dbgLogLevel     = LogLevel::Verbose},
      Swapchain::Desc{.window = window.GetHandle()}};

    auto&         sc = dev.GetSwapchain();
    RenderContext ctx(dev, {});

    Scene scene(dev, sc.GetImageCount());
    scene.createSwapchainDependentResources(dev);
    scene.prepare(ctx);

    // Default scene.
    //scene.loadCubeModel(ctx);
    scene.loadGltfModel(
      ctx,
      R"(C:\Users\kikijiki\Desktop\assets\Avocado\glTF\Avocado.gltf)");

    Viewport viewport{
      .offset = {}, .extent = sc.GetExtentF(), .depthExtent = {0, 1}};
    Rect renderArea{.offset = {0, 0}, .extent = sc.GetExtent()};

    window.OnResize = [&]() {
      // Don't recreate if dimensions haven't changed.
      if(
        sc.GetExtent()[0] == window.GetContentWidth() &&
        sc.GetExtent()[1] == window.GetContentHeight())
        return;

      // Wait for the device to be idle before recreating resources.
      dev.WaitIdle();

      // TODO: Stale descriptors?
      sc.Resize();
      scene.createSwapchainDependentResources(dev);

      viewport.extent   = sc.GetExtentF();
      renderArea.extent = sc.GetExtent();
    };

    // glTF loading.
    window.OnFileDrop = [&](const Arr<Str>& files) {
      for(const auto& file: files) {
        if(file.ends_with(".gltf")) {
          dev.WaitIdle();
          scene.loadGltfModel(ctx, file);
          break;
        }
      }
    };

    window.Run([&]() {
      scene.render(ctx, viewport, renderArea);
      return true;
    });

    // Wait for all pending operations before destroying stuff.
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
