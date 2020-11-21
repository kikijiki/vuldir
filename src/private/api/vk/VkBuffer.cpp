#include "vuldir/api/Binder.hpp"
#include "vuldir/api/Buffer.hpp"
#include "vuldir/api/Device.hpp"
#include "vuldir/api/vk/VkDispatcher.hpp"
#include "vuldir/api/vk/VkUti.hpp"

using namespace vd;

Buffer::Buffer(Device& device, const Desc& desc):
  m_device{device},
  m_desc{desc},
  m_state{ResourceState::Undefined},
  m_memoryDesc{},
  m_handle{}
{
  VkBufferCreateInfo ci{};
  ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  ci.size        = desc.size;
  ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  ci.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  ci.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

  if(desc.usage.IsSet(ResourceUsage::ShaderResource)) {
    // ci.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    // ci.usage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
    ci.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    ci.usage |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
  }
  if(desc.usage.IsSet(ResourceUsage::UnorderedAccess)) {
    ci.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    ci.usage |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
  }
  if(desc.usage.IsSet(ResourceUsage::IndexBuffer))
    ci.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  if(desc.usage.IsSet(ResourceUsage::VertexBuffer))
    ci.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  if(desc.usage.IsSet(ResourceUsage::IndirectArgument))
    ci.usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

  VDVkTry(m_device.api().CreateBuffer(&ci, &m_handle));

  m_device.api().GetBufferMemoryRequirements(m_handle, &m_memoryDesc);
  m_allocation = m_device.AllocateMemory(
    desc.memoryType, m_memoryDesc.size, m_memoryDesc.alignment);
  if(!m_allocation.IsValid())
    throw std::runtime_error("Failed to allocate memory");

  VDVkTry(m_device.api().BindBufferMemory(
    m_handle, m_allocation.pool->GetHandle(), m_allocation.offset));

  if(desc.defaultView) { AddView(desc.defaultView.value()); }

  if(!m_desc.name.empty())
    m_device.SetObjectName(
      m_handle, VK_OBJECT_TYPE_BUFFER, m_desc.name.c_str());
}

Buffer::~Buffer()
{
  for(auto& view: m_views) {
    m_device.api().DestroyBufferView(view->handle);
    m_device.GetBinder().Unbind(view->binding);
  }
  m_views.clear();

  if(m_handle) m_device.api().DestroyBuffer(m_handle);

  m_allocation.Free();

  m_handle = nullptr;
}

u32 Buffer::AddView(ViewType type, const ViewRange& range)
{
  auto view      = std::make_unique<View>();
  view->resource = this;
  view->type     = type;
  view->range    = range;

  // if(is a texel buffer) {
  //  VkBufferViewCreateInfo ci{};
  //  ci.sType  = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
  //  ci.buffer = m_handle;
  //  ci.format = VK_FORMAT_UNDEFINED;
  //  ci.offset = rangeoffset;
  //  ci.range  = range.size;
  //
  //  VDVkTry(m_device.api().CreateBufferView(&ci, &view.handle));
  //}

  u32 idx = 0u;
  for(auto& v: m_views)
    if(v->type == type) ++idx;

  if(view->handle) {
    const char* resName =
      m_desc.name.empty() ? "NONAME" : m_desc.name.c_str();
    auto name = formatString("%s %s #%u", resName, toString(type), idx);
    m_device.SetObjectName(
      view->handle, VK_OBJECT_TYPE_BUFFER_VIEW, name.c_str());
  }

  view->binding = m_device.GetBinder().Bind(*view);
  m_views.push_back(std::move(view));

  return idx;
}

const Buffer::View* Buffer::GetView(ViewType type, u32 index) const
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

bool Buffer::Write(Span<u8 const> data)
{
  return m_allocation.pool->Write(m_allocation, data);
}
