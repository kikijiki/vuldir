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
  if(m_desc.handle) { // Non owned image, wrap only.
    m_handle = m_desc.handle;
  } else { // Owned image, create.
    if(m_desc.mips == 0u) {
      auto largest = std::max(m_desc.extent[0], m_desc.extent[1]);
      m_desc.mips  = toU32(std::log2(largest));
    }

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
        break;
      default:
        throw std::runtime_error("Invalid image dimension");
    }

    VDVkTry(m_device.api().CreateImage(&ci, &m_handle));

    m_device.api().GetImageMemoryRequirements(m_handle, &m_memoryDesc);
    m_allocation = m_device.AllocateMemory(
      desc.memoryType, m_memoryDesc.size, m_memoryDesc.alignment);
    if(!m_allocation.IsValid())
      throw std::runtime_error("Failed to allocate memory");

    VDVkTry(m_device.api().BindImageMemory(
      m_handle, m_allocation.pool->GetHandle(), m_allocation.offset));
  }

  if(m_desc.defaultView) AddView(*m_desc.defaultView);

  if(!m_desc.name.empty())
    m_device.SetObjectName(
      m_handle, VK_OBJECT_TYPE_IMAGE, m_desc.name.c_str());
}

Image::~Image()
{
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
  auto view = std::make_unique<View>();

  view->resource = this;
  view->format   = m_desc.format;
  view->samples  = m_desc.samples;
  view->type     = type;
  view->range    = range;

  VkImageViewCreateInfo ci{};
  ci.sType  = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  ci.image  = m_handle;
  ci.format = convert(m_desc.format);
  // ci.components
  ci.subresourceRange.baseArrayLayer = range.layerOffset;
  ci.subresourceRange.layerCount     = range.layerCount;
  ci.subresourceRange.baseMipLevel   = range.mipOffset;
  ci.subresourceRange.levelCount     = range.mipCount;
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
      ci.viewType = m_desc.extent[2] > 1u
                      ? VK_IMAGE_VIEW_TYPE_CUBE
                      : VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
      break;
    default:
      throw std::runtime_error("Invalid image dimension");
  }

  m_device.api().CreateImageView(&ci, &view->handle);
  view->binding = m_device.GetBinder().Bind(*view);

  u32 idx = 0u;
  for(auto& v: m_views)
    if(v->type == type) ++idx;

  const char* resName =
    m_desc.name.empty() ? "NONAME" : m_desc.name.c_str();
  auto name = formatString("%s %s #%u", resName, toString(type), idx);
  m_device.SetObjectName(
    view->handle, VK_OBJECT_TYPE_IMAGE_VIEW, name.c_str());
  fflush(stdout);
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
  //return m_views.empty() ? nullptr : m_views[0].get();
  return GetView(*m_desc.defaultView);
}

bool Image::Write(Span<u8 const> data)
{
  return m_allocation.pool->Write(m_allocation, data);
}
