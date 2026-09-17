#include "vuldir/api/Device.hpp"
#include "vuldir/api/Shader.hpp"
#include "vuldir/api/vk/VkDispatcher.hpp"
#include "vuldir/api/vk/VkUti.hpp"

using namespace vd;

Shader::Shader(Device& device, std::span<char const> code):
  m_device{device},
  m_bytecode{std::begin(code), std::end(code)},
  m_handle{}
{
  if(code.empty() || code.size_bytes() % sizeof(u32) != 0u)
    throw std::invalid_argument(
      "Shader bytecode must be non-empty and 4-byte aligned in size");

  VkShaderModuleCreateInfo ci{};
  ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  ci.codeSize = std::size(m_bytecode);
  ci.pCode    = reinterpret_cast<const u32*>(std::data(m_bytecode));

  VDVkTry(m_device.api().CreateShaderModule(&ci, &m_handle));
  ++m_device.m_shaderLoadCount;
}

Shader::~Shader()
{
  if(m_handle) {
    m_device.api().DestroyShaderModule(m_handle);
    m_handle = nullptr;
  }
}
