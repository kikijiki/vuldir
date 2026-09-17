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
  if(m_desc.size == 0u)
    throw std::invalid_argument("Buffer size must be non-zero");

  VkBufferCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  ci.size  = desc.size;

  ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  const auto queueFamily = m_device.GetQueueFamily(
    desc.isStaging ? QueueType::Copy : QueueType::Graphics);

  ci.queueFamilyIndexCount = 1;
  ci.pQueueFamilyIndices   = &queueFamily;

  ci.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  ci.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

  if(desc.usage.IsSet(ResourceUsage::ShaderResource)) {
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
  try {
    m_device.api().GetBufferMemoryRequirements(m_handle, &m_memoryDesc);
    m_allocation = m_device.AllocateMemory(
      desc.memoryType, m_memoryDesc.size, m_memoryDesc.alignment,
      m_memoryDesc.memoryTypeBits);
    if(!m_allocation.IsValid())
      throw std::runtime_error("Failed to allocate memory");

    VDVkTry(m_allocation.pool->Bind(m_handle, m_allocation));

    if(desc.defaultView) { AddView(desc.defaultView.value()); }

    if(!m_desc.name.empty())
      m_device.SetObjectName(
        m_handle, VK_OBJECT_TYPE_BUFFER, m_desc.name.c_str());
  } catch(...) {
    m_device.api().DestroyBuffer(m_handle);
    m_handle = nullptr;
    throw;
  }
  ++m_device.m_bufferCount;
}

Buffer::~Buffer()
{
  --m_device.m_bufferCount;
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
  m_views.reserve(m_views.size() + 1u);
  auto view      = std::make_unique<View>();
  view->resource = this;
  view->type     = type;
  view->range    = range;

  if(view->range.offset > m_desc.size)
    throw makeError<std::invalid_argument>(
      "Buffer view offset is out of range for '%s'",
      m_desc.name.c_str());
  if(view->range.size == MaxU64)
    view->range.size = m_desc.size - view->range.offset;
  if(
    view->range.size == 0u ||
    view->range.size > m_desc.size - view->range.offset) {
    throw makeError<std::invalid_argument>(
      "Buffer view size is out of range for '%s'",
      m_desc.name.c_str());
  }

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
  if(data.size_bytes() > m_desc.size) {
    VDLogE(
      "Write exceeds the declared size of buffer '%s'",
      m_desc.name.c_str());
    return false;
  }
  return m_allocation.pool->Write(m_allocation, data);
}

bool Buffer::Read(Span<u8> data)
{
  if(data.size_bytes() > m_desc.size) {
    VDLogE(
      "Read exceeds the declared size of buffer '%s'",
      m_desc.name.c_str());
    return false;
  }
  return m_allocation.pool->Read(m_allocation, data);
}
