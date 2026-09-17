cmake_minimum_required(VERSION 3.23.0 FATAL_ERROR)
project (vuldir LANGUAGES CXX C)

include(FetchContent)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CMAKE_DISABLE_SOURCE_CHANGES  ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)

set(
  CMAKE_CONFIGURATION_TYPES
  "Debug;Release;ReleaseDebug;Profile;ASAN"
  CACHE STRING "Configuration types" FORCE)

set(CMAKE_EXE_LINKER_FLAGS_RELEASEDEBUG ${CMAKE_EXE_LINKER_FLAGS_RELEASE})
set(CMAKE_EXE_LINKER_FLAGS_PROFILE      ${CMAKE_EXE_LINKER_FLAGS_RELEASE})
set(CMAKE_CXX_FLAGS_RELEASEDEBUG        ${CMAKE_CXX_FLAGS_RELEASE})
set(CMAKE_EXE_LINKER_FLAGS_ASAN         ${CMAKE_EXE_LINKER_FLAGS_RELEASE})
set(CMAKE_CXX_FLAGS_PROFILE             ${CMAKE_CXX_FLAGS_RELEASE})
set(CMAKE_CXX_FLAGS_ASAN                ${CMAKE_CXX_FLAGS_RELEASE})

set(TIDY_CHECKS
  clang-diagnostic-*
  clang-analyzer-*
#  cppcoreguidelines-*
#  modernize-*
  performance-*
#  readability-*
  bugprone-*
  # High-volume noise in a renderer (u32/float, x/y args, packed GPU enums).
  -bugprone-easily-swappable-parameters
  -bugprone-narrowing-conversions
  -bugprone-implicit-widening-of-multiplication-result
  -performance-enum-size
)
list(JOIN TIDY_CHECKS "," TIDY_CHECKS_ARG)

# Per-target only. A global CMAKE_CXX_CLANG_TIDY leaks into FetchContent
# deps (Catch2) and dumps their diagnostics on every `just run` build.
if(NOT MINGW AND NOT CMAKE_CROSSCOMPILING)
  set(VD_CLANG_TIDY
    clang-tidy
    "--format-style=file"
    "--checks=${TIDY_CHECKS_ARG}")
endif()

function(vd_enable_clang_tidy target)
  if(NOT VD_CLANG_TIDY)
    return()
  endif()
  set(_tidy ${VD_CLANG_TIDY})
  # header-filter is required for exclude-header-filter. Limit it to tests/
  # and treat Catch2 as -isystem so REQUIRE() expansions are not diagnosed.
  if(DEFINED catch2_SOURCE_DIR)
    list(APPEND _tidy
      "--header-filter=vuldir/tests/"
      "--exclude-header-filter=_deps/|catch2/"
      "--extra-arg=-isystem${catch2_SOURCE_DIR}/src")
  endif()
  set_property(TARGET ${target} PROPERTY CXX_CLANG_TIDY ${_tidy})
endfunction()

# Options
set(VD_API "vk" CACHE STRING "API to target")
set_property(CACHE VD_API PROPERTY STRINGS vk dx)

# Linux window system. Use xcb for X11/Xwayland or CI xvfb.
set(VD_WINDOW_SYSTEM "wayland" CACHE STRING "Linux window system (wayland|xcb)")
set_property(CACHE VD_WINDOW_SYSTEM PROPERTY STRINGS wayland xcb)

option(VD_USE_VULKAN_SDK "Use the Vulkan SDK instead of building from source" ON)
option(VD_BUILD_TESTS "Build the vuldir test suite (Catch2; Linux + Vulkan only)" ON)

set(VD_ROOT_DIR  ${CMAKE_CURRENT_SOURCE_DIR})
set(VD_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR})

set(VD_PRIVATE_DEFINITIONS _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING _CRT_SECURE_NO_WARNINGS)
set(VD_PUBLIC_DEFINITIONS "")

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
  message(STATUS "Using CCACHE")
  set_property(GLOBAL PROPERTY CMAKE_C_COMPILER_LAUNCHER ccache)
  set_property(GLOBAL PROPERTY CMAKE_CXX_COMPILER_LAUNCHER ccache)
endif(CCACHE_FOUND)

include(ShaderBuild)

message(STATUS "OS is ${CMAKE_SYSTEM_NAME}")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  list(APPEND VD_PUBLIC_DEFINITIONS VD_OS_WINDOWS VK_USE_PLATFORM_WIN32_KHR)
  list(APPEND VD_PRIVATE_DEFINITIONS NOMINMAX WIN32_LEAN_AND_MEAN VC_EXTRALEAN)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND VD_PUBLIC_DEFINITIONS VD_OS_LINUX)
  if(VD_WINDOW_SYSTEM STREQUAL "wayland")
    list(APPEND VD_PUBLIC_DEFINITIONS VD_WINDOW_WAYLAND VK_USE_PLATFORM_WAYLAND_KHR)
  elseif(VD_WINDOW_SYSTEM STREQUAL "xcb")
    list(APPEND VD_PUBLIC_DEFINITIONS VD_WINDOW_XCB VK_USE_PLATFORM_XCB_KHR)
  else()
    message(FATAL_ERROR "VD_WINDOW_SYSTEM must be wayland or xcb (got '${VD_WINDOW_SYSTEM}')")
  endif()
  message(STATUS "Window system is ${VD_WINDOW_SYSTEM}")
