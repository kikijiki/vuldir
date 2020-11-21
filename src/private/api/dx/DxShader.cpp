#include "vuldir/api/Device.hpp"
#include "vuldir/api/Shader.hpp"
#include "vuldir/api/dx/DxUti.hpp"

using namespace vd;

Shader::Shader(Device& device, std::span<char const> code):
  m_device{device},
  m_bytecode{std::begin(code), std::end(code)},
  m_handle{}
{
  VD_UNUSED(m_device);

  m_handle.BytecodeLength = std::size(m_bytecode);

  m_handle.pShaderBytecode =
    reinterpret_cast<const void*>(std::data(m_bytecode));
}

Shader::~Shader() {}