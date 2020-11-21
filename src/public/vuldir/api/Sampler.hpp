#pragma once

#include "vuldir/api/Common.hpp"

namespace vd {

class Device;

class Sampler
{
public:
  struct View;

  struct Desc {
    SamplerFilter minFilter = SamplerFilter::Nearest;
    SamplerFilter magFilter = SamplerFilter::Nearest;
    SamplerFilter mipFilter = SamplerFilter::Nearest;

    SamplerAddressMode u = SamplerAddressMode::Repeat;
    SamplerAddressMode v = SamplerAddressMode::Repeat;
    SamplerAddressMode w = SamplerAddressMode::Repeat;

    f32 lodMin  = 0.f;
    f32 lodMax  = 1e4f;
    f32 lodBias = 0.f;

    SamplerBorderColor color =
      SamplerBorderColor::FloatTransparentBlack;

    bool anisotropyEnable = false;
    f32  anisotropyMax    = 0.f;

    bool      compareEnable = false;
    CompareOp compareOp     = CompareOp::Never;
  };

public:
  VD_NONMOVABLE(Sampler);

  Sampler(Device& device, const Desc& desc);
  ~Sampler();

  View&       GetView() { return m_view; }
  const View& GetView() const { return m_view; }

private:
  Device& m_device;
  Desc    m_desc;

#ifdef VD_API_VK
public:
  struct View {
    VkSampler handle;
  };
#endif

#ifdef VD_API_DX
public:
  struct View {
    CPUDescriptorPool::Handle handle  = {};
    DescriptorBinding         binding = {};
  };
#endif

  View m_view;
};

} // namespace vd
