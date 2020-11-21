#pragma once

#include "vuldir/api/Common.hpp"

namespace vd {

class Device;

class Shader
{
public:
  VD_NONMOVABLE(Shader);

  Shader(Device& device, std::span<char const> code);
  ~Shader();

private:
  Device&   m_device;
  Arr<char> m_bytecode;

#ifdef VD_API_VK
public:
  VkShaderModule GetHandle() const { return m_handle; }

private:
  VkShaderModule m_handle;
#endif

#ifdef VD_API_DX
public:
  D3D12_SHADER_BYTECODE GetHandle() const { return m_handle; }

private:
  D3D12_SHADER_BYTECODE m_handle;
#endif
};

} // namespace vd