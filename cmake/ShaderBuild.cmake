include(DXC)

function(vd_add_shader)
  cmake_parse_arguments(SHADER "" "TARGET;SOURCE;OUTPUT" "" ${ARGN})

  set(DX_SHADER_MODEL 6_5) # Use 6_2 to use the software renderer.
  set(VK_TARGET_ENV universal1.5)
  set(SHADER_STAGES vs ps cs)

  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${SHADER_OUTPUT}")
  set_source_files_properties(${SHADER_SOURCE} PROPERTIES VS_TOOL_OVERRIDE "None")
  file(READ ${SHADER_SOURCE} SHADER_CODE)

  foreach(SHADER_STAGE ${SHADER_STAGES})
    string(TOUPPER ${SHADER_STAGE} SHADER_STAGE_UPPER)
    set(SHADER_ENTRYPOINT Main${SHADER_STAGE_UPPER})
    set(SHADER_STAGE_DEFINITION VD_STAGE_${SHADER_STAGE_UPPER})

    string(FIND "${SHADER_CODE}" ${SHADER_ENTRYPOINT} SHADER_ENTRYPOINT_MATCH)
    string(FIND "${SHADER_CODE}" ${SHADER_STAGE_DEFINITION} SHADER_STAGE_MATCH)
    if(${SHADER_ENTRYPOINT_MATCH} EQUAL -1 AND ${SHADER_STAGE_MATCH} EQUAL -1)
      continue()
    endif ()

    get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME_WE)
    set(SHADER_ID "${SHADER_NAME}.${SHADER_STAGE}")

    # OUTPUT cannot use generator expressions, so build into an intermediate
    # path (unique per target and config) and copy to the final location.
    set(INT_DIR  "${SHADER_OUTPUT}")
    set(INT_PATH "${INT_DIR}/${SHADER_TARGET}-$<CONFIG>-${SHADER_ID}")
    set(OUT_DIR  "$<TARGET_FILE_DIR:${SHADER_TARGET}>/${SHADER_OUTPUT}")
    set(OUT_PATH "${OUT_DIR}/${SHADER_ID}")

    file(TIMESTAMP "${SHADER_SOURCE}" SHADER_TIMESTAMP)
      set(${SHADER_ID}-CONFIG_TIME ${SHADER_TIMESTAMP} CACHE INTERNAL "")

      message(STATUS "Configuring shader target: ${SHADER_NAME}(${SHADER_STAGE_UPPER})")
      
      set(DXC_ARGS_REL
        ${SHADER_SOURCE}
        -T ${SHADER_STAGE}_${DX_SHADER_MODEL}
        -I ${VD_SHADER_INCLUDE_DIR}
        -E ${SHADER_ENTRYPOINT}
        -D ${SHADER_STAGE_DEFINITION}=1
        -res-may-alias)

      if(VD_API STREQUAL "vk")
        list(APPEND DXC_ARGS_REL
          -spirv
          -D SPIRV
          -fspv-target-env=${VK_TARGET_ENV})
        
        set(DXC_ARGS_DBG ${DXC_ARGS_REL})
        list(APPEND DXC_ARGS_DBG
          -Fc ${INT_PATH}.spirv
          -Cc -Zi -O0)
      else()
        set(DXC_ARGS_DBG ${DXC_ARGS_REL})
        list(APPEND DXC_ARGS_DBG
          -Fd ${INT_PATH}.pdb
          -Fc ${INT_PATH}.dxil
          -Cc -Zi -O0)
      endif()

      list(JOIN DXC_ARGS_REL " " DXC_ARGS_REL_STR)
      list(JOIN DXC_ARGS_DBG " " DXC_ARGS_DBG_STR)

      # Get list of all files included by the shader.
      set(DXC_INC_CMD ${VD_DXC} ${DXC_ARGS_REL} -Fo "${CMAKE_CURRENT_BINARY_DIR}/${SHADER_OUTPUT}/${SHADER_ID}.tmp" -Vi)
      execute_process(
        COMMAND ${DXC_INC_CMD}
        RESULT_VARIABLE DXC_RESULT
        OUTPUT_VARIABLE DXC_OUTPUT
        ERROR_VARIABLE DXC_ERROR
        ERROR_STRIP_TRAILING_WHITESPACE)

      if(DXC_RESULT)
        list(JOIN DXC_INC_CMD " " DXC_INC_CMD_STR)
        message(STATUS "Failed executing command: ${DXC_INC_CMD_STR}")
        message(FATAL_ERROR "Shader configuration error: ${DXC_ERROR}")
      endif()

      # Convert dependencies to a cmake list.
      if(DXC_OUTPUT)
        string(REPLACE "\n" ";" SHADER_DEPS ${DXC_OUTPUT})
        list(FILTER SHADER_DEPS INCLUDE REGEX "Opening file")
        list(TRANSFORM SHADER_DEPS REPLACE "Opening file \\[(.+)\\], stack top.*" "\\1")
        list(TRANSFORM SHADER_DEPS STRIP)
        set(NORMALIZED_SHADER_DEPS)
        foreach(SHADER_DEP IN LISTS SHADER_DEPS)
          cmake_path(NORMAL_PATH SHADER_DEP)
          list(APPEND NORMALIZED_SHADER_DEPS "${SHADER_DEP}")
        endforeach()
        set(SHADER_DEPS ${NORMALIZED_SHADER_DEPS})
        list(REMOVE_DUPLICATES SHADER_DEPS)
        message(STATUS "Found dependencies: ${SHADER_DEPS}")
      endif()

      set(CMD_RELEASE "${VD_DXC} ${DXC_ARGS_REL_STR} -Fo ${INT_PATH}.cso")
      string(REPLACE " " ";" CMD_RELEASE ${CMD_RELEASE})
      set(CMD_DEBUG   "${VD_DXC} ${DXC_ARGS_DBG_STR} -Fo ${INT_PATH}.cso")
      string(REPLACE " " ";" CMD_DEBUG ${CMD_DEBUG})

      add_custom_command(
        OUTPUT "${INT_PATH}.cso"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${INT_DIR}"
        # Per-config command selection goes through Execute.cmake.
        COMMAND ${CMAKE_COMMAND}
          -Dconfig="$<CONFIG>"
          -Dcmd_debug="${CMD_DEBUG}"
          -Dcmd_release="${CMD_RELEASE}"
          -Dcmd_releasedebug="${CMD_RELEASE}"
          -Dcmd_profile="${CMD_RELEASE}"
          -Dcmd_asan="${CMD_DEBUG}"
          -P "${CMAKE_SOURCE_DIR}/cmake/Execute.cmake"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${OUT_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${INT_PATH}.cso" "${OUT_PATH}.cso"
        MAIN_DEPENDENCY ${SHADER_SOURCE}
        DEPENDS ${SHADER_DEPS}
        COMMAND_EXPAND_LISTS
      )
      add_custom_target(
        "ST-${SHADER_ID}"
        DEPENDS "${INT_PATH}.cso"
      )
      add_dependencies(${SHADER_TARGET} "ST-${SHADER_ID}")

  endforeach()
endfunction()
