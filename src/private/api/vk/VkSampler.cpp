#include "vuldir/api/Device.hpp"
#include "vuldir/api/Sampler.hpp"
#include "vuldir/api/vk/VkDispatcher.hpp"
#include "vuldir/api/vk/VkUti.hpp"

using namespace vd;

Sampler::Sampler(Device& device, const Desc& desc):
  m_device{device}, m_desc{desc}, m_view{}
{
  VkSamplerCreateInfo ci{};

  ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

  ci.minFilter  = convert(desc.minFilter);
  ci.magFilter  = convert(desc.magFilter);
  ci.mipmapMode = desc.mipFilter == SamplerFilter::Nearest
                    ? VK_SAMPLER_MIPMAP_MODE_NEAREST
                    : VK_SAMPLER_MIPMAP_MODE_LINEAR;

  ci.addressModeU = convert(desc.u);
  ci.addressModeV = convert(desc.v);
  ci.addressModeW = convert(desc.w);

  ci.minLod     = desc.lodMin;
  ci.maxLod     = desc.lodMax;
  ci.mipLodBias = desc.lodBias;

  ci.borderColor = convert(desc.color);

  ci.anisotropyEnable = desc.anisotropyEnable;
  ci.maxAnisotropy    = desc.anisotropyMax;

  ci.compareEnable = desc.compareEnable;
  ci.compareOp     = convert(desc.compareOp);

  VDVkTry(m_device.api().CreateSampler(&ci, &m_view.handle));
}

Sampler::~Sampler()
{
  if(m_view.handle) m_device.api().DestroySampler(m_view.handle);

  m_view.handle = nullptr;
}
