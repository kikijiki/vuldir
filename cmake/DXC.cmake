include(FetchContent)

# The Linux dxc binary might need "ncurses5-compat-libs".

# Cross-compiling Windows targets from Linux: compile shaders with the host dxc.
if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_NAME STREQUAL "Windows")
  find_program(VD_DXC_SYSTEM NAMES dxc)
  if(VD_DXC_SYSTEM)
    set(VD_DXC "${VD_DXC_SYSTEM}")
  else()
    set(VD_DXC "${CMAKE_SOURCE_DIR}/_cache/dxc/Linux/bin/dxc")
  endif()

  if(NOT EXISTS "${VD_DXC}")
    unset(VD_DXC)
    FetchContent_Declare(
      DXC
      URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2502/linux_dxc_2025_02_20.x86_64.tar.gz
      SOURCE_DIR "${CMAKE_SOURCE_DIR}/_cache/dxc/Linux/"
      DOWNLOAD_EXTRACT_TIMESTAMP OFF)
    FetchContent_MakeAvailable(DXC)
    set(VD_DXC "${dxc_SOURCE_DIR}/bin/dxc")
  endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  # Try local
  set(VD_DXC "${CMAKE_SOURCE_DIR}/_cache/dxc/${CMAKE_SYSTEM_NAME}/bin/x64/dxc.exe")

  # Try Vulkan SDK
  if(NOT EXISTS ${VD_DXC} AND VD_API STREQUAL "vk" AND DEFINED Vulkan_dxc_EXECUTABLE)
    set(VD_DXC "${Vulkan_dxc_EXECUTABLE}")
  endif()

  # Download and cache locally.
  if(NOT EXISTS ${VD_DXC})
    unset(VD_DXC)
    FetchContent_Declare(
      DXC
      URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2502/dxc_2025_02_20.zip
      SOURCE_DIR "${CMAKE_SOURCE_DIR}/_cache/dxc/${CMAKE_SYSTEM_NAME}/"
      DOWNLOAD_EXTRACT_TIMESTAMP OFF)
    FetchContent_MakeAvailable(DXC)

    set(VD_DXC "${dxc_SOURCE_DIR}/bin/x64/dxc.exe")
  endif()
else()
  # Prefer a system dxc: prebuilt binaries often fail on NixOS.
  find_program(VD_DXC_SYSTEM NAMES dxc)
  if(VD_DXC_SYSTEM)
    set(VD_DXC "${VD_DXC_SYSTEM}")
  else()
    set(VD_DXC "${CMAKE_SOURCE_DIR}/_cache/dxc/${CMAKE_SYSTEM_NAME}/bin/dxc")
  endif()

  # Download and cache locally.
  if(NOT EXISTS "${VD_DXC}")
    unset(VD_DXC)
    FetchContent_Declare(
      DXC
      URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2502/linux_dxc_2025_02_20.x86_64.tar.gz
      SOURCE_DIR "${CMAKE_SOURCE_DIR}/_cache/dxc/${CMAKE_SYSTEM_NAME}/"
      DOWNLOAD_EXTRACT_TIMESTAMP OFF)
    FetchContent_MakeAvailable(DXC)
    set(VD_DXC "${dxc_SOURCE_DIR}/bin/dxc")
  endif()
endif()


if(NOT EXISTS ${VD_DXC})
  message(FATAL_ERROR "Could not find or fetch DXC!")
else()
  message(STATUS "DXC is in ${VD_DXC}")
endif()
