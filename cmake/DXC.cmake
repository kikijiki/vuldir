include(FetchContent)

# Might need "ncurses5-compat-libs"

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  # Try local
  set(VD_DXC "${CMAKE_SOURCE_DIR}/_cache/dxc/${CMAKE_SYSTEM_NAME}/bin/x64/dxc.exe")

  # Try Windows SDK
  # Too old?
  #if(NOT EXISTS ${VD_DXC})
  #  unset(VD_DXC)
  #  file(TO_CMAKE_PATH "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/" WIN10_SDK_PATH)
  #  find_program(VD_DXC NAMES dxc.exe HINTS "${WIN10_SDK_PATH}/bin/10.0.22621.0" "${WIN10_SDK_PATH}/bin/10.0.22000.0" "${WIN10_SDK_PATH}/bin/*" PATH_SUFFIXES x64)
  #endif()

  # Try Vulkan SDK
  if(NOT EXISTS ${VD_DXC} AND VD_API STREQUAL "vk" AND DEFINED Vulkan_dxc_EXECUTABLE)
    set(VD_DXC "${Vulkan_dxc_EXECUTABLE}")
  endif()

  # Get latest build and save in local cache
  if(NOT EXISTS ${VD_DXC})
    unset(VD_DXC)
    FetchContent_Declare(
      DXC
      URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2403.2/dxc_2024_03_29.zip
      #URL https://ci.appveyor.com/api/projects/dnovillo/directxshadercompiler/artifacts/build%2FRelease%2Fdxc-artifacts.zip?branch=main&pr=false&job=image%3A%20Visual%20Studio%202022
      SOURCE_DIR "${CMAKE_SOURCE_DIR}/_cache/dxc/${CMAKE_SYSTEM_NAME}/"
      DOWNLOAD_EXTRACT_TIMESTAMP OFF)
    FetchContent_MakeAvailable(DXC)

    set(VD_DXC "${dxc_SOURCE_DIR}/bin/x64/dxc.exe")
  endif()
else()
  # Try local
  set(VD_DXC "${CMAKE_SOURCE_DIR}/_cache/dxc/${CMAKE_SYSTEM_NAME}/bin/dxc")

  # Get latest build and save in local cache
  if(NOT EXISTS ${VD_DXC})
    FetchContent_Declare(
      DXC
      #URL https://ci.appveyor.com/api/projects/dnovillo/directxshadercompiler/artifacts/build%2Fdxc-artifacts.tar.gz?branch=main&pr=false&job=image%3A%20Ubuntu
      URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2403.2/linux_dxc_2024_03_29.x86_64.tar.gz
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
