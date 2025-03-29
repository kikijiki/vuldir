file(GLOB_RECURSE VD_SRC_SAMPLE src/*.cpp src/*.hpp)

file(GLOB_RECURSE VD_SRC_SAMPLE_SHADERS shaders/*.hlsl)
source_group(Shaders FILES ${VD_SRC_SAMPLE_SHADERS})

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  add_executable(sample ${VD_SRC_SAMPLE} ${VD_SRC_SAMPLE_SHADERS})
  #add_executable(sample WIN32 ${VD_SRC_SAMPLE} ${VD_SRC_SAMPLE_SHADERS})
else()
  add_executable(sample ${VD_SRC_SAMPLE} ${VD_SRC_SAMPLE_SHADERS})
endif()

set_target_properties(sample PROPERTIES
  DEBUG_POSTFIX -d
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CMAKE_CXX_EXTENSIONS OFF)
target_link_libraries(sample PRIVATE vuldir)
target_compile_options(sample PRIVATE ${VD_COMPILE_OPTIONS})
target_link_options(sample PRIVATE ${VD_LINK_OPTIONS})
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT sample)
set_property(TARGET sample PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:sample>)

foreach(SHADER ${VD_SRC_SAMPLE_SHADERS})
  vd_add_shader(TARGET sample SOURCE ${SHADER} OUTPUT "Shaders")
endforeach()

add_custom_command(TARGET sample PRE_BUILD
  COMMAND clang-format -i -style=file ${VD_SRC_SAMPLE} ${VD_SRC_SAMPLE_SHADERS}
)