else()
  message(FATAL_ERROR "Unsupported OS")
endif()

message(STATUS "API is ${VD_API}")
if(VD_API STREQUAL "vk")
  list(APPEND VD_PUBLIC_DEFINITIONS VD_API_VK VK_NO_PROTOTYPES)
  if(VD_USE_VULKAN_SDK)
    find_package(Vulkan)
  else()
    FetchContent_Declare(
      VulkanHeaders
      GIT_REPOSITORY "https://github.com/KhronosGroup/Vulkan-Headers.git"
      GIT_TAG        "v1.4.309"
      DOWNLOAD_EXTRACT_TIMESTAMP OFF)
    FetchContent_MakeAvailable(VulkanHeaders FETCHCONTENT_UPDATES_DISCONNECTED)
    
    FetchContent_Declare(
      VulkanValidationLayers
      GIT_REPOSITORY "https://github.com/KhronosGroup/Vulkan-ValidationLayers"
      GIT_TAG        "v1.4.309"
      DOWNLOAD_EXTRACT_TIMESTAMP OFF)
    
    FetchContent_Populate(VulkanValidationLayers FETCHCONTENT_UPDATES_DISCONNECTED)
    file(MAKE_DIRECTORY ${vulkanvalidationlayers_SOURCE_DIR}/build)

    execute_process(
      WORKING_DIRECTORY ${vulkanvalidationlayers_SOURCE_DIR}/build
      COMMAND ${CMAKE_COMMAND}
        -C ../external/helper.cmake
        -DCMAKE_BUILD_TYPE=Release
        -DUPDATE_DEPS=ON
        -DUSE_CCACHE=OFF
        ..)
    execute_process(
      WORKING_DIRECTORY ${vulkanvalidationlayers_SOURCE_DIR}/build
      COMMAND ${CMAKE_COMMAND}
        --build .
        --config Release)
  endif()
elseif(VD_API STREQUAL "dx")
  list(APPEND VD_PUBLIC_DEFINITIONS VD_API_DX)
else()
  message(FATAL_ERROR "Unsupported API")
endif()

set(VD_COMPILE_OPTIONS "")
set(VD_LINK_OPTIONS "")

# Microsoft Visual C++ (msvc or clang)
if((CMAKE_CXX_COMPILER_ID MATCHES "MSVC") OR MSVC)
  list(APPEND VD_COMPILE_OPTIONS
    /wd4996 /nologo /Zm200 /MP /W4 /WX /GR-
    $<$<CONFIG:Debug>:        /Zi /MDd /Od>
    $<$<CONFIG:ReleaseDebug>: /Zi /MD  /O2 /Ob2>
    $<$<CONFIG:Release>:          /MD  /O2 /Ob2 /GL>
    $<$<CONFIG:Profile>:      /Zi /MD  /O2 /Ob2 /GL> # TODO
    $<$<CONFIG:Asan>:         /Zi /MD  /O1 /Ob2 /fsanitize=address>
  )
  list(APPEND VD_LINK_OPTIONS
    $<$<CONFIG:Debug>:        /DEBUG>
    $<$<CONFIG:ReleaseDebug>: /DEBUG>
    $<$<CONFIG:Release>:>
    $<$<CONFIG:Profile>:      /DEBUG>
    $<$<CONFIG:Asan>:         /DEBUG /fsanitize=address>
  )
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  list(APPEND VD_COMPILE_OPTIONS
    -Wall -Wextra -Werror
    -Wno-missing-field-initializers
    -Wno-format-security
    -Wno-comment
    -Wno-unused-variable
    # Math.hpp's Float16/Float32 bit casts trigger this under GCC -O2.
    -Wno-strict-aliasing
    # False positives under GCC -O2 (e.g. std::max/min on locals set in a
    # switch).
    -Wno-maybe-uninitialized
    $<$<CONFIG:Debug>:        -g>
    $<$<CONFIG:ReleaseDebug>: -g -O2>
    $<$<CONFIG:Release>:      -O2>
    $<$<CONFIG:Profile>:      -g -O2>
    $<$<CONFIG:Asan>:         -g -O1 -fsanitize=address -fno-omit-frame-pointer>)
  list(APPEND VD_LINK_OPTIONS
    $<$<CONFIG:Asan>: -fsanitize=address>)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  list(APPEND VD_COMPILE_OPTIONS
    -Wall -Wextra -Werror -Wno-missing-braces
    -Wno-c++98-compat -Wno-c++98-compat-pedantic
    -Wno-old-style-cast -Wno-extra-semi
    -Wno-extra-semi-stmt -Wno-switch-enum
    -Wno-format-nonliteral -Wno-format-security
    -Wno-covered-switch-default -Wno-ctad-maybe-unsupported
    -Wno-language-extension-token
    -Wno-missing-field-initializers
    -Wno-enum-conversion)

  if(NOT MSVC)
    list(APPEND VD_COMPILE_OPTIONS
      $<$<CONFIG:Debug>:        -g>
      $<$<CONFIG:ReleaseDebug>: -g -O2>
      $<$<CONFIG:Release>:      -O2>
      $<$<CONFIG:Profile>:      -g -O2> # TODO
      $<$<CONFIG:Asan>:         -g -O1 -fsanitize=address -fno-omit-frame-pointer>)
    
    list(APPEND VD_LINK_OPTIONS
      $<$<CONFIG:Debug>:        >
      $<$<CONFIG:ReleaseDebug>: >
      $<$<CONFIG:Release>:      >
      $<$<CONFIG:Profile>:      >
      $<$<CONFIG:Asan>:         -fsanitize=address>)
  endif()
