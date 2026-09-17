###############################################################################
# RmlUi (sample-scoped). Pin a known release; soft-raster overlay path does not
# need RmlUi's official GPU backends.
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
set(RMLUI_SVG_PLUGIN OFF CACHE BOOL "" FORCE)
set(RMLUI_LOTTIE_PLUGIN OFF CACHE BOOL "" FORCE)
set(RMLUI_COMPILER_OPTIONS OFF CACHE BOOL "" FORCE)
set(RMLUI_INSTALL_RUNTIME_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS ON CACHE BOOL "" FORCE)
set(RMLUI_THIRDPARTY_CONTAINERS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# A nested `nix develop .#dx12` can retain host search paths. Pin RmlUi's
# FreeType lookup to the MinGW package exported by the DX12 shell.
if(MINGW AND DEFINED ENV{VD_MINGW_FREETYPE_LIBRARY})
  set(FREETYPE_INCLUDE_DIR_freetype2
    "$ENV{VD_MINGW_FREETYPE_INCLUDE}" CACHE PATH "" FORCE)
  set(FREETYPE_INCLUDE_DIR_ft2build
    "$ENV{VD_MINGW_FREETYPE_INCLUDE}" CACHE PATH "" FORCE)
  set(FREETYPE_LIBRARY_RELEASE
    "$ENV{VD_MINGW_FREETYPE_LIBRARY}" CACHE FILEPATH "" FORCE)
endif()

# Prefer a system package, but keep the sample buildable on clean CI runners
# and native Windows installations. The sample only loads TrueType fonts, so
# the optional compression and shaping dependencies are not needed here.
find_package(Freetype QUIET)
if(NOT TARGET Freetype::Freetype)
  set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
  set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
  FetchContent_Declare(
    Freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG        VER-2-13-3
    GIT_SHALLOW    TRUE
    DOWNLOAD_EXTRACT_TIMESTAMP OFF)
  FetchContent_MakeAvailable(Freetype)
  add_library(Freetype::Freetype ALIAS freetype)
endif()

FetchContent_Declare(
  RmlUi
  GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
  GIT_TAG        6.1
  GIT_SHALLOW    TRUE
  DOWNLOAD_EXTRACT_TIMESTAMP OFF)

set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_INSTALL OFF CACHE BOOL "" FORCE)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(NFD_PORTAL ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(
  NativeFileDialog
  GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
  GIT_TAG        v1.3.0
  GIT_SHALLOW    TRUE
  DOWNLOAD_EXTRACT_TIMESTAMP OFF)

# Root sets CMAKE_CXX_CLANG_TIDY; keep it off for third-party RmlUi.
set(_vd_sample_clang_tidy "${CMAKE_CXX_CLANG_TIDY}")
set(CMAKE_CXX_CLANG_TIDY "")
FetchContent_MakeAvailable(RmlUi NativeFileDialog)
set(CMAKE_CXX_CLANG_TIDY "${_vd_sample_clang_tidy}")

# RmlUi links FreeType privately from a static archive. Append its dependency
# libraries to RmlUi itself so they follow FreeType in the final link order.
if(MINGW AND DEFINED ENV{VD_MINGW_FREETYPE_DEPS})
  target_link_libraries(rmlui_core INTERFACE $ENV{VD_MINGW_FREETYPE_DEPS})
endif()

###############################################################################

file(GLOB_RECURSE VD_SRC_SAMPLE src/*.cpp src/*.hpp)

file(GLOB_RECURSE VD_SRC_SAMPLE_SHADERS shaders/*.hlsl)
source_group(Shaders FILES ${VD_SRC_SAMPLE_SHADERS})

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  add_executable(sample ${VD_SRC_SAMPLE} ${VD_SRC_SAMPLE_SHADERS})
else()
  add_executable(sample ${VD_SRC_SAMPLE} ${VD_SRC_SAMPLE_SHADERS})
endif()

set_target_properties(sample PROPERTIES
  DEBUG_POSTFIX -d
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
vd_enable_clang_tidy(sample)
target_link_libraries(sample PRIVATE vuldir RmlUi::Core nfd)
if(WIN32)
  target_link_libraries(sample PRIVATE psapi)
endif()
if(TARGET RmlUi::Debugger)
  target_link_libraries(sample PRIVATE RmlUi::Debugger)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  if(VD_WINDOW_SYSTEM STREQUAL "wayland")
    include(WaylandProtocols)
    vd_add_wayland_protocols(sample)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
    target_link_libraries(sample PRIVATE PkgConfig::XKBCOMMON)
  else()
    target_link_libraries(sample PRIVATE xcb-keysyms)
  endif()
endif()
# Treat RmlUi headers as system includes so -Werror on the sample does not fail
# on third-party diagnostics.
get_target_property(_rml_inc RmlUi::Core INTERFACE_INCLUDE_DIRECTORIES)
if(_rml_inc)
  target_include_directories(sample SYSTEM PRIVATE ${_rml_inc})
endif()
if(MINGW)
  target_link_libraries(sample PRIVATE user32 gdi32 shell32 ole32 uuid mcfgthread)
  if(DEFINED ENV{VD_MINGW_FREETYPE_DLLS})
    set(_vd_freetype_dlls "$ENV{VD_MINGW_FREETYPE_DLLS}")
    foreach(_freetype_dll IN LISTS _vd_freetype_dlls)
      add_custom_command(TARGET sample POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${_freetype_dll}" "$<TARGET_FILE_DIR:sample>"
        COMMENT "Copying MinGW FreeType runtime dependency")
    endforeach()
  endif()
  target_link_options(sample PRIVATE -static-libgcc -static-libstdc++)

  # mcfgthread is linked dynamically; bundle the DLL next to the PE for Wine runs.
  if(DEFINED ENV{MCFGTHREAD_DLL} AND EXISTS "$ENV{MCFGTHREAD_DLL}")
    add_custom_command(TARGET sample POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$ENV{MCFGTHREAD_DLL}" "$<TARGET_FILE_DIR:sample>"
      COMMENT "Copying libmcfgthread-2.dll for Wine")
  endif()
endif()
target_compile_options(sample PRIVATE ${VD_COMPILE_OPTIONS})
target_link_options(sample PRIVATE ${VD_LINK_OPTIONS})
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT sample)
set_property(TARGET sample PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:sample>)

foreach(SHADER ${VD_SRC_SAMPLE_SHADERS})
  vd_add_shader(TARGET sample SOURCE ${SHADER} OUTPUT "Shaders")
endforeach()

# UI assets resolve relative to the sample binary working directory (see just run-vk).
# Font is copied from the pinned RmlUi tree (OFL) so the panel has usable text.
add_custom_command(TARGET sample POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:sample>/ui"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/ui"
    "$<TARGET_FILE_DIR:sample>/ui"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Regular.ttf"
    "$<TARGET_FILE_DIR:sample>/ui/LatoLatin-Regular.ttf"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
    "$<TARGET_FILE_DIR:sample>/smaa"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/smaa/AreaTexRGBA.bin"
    "$<TARGET_FILE_DIR:sample>/smaa/AreaTexRGBA.bin"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/smaa/SearchTexR.bin"
    "$<TARGET_FILE_DIR:sample>/smaa/SearchTexR.bin"
  COMMENT "Copying sample UI and SMAA assets next to binary")
