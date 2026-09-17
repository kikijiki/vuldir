#include "vuldir/api/Binder.hpp"
#include "vuldir/api/Device.hpp"
#include "vuldir/api/Sampler.hpp"

using namespace vd;

#include "vuldir/api/dx/DxUti.hpp"

Sampler::Sampler(Device& device, const Desc& desc):
  m_device{device}, m_view{}
{
  if(
    desc.anisotropyEnable &&
    (!std::isfinite(desc.anisotropyMax) || desc.anisotropyMax < 1.f ||
     desc.anisotropyMax > D3D12_MAX_MAXANISOTROPY))
    throw std::invalid_argument("Sampler anisotropy must be between 1 and 16");

  D3D12_SAMPLER_DESC smp{};

  smp.Filter = convert(
    desc.minFilter, desc.magFilter, desc.mipFilter,
    desc.anisotropyEnable, desc.compareEnable);
  smp.AddressU       = convert(desc.u);
  smp.AddressV       = convert(desc.v);
  smp.AddressW       = convert(desc.w);
  smp.MipLODBias     = desc.lodBias;
  smp.MaxAnisotropy  = toU32(desc.anisotropyMax);
  smp.ComparisonFunc = convert(desc.compareOp);
  smp.MinLOD         = desc.lodMin;
  smp.MaxLOD         = desc.lodMax;

  switch(desc.color) {
    case SamplerBorderColor::FloatTransparentBlack:
      smp.BorderColor[0] = 0.f;
      smp.BorderColor[1] = 0.f;
      smp.BorderColor[2] = 0.f;
      smp.BorderColor[3] = 0.f;
      break;
    case SamplerBorderColor::FloatOpaqueBlack:
      smp.BorderColor[0] = 0.f;
      smp.BorderColor[1] = 0.f;
      smp.BorderColor[2] = 0.f;
      smp.BorderColor[3] = 1.f;
      break;
    case SamplerBorderColor::FloatOpaqueWhite:
      smp.BorderColor[1] = 1.f;
      smp.BorderColor[2] = 1.f;
      smp.BorderColor[3] = 1.f;
      smp.BorderColor[0] = 1.f;
      break;
  }

  m_view.handle = m_device.GetDescriptorPool().Allocate(
    D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  m_device.api().CreateSampler(&smp, m_view.handle.cpu);
  m_view.binding = m_device.GetBinder().Bind(m_view);
}

Sampler::~Sampler()
{
  m_device.GetBinder().Unbind(m_view.binding);
  m_device.GetDescriptorPool().Free(m_view.handle);
}
