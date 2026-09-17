#include "vuldir/api/Binder.hpp"
#include "vuldir/api/Device.hpp"
#include "vuldir/api/Image.hpp"
#include "vuldir/api/vk/VkDispatcher.hpp"
#include "vuldir/api/vk/VkUti.hpp"

using namespace vd;

Image::Image(Device& device, const Desc& desc):
  m_device{device},
  m_desc{desc},
  m_state{ResourceState::Undefined},
  m_views{},
  m_allocation{},
  m_memoryDesc{},
  m_handle{}
{
  if(!m_desc.handle && m_desc.memoryType != MemoryType::Main)
    throw std::invalid_argument(
      "Owned images require Main memory; use RenderContext for transfers");
  if(m_desc.format == Format::UNDEFINED)
    throw std::invalid_argument("Image format must be defined");
  if(
    m_desc.samples == 0u || (m_desc.samples & (m_desc.samples - 1u)) != 0u ||
    m_desc.samples > 64u)
    throw std::invalid_argument("Image sample count is invalid");

  if(
    m_desc.extent[0] == 0u ||
    (m_desc.dimension != Dimension::e1D && m_desc.extent[1] == 0u) ||
    (m_desc.dimension == Dimension::e3D && m_desc.extent[2] == 0u))
    throw std::runtime_error("Image extent must be non-zero");

  if(m_desc.dimension == Dimension::e1D) m_desc.extent[1] = 1u;
  if(
    m_desc.dimension == Dimension::e1D ||
    m_desc.dimension == Dimension::e2D) {
    m_desc.extent[2] = std::max(1u, m_desc.extent[2]);
  } else if(m_desc.dimension == Dimension::eCube) {
    m_desc.extent[2] = std::max(6u, m_desc.extent[2]);
    if(m_desc.extent[2] % 6u != 0u)
      throw std::invalid_argument(
        "Cube image layer count must be a multiple of six");
    if(m_desc.extent[0] != m_desc.extent[1])
      throw std::invalid_argument("Cube image width and height must match");
  } else if(m_desc.dimension != Dimension::e3D) {
    throw std::invalid_argument("Invalid image dimension");
  }

  if(m_desc.mips == 0u) {
    u32 largest = std::max(m_desc.extent[0], m_desc.extent[1]);
    if(m_desc.dimension == Dimension::e3D)
      largest = std::max(largest, m_desc.extent[2]);
    m_desc.mips = 1u;
    while(largest >>= 1u) ++m_desc.mips;
  }
  u32 maxMips = 1u;
  u32 largest = std::max(m_desc.extent[0], m_desc.extent[1]);
  if(m_desc.dimension == Dimension::e3D)
    largest = std::max(largest, m_desc.extent[2]);
  while(largest >>= 1u) ++maxMips;
  if(m_desc.mips > maxMips)
    throw std::invalid_argument("Image mip count exceeds its extent");
  if(
    m_desc.samples > 1u &&
    (m_desc.mips != 1u ||
     m_desc.usage.IsSet(ResourceUsage::UnorderedAccess) ||
     (m_desc.dimension != Dimension::e2D &&
      m_desc.dimension != Dimension::eCube)))
    throw std::invalid_argument(
      "Multisampled images must be 2D, single-mip, and not unordered access");

  const bool depthFormat = getFormatAspect(m_desc.format) != ImageAspect::Color;
  if(
    (m_desc.usage.IsSet(ResourceUsage::DepthStencil) && !depthFormat) ||
    (m_desc.usage.IsSet(ResourceUsage::RenderTarget) && depthFormat))
    throw std::invalid_argument("Image format is incompatible with its usage");

  if(m_desc.handle) { // Non owned image, wrap only.
    m_handle = m_desc.handle;
  } else { // Owned image, create.
    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.format        = convert(m_desc.format);
    ci.extent.width  = m_desc.extent[0];
    ci.extent.height = m_desc.extent[1];
    ci.extent.depth  = 1u; // Updated later based on dimensions.
    ci.samples     = static_cast<VkSampleCountFlagBits>(m_desc.samples);
    ci.mipLevels   = m_desc.mips;
    ci.arrayLayers = 1u;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.tiling      = VK_IMAGE_TILING_OPTIMAL;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    ci.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ci.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    if(m_desc.usage.IsSet(ResourceUsage::ShaderResource))
      ci.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if(m_desc.usage.IsSet(ResourceUsage::UnorderedAccess))
      ci.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if(m_desc.usage.IsSet(ResourceUsage::DepthStencil))
      ci.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if(m_desc.usage.IsSet(ResourceUsage::RenderTarget))
      ci.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    switch(m_desc.dimension) {
      case Dimension::e1D:
        ci.imageType   = VK_IMAGE_TYPE_1D;
        ci.arrayLayers = m_desc.extent[2];
        break;
      case Dimension::e2D:
        ci.imageType   = VK_IMAGE_TYPE_2D;
        ci.arrayLayers = m_desc.extent[2];
        break;
      case Dimension::e3D:
        ci.imageType    = VK_IMAGE_TYPE_3D;
        ci.extent.depth = m_desc.extent[2];
        break;
      case Dimension::eCube:
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        ci.arrayLayers = m_desc.extent[2];
        break;
      default:
        throw std::runtime_error("Invalid image dimension");
    }

    VDVkTry(m_device.api().CreateImage(&ci, &m_handle));
    try {
      m_device.api().GetImageMemoryRequirements(m_handle, &m_memoryDesc);
      m_allocation = m_device.AllocateMemory(
        desc.memoryType, m_memoryDesc.size, m_memoryDesc.alignment,
        m_memoryDesc.memoryTypeBits);
      if(!m_allocation.IsValid())
        throw std::runtime_error("Failed to allocate memory");

      VDVkTry(m_allocation.pool->Bind(m_handle, m_allocation));
    } catch(...) {
      m_device.api().DestroyImage(m_handle);
      m_handle = nullptr;
      throw;
    }
  }
  try {
    if(m_desc.defaultView) AddView(*m_desc.defaultView);

    if(!m_desc.name.empty())
      m_device.SetObjectName(
        m_handle, VK_OBJECT_TYPE_IMAGE, m_desc.name.c_str());
  } catch(...) {
    if(!m_desc.handle && m_handle) m_device.api().DestroyImage(m_handle);
    m_handle = nullptr;
    throw;
  }
  ++m_device.m_imageCount;
}

