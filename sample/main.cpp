#include "Sample.hpp"

using namespace vd;

// #ifdef VD_OS_WINDOWS
//  INT WINAPI WinMain(
//   HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/,
//   PSTR /*lpCmdLine*/, INT /*nCmdShow*/)
// #else
int main()
// #endif
{
  try {
    Window window{"Vuldir Sample", 400u, 400u};

    Device dev{
      {.dbgEnable = true,
       //.dbgUseSoftwareRenderer = true,
       //.dbgUseDRED      = true,
       //.dbgUseRenderdoc = true,
       .dbgLogLevel = LogLevel::Info},
      Swapchain::Desc{.window = window.GetHandle()}};

    auto& sc = dev.GetSwapchain();

    RenderContext ctx(dev, {});

    auto res = createResources(dev, sc.GetImageCount());
    createSwapchainDependentResources(dev, res);
    prepareScene(ctx, res);

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
      createSwapchainDependentResources(dev, res);

      viewport.extent   = sc.GetExtentF();
      renderArea.extent = sc.GetExtent();
    };

    // glTF loading.
    window.OnFileDrop = [](const Arr<Str>& files) {
      for(const auto& file: files) {
        if(file.ends_with(".gltf")) {
          auto reader = DataReader(DataReader::Desc{});
          auto model  = reader.ReadModel(file, {});
          break;
        }
      }
    };

    f32 angle = .0f;
    window.Run([&]() {
      auto&       backbuffer = sc.AcquireNextImage(true);
      const auto  frameIdx   = sc.GetFrameIndex();
      const auto& frame      = res.frames[frameIdx];

      ctx.Reset();

      // Spin
      angle += .01f;
      res.objects[0]->data.world = vd::mt::RotationY(angle);
      res.objects[0]->Update();

      // Get command buffer for graphics queue.
      auto& cmd = ctx.GetCmd(QueueType::Graphics);
      cmd.Begin();

      cmd.SetViewport(viewport);
      cmd.SetScissor(renderArea);

      // Reset resources state.
      for(u32 idx = 0; idx < GBufferCount; ++idx) {
        cmd.AddBarrier(
          *frame.gbuffers[idx], ResourceState::RenderTarget);
      }
      cmd.AddBarrier(
        *frame.depthStencil, ResourceState::DepthStencilRW);
      cmd.AddBarrier(backbuffer, ResourceState::RenderTarget);

      cmd.FlushBarriers();

      for(auto& pass: frame.passes) {
        pass.pipeline->Bind(cmd);

        if(pass.id == PassLighting) {
          SArr<u32, 4> push{
            frame.scene->buffer->GetView(ViewType::SRV)->binding.index,
            0u, 0u, 0u};
          cmd.PushConstants(push);

          cmd.BeginRendering(pass.color, &pass.depthStencil);
          cmd.Draw(3u); // Fullscreen triangle
          cmd.EndRendering();
        } else {
          // Draw all objects in the scene.
          for(const auto& object: res.objects) {
            SArr<u32, 4> push{
              frame.scene->buffer->GetView(ViewType::SRV)
                ->binding.index,
              object->buffer->GetView(ViewType::SRV)->binding.index, 0u,
              0u};
            cmd.PushConstants(push);

            cmd.BeginRendering(pass.color, &pass.depthStencil);
            cmd.Draw(object->meta);
            cmd.EndRendering();
          }
        }

        // DepthStencil is in the DepthStencilRO state after the Depth pass.
        if(pass.id == PassDepth) {
          cmd.AddBarrier(
            *frame.depthStencil, ResourceState::DepthStencilRO);
          cmd.FlushBarriers();
        }

        // GBuffer is in the ShaderResourceGraphics state after the GBuffer pass.
        if(pass.id == PassGBuffer) {
          for(u32 idx = 0; idx < GBufferCount; ++idx) {
            cmd.AddBarrier(
              *frame.gbuffers[idx],
              ResourceState::ShaderResourceGraphics);
          }
          cmd.FlushBarriers();
        }
      }

      // Transition backbuffer for present.
      cmd.AddBarrier(backbuffer, ResourceState::Present);
      cmd.FlushBarriers();

      // Submit commands to gpu and schedule the next present.
      cmd.End();
      CommandBuffer* cmds[] = {&cmd};
      ctx.Submit(cmds, {}, {}, SwapchainDep::AcquireRelease);
      sc.Present();
      sc.NextFrame();

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
