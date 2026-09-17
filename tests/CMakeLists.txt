# Catch2 v3 test suite. Linux + Vulkan only: the tests need a real Device and
# Swapchain, and the DX12 backend is Windows-only.

if(NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND VD_API STREQUAL "vk"))
  message(
    STATUS
    "VD_BUILD_TESTS: skipping (requires Linux + VD_API=vk, got "
    "${CMAKE_SYSTEM_NAME}/${VD_API})")
  return()
endif()

message(STATUS "Fetching Catch2 for the vuldir test suite")
# Do not run clang-tidy on Catch2 (same as sample/RmlUi).
set(_vd_tests_clang_tidy "${CMAKE_CXX_CLANG_TIDY}")
set(CMAKE_CXX_CLANG_TIDY "")
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY "https://github.com/catchorg/Catch2.git"
  GIT_TAG        "v3.9.1"
  DOWNLOAD_EXTRACT_TIMESTAMP OFF)
FetchContent_MakeAvailable(Catch2)
set(CMAKE_CXX_CLANG_TIDY "${_vd_tests_clang_tidy}")
foreach(_catch_tgt Catch2 Catch2WithMain)
  if(TARGET ${_catch_tgt})
    set_property(TARGET ${_catch_tgt} PROPERTY CXX_CLANG_TIDY "")
    # Expose Catch2 as -isystem so clang-tidy skips REQUIRE() expansions.
    get_target_property(_catch_inc ${_catch_tgt} INTERFACE_INCLUDE_DIRECTORIES)
    if(_catch_inc)
      set_property(
        TARGET ${_catch_tgt}
        APPEND PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_catch_inc}")
    endif()
  endif()
endforeach()

add_executable(vuldir_tests_core
  unit/CoreMathTests.cpp
  unit/IblMathTests.cpp
  unit/InputTests.cpp
  unit/ModelReaderTests.cpp)
target_include_directories(vuldir_tests_core PRIVATE
  ${CMAKE_SOURCE_DIR}/sample/src)
target_link_libraries(vuldir_tests_core PRIVATE vuldir Catch2::Catch2WithMain)
target_compile_options(vuldir_tests_core PRIVATE ${VD_COMPILE_OPTIONS})
target_link_options(vuldir_tests_core PRIVATE ${VD_LINK_OPTIONS})
set_target_properties(vuldir_tests_core PROPERTIES
  DEBUG_POSTFIX -d
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
vd_enable_clang_tidy(vuldir_tests_core)
add_test(NAME vuldir_tests_core COMMAND vuldir_tests_core --reporter console
  --allow-running-no-tests)

# Shared GPU/window fixture.
add_library(vuldir_test_support STATIC
  support/TestEnv.hpp
  support/TestEnv.cpp
  support/TestWindow.hpp
  support/TestWindow.cpp
  support/TestMain.cpp)
target_include_directories(vuldir_test_support PUBLIC support)
target_link_libraries(vuldir_test_support PUBLIC vuldir Catch2::Catch2)
if(VD_WINDOW_SYSTEM STREQUAL "wayland")
  include(WaylandProtocols)
  vd_add_wayland_protocols(vuldir_test_support)
endif()
target_compile_options(vuldir_test_support PRIVATE ${VD_COMPILE_OPTIONS})
set_target_properties(vuldir_test_support PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
vd_enable_clang_tidy(vuldir_test_support)

function(vd_add_test_exe NAME)
  cmake_parse_arguments(VDT "" "" "SOURCES" ${ARGN})

  add_executable(${NAME} ${VDT_SOURCES})
  target_link_libraries(${NAME} PRIVATE
    vuldir_test_support
    Catch2::Catch2)
  target_compile_options(${NAME} PRIVATE ${VD_COMPILE_OPTIONS})
  target_link_options(${NAME} PRIVATE ${VD_LINK_OPTIONS})
  set_target_properties(${NAME} PROPERTIES
    DEBUG_POSTFIX -d
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
  vd_enable_clang_tidy(${NAME})

  # --allow-running-no-tests: Catch2 exits non-zero if every case SKIP()s
  # (no display); GPU binaries should not fail CTest in that case.
  add_test(NAME ${NAME} COMMAND ${NAME} --reporter console
    --allow-running-no-tests)
  set_tests_properties(${NAME} PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp"
    RUN_SERIAL TRUE) # One shared Device per binary.
endfunction()

# Unit tests: CommandBuffer state machine (needs a Device, no swapchain).

vd_add_test_exe(vuldir_tests_unit SOURCES
  unit/CommandBufferStateTests.cpp)

# Integration tests: RenderContext frame loops, concurrent contexts, Binder
# deferred reclaim.

vd_add_test_exe(vuldir_tests_integration SOURCES
  integration/RenderContextFrameLoopTests.cpp
  integration/ConcurrentRenderContextTests.cpp
  integration/BinderDeferredReclaimTests.cpp)