Image::~Image()
{
  --m_device.m_imageCount;
  for(auto& view: m_views) {
    m_device.GetBinder().Unbind(view->binding);
    m_device.api().DestroyImageView(view->handle);
  }
  m_views.clear();

  if(!m_desc.handle && m_handle) { // Owned image, destroy.
    m_device.api().DestroyImage(m_handle);
  }

  m_allocation.Free();

  m_handle = nullptr;
}

u32 Image::AddView(ViewType type, const ViewRange& range)
{
  m_views.reserve(m_views.size() + 1u);
  auto view = std::make_unique<View>();

  view->resource = this;
  view->format   = m_desc.format;
  view->samples  = m_desc.samples;
  view->type     = type;
  view->range    = range;

  const u32 layerCount =
    m_desc.dimension == Dimension::e3D ? 1u : m_desc.extent[2];
  if(view->range.mipOffset >= m_desc.mips)
    throw std::invalid_argument("Image view mip offset is out of range");
  if(view->range.mipCount == MaxU32)
    view->range.mipCount = m_desc.mips - view->range.mipOffset;
  if(
    view->range.mipCount == 0u ||
    view->range.mipCount > m_desc.mips - view->range.mipOffset) {
    throw std::invalid_argument("Image view mip count is out of range");
  }
  if(view->range.layerOffset >= layerCount)
    throw std::invalid_argument("Image view layer offset is out of range");
  if(view->range.layerCount == MaxU32)
    view->range.layerCount = layerCount - view->range.layerOffset;
  if(
    view->range.layerCount == 0u ||
    view->range.layerCount > layerCount - view->range.layerOffset) {
    throw std::invalid_argument("Image view layer count is out of range");
  }

  const bool usageValid =
    (type == ViewType::SRV &&
     m_desc.usage.IsSet(ResourceUsage::ShaderResource)) ||
    (type == ViewType::UAV &&
     m_desc.usage.IsSet(ResourceUsage::UnorderedAccess)) ||
    (type == ViewType::RTV &&
     m_desc.usage.IsSet(ResourceUsage::RenderTarget)) ||
    (type == ViewType::DSV &&
     m_desc.usage.IsSet(ResourceUsage::DepthStencil));
  if(!usageValid)
    throw std::invalid_argument(
      "Image view type is incompatible with image usage");

  VkImageViewCreateInfo ci{};
  ci.sType  = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  ci.image  = m_handle;
  ci.format = convert(m_desc.format);
  ci.subresourceRange.baseArrayLayer = view->range.layerOffset;
  ci.subresourceRange.layerCount     = view->range.layerCount;
  ci.subresourceRange.baseMipLevel   = view->range.mipOffset;
  ci.subresourceRange.levelCount     = view->range.mipCount;
  ci.subresourceRange.aspectMask     = getVkAspectFlags(m_desc.format);

  switch(m_desc.dimension) {
    case Dimension::e1D:
      ci.viewType = m_desc.extent[2] > 1u ? VK_IMAGE_VIEW_TYPE_1D_ARRAY
                                          : VK_IMAGE_VIEW_TYPE_1D;
      break;
    case Dimension::e2D:
      ci.viewType = m_desc.extent[2] > 1u ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                          : VK_IMAGE_VIEW_TYPE_2D;
      break;
    case Dimension::e3D:
      ci.viewType = VK_IMAGE_VIEW_TYPE_3D;
      break;
    case Dimension::eCube:
      // Single cubemap uses CUBE; cube arrays need >6 layers.
      ci.viewType = m_desc.extent[2] > 6u
                      ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY
                      : VK_IMAGE_VIEW_TYPE_CUBE;
      break;
    default:
      throw std::runtime_error("Invalid image dimension");
  }

  VDVkTry(m_device.api().CreateImageView(&ci, &view->handle));
  try {
    view->binding = m_device.GetBinder().Bind(*view);
  } catch(...) {
    m_device.api().DestroyImageView(view->handle);
    view->handle = nullptr;
    throw;
  }

  u32 idx = 0u;
  for(auto& v: m_views)
    if(v->type == type) ++idx;

  const char* resName =
    m_desc.name.empty() ? "NONAME" : m_desc.name.c_str();
  auto name = formatString("%s %s #%u", resName, toString(type), idx);
  m_device.SetObjectName(
    view->handle, VK_OBJECT_TYPE_IMAGE_VIEW, name.c_str());
  m_views.push_back(std::move(view));

  return idx;
}

const Image::View* Image::GetView(ViewType type, u32 index) const
{
  u32 typeIdx = 0u;
  for(auto& view: m_views) {
    if(view->type != type) continue;

    if(typeIdx == index) return view.get();
    else
      typeIdx++;
  }

  return nullptr;
}

const Image::View* Image::GetView() const
{
  return m_desc.defaultView ? GetView(*m_desc.defaultView) : nullptr;
}

bool Image::Write(Span<u8 const> data)
{
  if(!m_allocation.IsValid()) {
    VDLogE("Cannot write an externally owned image");
    return false;
  }
  return m_allocation.pool->Write(m_allocation, data);
}
