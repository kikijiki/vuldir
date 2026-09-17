# Vuldir architecture

Vuldir exposes one rendering API over Vulkan 1.3 and DirectX 12. Public
objects describe resources, views, pipelines, command recording, and
synchronization; backend files translate them to Vulkan or DX12 objects.

## Object ownership

`Device` owns the physical/logical device, three queues (Graphics, Compute,
Copy), the swapchain, the bindless `Binder`, and shared memory pools. Buffers,
images, views, samplers, shaders, pipelines, fences, command pools, and command
buffers retain their owning `Device` and reject cross-device use.

`RenderContext` is the frame-submission unit. Each context has a ring of frame
slots sized by `maxFramesInFlight`; every slot owns a command pool and command
buffer for each queue plus its completion fence. Multiple contexts can record
in parallel. Shared queues, descriptors, memory pools, and device statistics
are synchronized internally.

## Command recording and submission

Command buffers follow `Ready -> Recording -> Closed -> Ready`. Barriers update
recording-local virtual resource states, so resetting or abandoning a recording
does not mutate global state. A successful queue submission validates that its
recorded initial states are still current and then atomically commits its final
states. Stale concurrent submissions are rejected.

`RenderContext::Write` uses a staging buffer and the Copy queue, then performs
the final transition and any Vulkan queue-family ownership handoff on Graphics.
The operation is synchronous: staging storage is reclaimed only after the
transfer and handoff complete.

`SwapchainDep` attaches acquire/release synchronization to a submission.
Vulkan uses acquire and per-image present semaphores. DX12 orders submission and
presentation on the graphics queue and uses per-frame/per-image fences. Queue
operations are serialized by `Device` in both backends.

## Bindless descriptor model

All shaders share the declarations in
[`Layout.hlsli`](src/shaders/vuldir/Layout.hlsli). A 16-byte push-constant/root-
constant block carries draw-specific indices, while resources are addressed
through stable bindless indices.

| Logical group | Vulkan set 0 binding | DX12 register mapping |
|---|---:|---|
| Static samplers | 0-5 | `s0`-`s5`, space 0 |
| Bindless samplers | 6 | `s0[]`, space 1 |
| SRV/UAV buffers | 7 | `t0[]` / `u0[]`, space 1 |
| Sampled images | 8 | `t0[]`, spaces 2-8 by dimension |
| Storage images | 9 | `u0[]`, spaces 2-6 by dimension |

Vulkan implements this as one descriptor pool, set layout, descriptor set, and
pipeline layout. Bindings use update-after-bind and partially-bound semantics.
DX12 implements the same logical layout with one sampler heap, one CBV/SRV/UAV
heap, descriptor tables, and a shared root signature. RTV and DSV descriptors
remain CPU-visible because they are attachment handles rather than shader
bindings.

Resource views allocate and populate their backend descriptor when constructed.
Destroying a view queues its bindless slot for reuse only after every registered
frame slot that could reference it has retired.

## Resources and memory

`Buffer` and `Image` descriptions carry usage, memory class, dimensions,
format, and initial ownership. `Main` memory favors device-local storage;
`Upload` and `Download` remain host-visible. Vulkan selects a compatible
memory type and suballocates from shared pools. DX12 selects the corresponding
heap type and resource state.

Images own zero or more explicit views. A default view is created only when the
image usage and format permit one; attachment-only or externally owned images
do not require it. Swapchain images wrap backend-owned resources and therefore
cannot be written through the allocation-backed upload path.

## Resource-state mapping

`ResourceState` is the common synchronization vocabulary. Vulkan derives
pipeline stages and access masks in addition to the image layout shown below;
DX12 maps directly to `D3D12_RESOURCE_STATES`.

| Vuldir state | Vulkan access / image layout | DirectX 12 state |
|---|---|---|
| `Undefined` | memory read/write; `UNDEFINED` | `COMMON` |
| `VertexBuffer` | vertex-attribute read | `VERTEX_AND_CONSTANT_BUFFER` |
| `IndexBuffer` | index read | `INDEX_BUFFER` |
| `ConstantBuffer` | uniform read | `VERTEX_AND_CONSTANT_BUFFER` |
| `IndirectArgument` | indirect-command read | `INDIRECT_ARGUMENT` |
| `RenderTarget` | color-attachment write; `COLOR_ATTACHMENT_OPTIMAL` | `RENDER_TARGET` |
| `DepthStencilRW` | depth/stencil write; `DEPTH_STENCIL_ATTACHMENT_OPTIMAL` | `DEPTH_WRITE` |
| `DepthStencilRO` | depth/stencil read; `DEPTH_STENCIL_READ_ONLY_OPTIMAL` | `DEPTH_READ` |
| `ShaderResourceGraphics` | shader read; `SHADER_READ_ONLY_OPTIMAL` | pixel + non-pixel shader resource |
| `ShaderResourceCompute` | shader read; `SHADER_READ_ONLY_OPTIMAL` | non-pixel shader resource |
| `UnorderedAccess` | shader read/write; `GENERAL` | `UNORDERED_ACCESS` |
| `CopySrc` | transfer read; `TRANSFER_SRC_OPTIMAL` | `COPY_SOURCE` |
| `CopyDst` | transfer write; `TRANSFER_DST_OPTIMAL` | `COPY_DEST` |
| `Present` | presentation access; `PRESENT_SRC_KHR` | `PRESENT` |

Buffer barriers use the access/stage mapping without an image layout. Image
barriers additionally transition layouts. Repeating an unordered-access state
emits a UAV-style memory dependency rather than being discarded as a no-op.

## Pipelines and rendering

Graphics and compute pipelines share the device bindless layout. Vulkan uses
dynamic rendering and records attachment descriptions directly with
`vkCmdBeginRendering`; it does not create render-pass or framebuffer objects.
DX12 binds RTV/DSV handles and configures the matching pipeline state object.
Pipeline creation validates shader ownership, attachment formats, sample
settings, and backend-supported raster/depth/blend features.

The API excludes DX12 root descriptors and Vulkan combined image samplers.
Shaders use separate image and sampler indices.

## Data and sample layer

The in-tree readers decode PNG/HDR images and JSON/glTF/GLB scene data into
validated CPU-side structures. The sample uploads that data through
`RenderContext`, renders its scene passes, and composites the CPU-rasterized
RmlUi debug overlay onto the swapchain.
