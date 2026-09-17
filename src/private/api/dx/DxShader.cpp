#include "vuldir/api/Device.hpp"
#include "vuldir/api/Shader.hpp"
#include "vuldir/api/dx/DxUti.hpp"

using namespace vd;

Shader::Shader(Device& device, std::span<char const> code):
  m_device{device},
  m_bytecode{std::begin(code), std::end(code)},
  m_handle{}
{
  if(code.empty() || code.size_bytes() % sizeof(u32) != 0u)
    throw std::invalid_argument(
      "Shader bytecode must be non-empty and 4-byte aligned in size");

  VD_UNUSED(m_device);

  m_handle.BytecodeLength = std::size(m_bytecode);

  m_handle.pShaderBytecode =
    reinterpret_cast<const void*>(std::data(m_bytecode));
  ++m_device.m_shaderLoadCount;
}

Shader::~Shader() {}
