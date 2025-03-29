#pragma once

#include "vuldir/core/Core.hpp"

#ifdef VD_API_VK

  #ifndef VK_NO_PROTOTYPES
    #define VK_NO_PROTOTYPES
  #endif

  #include "vulkan/vulkan.h"

  #define VD_API_VALUE(vd, vk, dx) vd = vk

  #define VD_API_VALUE_CONVERTER(VDTYPE, VKTYPE, DXTYPE) \
    inline constexpr VKTYPE convert(VDTYPE value)        \
    {                                                    \
      return static_cast<VKTYPE>(value);                 \
    }                                                    \
    inline constexpr VDTYPE convert(VKTYPE value)        \
    {                                                    \
      return static_cast<VDTYPE>(value);                 \
    }

  #define VD_API_OBJ(VKTYPE, DXTYPE)        \
  public:                                   \
    VKTYPE GetHandle() { return m_handle; } \
                                            \
  private:                                  \
    Device & m_device;                      \
    VKTYPE m_handle;

#endif

#ifdef VD_API_DX
  #include <d3d12.h>
  #include <dxgi1_6.h>
  #include <wrl/client.h>

  #define VD_API_VALUE(vd, vk, dx) vd = dx

  #define VD_API_VALUE_CONVERTER(VDTYPE, VKTYPE, DXTYPE) \
    inline constexpr DXTYPE convert(VDTYPE value)        \
    {                                                    \
      return static_cast<DXTYPE>(value);                 \
    }                                                    \
    inline constexpr VDTYPE convert(DXTYPE value)        \
    {                                                    \
      return static_cast<VDTYPE>(value);                 \
    }

  #define VD_API_OBJ(VKTYPE, DXTYPE)                \
  public:                                           \
    DXTYPE& GetHandle() { return *m_handle.Get(); } \
                                                    \
  private:                                          \
    Device & m_device;                              \
    ComPtr<DXTYPE> m_handle;

template<typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

#endif

#include "vuldir/api/Types.hpp"