endif()

file(GLOB_RECURSE VD_SRC_CORE_PUBLIC  src/public/vuldir/core/*.*)
file(GLOB_RECURSE VD_SRC_CORE_PRIVATE src/private/core/*.*)

file(GLOB_RECURSE VD_SRC_API_IMPL_PUBLIC  src/public/vuldir/api/${VD_API}/*.*)
file(GLOB_RECURSE VD_SRC_API_IMPL_PRIVATE src/private/api/${VD_API}/*.*)

file(GLOB VD_SRC_API_PUBLIC  src/public/vuldir/api/*.*)
file(GLOB VD_SRC_API_PRIVATE src/private/api/*.*)

file(GLOB VD_SRC_MAIN_PUBLIC  src/public/vuldir/*.*)
file(GLOB VD_SRC_MAIN_PRIVATE src/private/*.*)

set(VD_SRC_PUBLIC
  ${VD_SRC_CORE_PUBLIC}
  ${VD_SRC_API_PUBLIC}
  ${VD_SRC_API_IMPL_PUBLIC}
  ${VD_SRC_MAIN_PUBLIC}
  ${VD_SRC_NATVIS})

set(VD_SRC_PRIVATE
  ${VD_SRC_CORE_PRIVATE}
  ${VD_SRC_API_PRIVATE}
  ${VD_SRC_API_IMPL_PRIVATE}
  ${VD_SRC_MAIN_PRIVATE})
  
set(VD_SRC_ALL ${VD_SRC_PUBLIC} ${VD_SRC_PRIVATE})
set(VD_SHADER_INCLUDE_DIR ${VD_ROOT_DIR}/src/shaders)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src FILES ${VD_SRC_ALL})

set(VD_SRC_NATVIS "")
if(MSVC)
  file(GLOB VD_SRC_NATVIS *.natvis)
  set_source_files_properties(${SRC_NATVIS} PROPERTIES HEADER_FILE_ONLY TRUE VS_TOOL_OVERRIDE "NATVIS")
endif()

add_library(vuldir)
target_sources(vuldir PUBLIC ${VD_SRC_PUBLIC})
target_sources(vuldir PRIVATE ${VD_SRC_PRIVATE})
target_include_directories(vuldir PUBLIC  src/public)
target_include_directories(vuldir PRIVATE src/private)
target_compile_options(vuldir PRIVATE ${VD_COMPILE_OPTIONS})
target_compile_definitions(vuldir
  PUBLIC  ${VD_PUBLIC_DEFINITIONS}
  PRIVATE ${VD_PRIVATE_DEFINITIONS})
set_target_properties(vuldir PROPERTIES
  PREFIX lib
  DEBUG_POSTFIX -d
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
vd_enable_clang_tidy(vuldir)

if(VD_API STREQUAL "vk")
  if(VD_USE_VULKAN_SDK)
    target_link_libraries(vuldir Vulkan::Vulkan)
  else()
    target_link_libraries(vuldir Vulkan::Headers)
  endif()
elseif(VD_API STREQUAL "dx")
  if(MINGW)
    target_link_libraries(vuldir
      d3d12 dxgi dxguid d3dcompiler DirectX-Guids)
  else()
    target_link_libraries(vuldir dxgi.lib dxguid.lib d3d12.lib d3dcompiler.lib)
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(vuldir ${CMAKE_DL_LIBS})
  if(VD_WINDOW_SYSTEM STREQUAL "wayland")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
    target_link_libraries(vuldir PkgConfig::WAYLAND_CLIENT)
  else()
    target_link_libraries(vuldir xcb xcb-icccm xcb-ewmh)
  endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND MINGW)
  target_link_libraries(vuldir uuid ole32)
endif()

if(NOT MINGW AND NOT CMAKE_CROSSCOMPILING)
  file(GLOB_RECURSE VD_FORMAT_SAMPLE
    sample/src/*.cpp sample/src/*.hpp sample/shaders/*.hlsl)
  add_custom_target(format
    COMMAND clang-format -i -style=file ${VD_SRC_PUBLIC} ${VD_SRC_PRIVATE}
      ${VD_FORMAT_SAMPLE}
    COMMENT "Formatting vuldir sources"
  )
endif()

add_subdirectory(sample)

if(VD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
