# VULDIR

## Build Status

| API | Config | Windows | Ubuntu |
|-----|--------|---------|--------|
| Vulkan | Debug | ![Windows-Vulkan-Debug](https://github.com/kikijiki/Vuldir/actions/workflows/build.yml/badge.svg?branch=master&event=push&jobName=windows-latest-vk-debug) | ![Ubuntu-Vulkan-Debug](https://github.com/kikijiki/Vuldir/actions/workflows/build.yml/badge.svg?branch=master&event=push&jobName=ubuntu-latest-vk-debug) |
| Vulkan | Release | ![Windows-Vulkan-Release](https://github.com/kikijiki/Vuldir/actions/workflows/build.yml/badge.svg?branch=master&event=push&jobName=windows-latest-vk-release) | ![Ubuntu-Vulkan-Release](https://github.com/kikijiki/Vuldir/actions/workflows/build.yml/badge.svg?branch=master&event=push&jobName=ubuntu-latest-vk-release) |
| DirectX 12 | Debug | ![Windows-DirectX-Debug](https://github.com/kikijiki/Vuldir/actions/workflows/build.yml/badge.svg?branch=master&event=push&jobName=windows-latest-dx-debug) | N/A |
| DirectX 12 | Release | ![Windows-DirectX-Release](https://github.com/kikijiki/Vuldir/actions/workflows/build.yml/badge.svg?branch=master&event=push&jobName=windows-latest-dx-release) | N/A |

## What is Vuldir?

Vuldir is a renderer that provides a unified API for Vulkan and DirectX 12.
It does not use any third-party libraries, so stuff like PNG/glTF loading, memory allocators, math, etc. are implemented from scratch.

## Does it work?

I worked on this in my spare time to learn how to use Vulkan and DX12, so it's not really production-ready. It can somewhat load a simple glTF model and render it with both APIs.

## Build

Pick your CMake configuration:

- API: Vulkan 1.3 or DirectX 12
- OS: Windows or Linux (DX12 is Windows only)
- Generator: Ninja or VS2022
- Configuration: Debug or Release
  - Affects shader build too

## Architecture Notes, TODOs, etc

### Compromises and simplifications

- No support for input attachments? These are important for pixel-local loads on tile-based architectures (mobile GPUs) but are not critical for desktop applications, and are not supported by DirectX.
  - Still need to use attachments for render passes though.
- No support for root descriptors because it does not have a Vulkan equivalent.
- No support for Vulkan's combined image sampler.
- Not using Vulkan render passes and framebuffer, instead using the newer [streamlined approach](https://www.khronos.org/blog/streamlining-render-passes).

### Bindings

Use bindless for everything (ok with Vulkan 1.2 without any extension).
All the pipelines use the same [layout/signature](src/shaders/vuldir/Layout.hlsli).

Basic model:

- One single descriptor pool
- One single descriptor set
- One single descriptor layout
  - For each descriptor type, allocate a big number (under the device limit)
  - Stage flags: all.
- 2 storage buffers per shader, one with the constants and one with the bindless resource indices
- Push constants are used to reference the buffer for the current object
  - One (index+offset) for constant buffer
  - One (index+offset) for resource indices

Shader (generated from metadata):

- push_constants struct holding 2 ints for the instance identity.
- Declarations for the bindless resources:
  - constants[] <- index using PC[0].
  - resources[] <- index using PC[1], contains indices to shader resources below.
  - Required shader resources:
    - textures[]
    - samplers[]
  - On dx12 every array will be defined in a different space, same for vulkan.

Basic usage:

- On resource creation, find range of free descriptor and "allocate" (index + size, ring buffer).
  - The resource stays bound for its whole lifetime.
- On queue submit, use a fence to detect completion. On completion, free the allocation.

### Bindings

DX:

- RootSignature
  - Static Sampler[]
  - Root Parameter[]
    - Type (Constant, Table, SRV, UAV, CBV)
    - Visibility
    - One of:
      - Root constant
        - Register
        - Space
        - Size
      - Root descriptor
        - Register
        - Space
      - Descriptor Table
        - Range[]
          - Type (SRV, UAV, CBV, SAMPLER)
          - Count
          - Base register
          - Space

VK:

- PipelineLayout
  - PushConstants[]
    - Offset
    - Size
    - Flags
  - DescriptorSetLayout[]
    - DescriptorSetLayoutBinding[]
      - Binding index
      - Type (sampler, image, buffer, constant, ...)
      - Count
      - Flags
      - Static Sampler[]

DX<->VK

- Root constant -> Push constant
- Root descriptor -> NONE
- Descriptor table -> Descriptor set

#### Vulkan

- Create descriptor pool
- Allocate descriptor set (using layout)
- Create descriptor update info
- Set resources/views
- Execute update
- Bind descriptor sets
- Draw

#### DX12

- Create CPU heap
- Allocate descriptors (views)
- Create GPU heap
- Copy desscriptors (for binding)
- Bind descriptors (update is done through mapped memory)
- Bind gpu heap
- Draw

#### Interface

Binding

- Key
  - Stages
  - View type
  - Set/Space idx
  - Binding idx
  - Count
- Value
  - View handle

Layout

- DX
- VK
  - Vector of DescriptorSetLayout

Layout

- Input: list of bindings
- DX
  - For each binding
    - Add a heap type entry with an offset (increasing).
    - Save offsets and ranges in temp variables.
  - With the accumulate info, create the descriptor tables info and root parameters.
  - With the root parameters, create the root signature.
- VK
  - For each binding
    - Create DescriptorSetLayoutBinding and DescriptorBindingFlags.
    - Create intermediate vulkan structs.
    - Create PipelineLayoutCreateInfo and pipeline.

#### Static samplers

- Create sampler object
  - Parameters are from the Desc, except binding index, set/space and stage visibility.
  - For DX12 use `D3D12_SAMPLER_DESC`, then when binding convert to `D3D12_STATIC_SAMPLER_DESC`. There is nothing to create.
  - For VK you need to create the sampler both for immutable and dynamic samplers.
- Create layout
  - Set index, set/space and stage visibility.
  - Pass in layout desc for initialization.

#### Binding set

- Input: Layout
- DX
  - For each heap desc create a gpu range.
  - Gpu heap auto expands like vector.
  - Request is made to a descriptor pool.
  - Result is stored in a map of heap type/range in the binding set.
- VK
  - For each set layout
    - Allocate descriptor set (batch) (using pool)
    - Store result.

Update bindings:

- For each binding:
  - DX
    - Copy cpu handles to gpu
    - Heap type and offset are obtained from the set (map of key/layout).
    - The destination gpu heap range too is obrained from the set.
  - VK
    - Fill WriteDescriptorSets and update.
    - View descriptors are obtained from the views (create in the view CTOR).

#### Bindless

- GPU heap split in 2
  - Top: normal descriptors
  - Bottom: Bindless range

### Resource state

- Vertex buffer
  - VK: VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT
  - DX: D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
- Index buffer
  - VK: VK_ACCESS_INDEX_READ_BIT
  - DX: D3D12_RESOURCE_STATE_INDEX_BUFFER
- Constant buffer
  - VK: VK_ACCESS_UNIFORM_READ_BIT
  - DX: D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
- Render target
  - VK: VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT / VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
  - DX: D3D12_RESOURCE_STATE_RENDER_TARGET
- Unordered access
  - VK: VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT / VK_IMAGE_LAYOUT_GENERAL
  - DX: D3D12_RESOURCE_STATE_UNORDERED_ACCESS
- Depth (read)
  - VK: VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT / VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
  - DX: D3D12_RESOURCE_STATE_DEPTH_READ
- Depth (write)
  - VK: VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT / VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
  - DX: D3D12_RESOURCE_STATE_DEPTH_WRITE
- Shader resource
  - VK: VK_ACCESS_SHADER_READ_BIT / VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
  - DX: D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
- Copy source
  - VK: VK_ACCESS_TRANSFER_READ_BIT / VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
  - DX: D3D12_RESOURCE_STATE_COPY_SOURCE
- Copy destination
  - VK: VK_ACCESS_TRANSFER_WRITE_BIT / VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
  - DX: D3D12_RESOURCE_STATE_COPY_DEST
- Present
  - VK: VK_ACCESS_MEMORY_READ_BIT / VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
  - DX: D3D12_RESOURCE_STATE_PRESENT
- Indirect argument
  - VK: VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT
  - DX: D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT

### Resource creation

#### Image

- Create params
  - Image type: 1D,2D,3D,cube
    - VK: VkImageType
    - DX: D3D12_RESOURCE_DIMENSION_TEXTUREXX
  - Bind flags: renderTarget, depthStencil, shaderResource, unorderedAccess, constant, idxbuf, vtxbuf, ...
    - VK: VkImageUsageFlagBits
    - DX: D3D12_RESOURCE_FLAG_XX
  - Format
  - Sample count
  - Size
  - Mips
- Handle
  - VK: VkImage
  - DX: ID3D12Resource

#### Image view

- Create params
  - Image
  - View dimension: 1D, 2D, 3D, cube
    - VK: VkImageViewType
    - DX: D3D12_SRV_DIMENSION_XX, D3D12_UAV_DIMENSION_XX
  - Format (same as image)
  - Subresource range (default: all)
    - VK: VkImageSubresourceRange
    - DX: XX_VIEW_DESC
  - Aspect: color, depth, stencil, ... (get from format)
    - VK: VkImageAspectFlagBits
    - DX:
- CTOR
  - DX: Allocate handle from cpu pool.
  - Create the view
    - DX: CreateShaderResourceView, CreateUnorderedAccessView, CreateDepthStencilView
    - VK: vkCreateImageView

VK:

- Create image
- Create image view

### Descriptor pools

- Cpu visible descriptor heap (DX ONLY):
  - Types: SRV, Sampler, RTV, DSV
  - Owner: device
- Shader visible descriptor heap:
  - Types: SRV, Sampler
  - Owner: context
- They both have a cpu and gpu handle (handle to the first element + size).
