#include "vuldir/api/Binder.hpp"
#include "vuldir/api/Device.hpp"
#include "vuldir/api/PhysicalDevice.hpp"
#include "vuldir/api/Sampler.hpp"
#include "vuldir/api/vk/VkDispatcher.hpp"
#include "vuldir/api/vk/VkUti.hpp"

using namespace vd;

Sampler::Sampler(Device& device, const Desc& desc): Sampler(device, desc, true)
{}

Sampler::Sampler(Device& device, const Desc& desc, bool bindless):
  m_device{device}, m_desc{desc}, m_view{}
{
  if(desc.anisotropyEnable) {
    const auto& physicalDevice = m_device.GetPhysicalDevice();
    if(!physicalDevice.GetFeatures()->samplerAnisotropy)
      throw std::runtime_error("Sampler anisotropy is not supported");

    const auto limit =
      physicalDevice.GetProperties()->limits.maxSamplerAnisotropy;
    if(
      !std::isfinite(desc.anisotropyMax) || desc.anisotropyMax < 1.f ||
      desc.anisotropyMax > limit)
      throw std::invalid_argument(
        "Sampler anisotropy must be between 1 and the device limit");
  }

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
  if(bindless) m_view.binding = m_device.GetBinder().Bind(m_view);
}

Sampler::~Sampler()
{
  if(m_view.binding.IsValid())
    m_device.GetBinder().Unbind(m_view.binding);
  if(m_view.handle) m_device.api().DestroySampler(m_view.handle);

  m_view.handle = nullptr;
}
