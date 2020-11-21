string(TOLOWER "${config}" config)
set(cmd "${cmd_${config}}")

separate_arguments(cmd)

if(0)
  message(STATUS "EXECUTE ${cmd}")
  execute_process(
    COMMAND ${cmd}
    RESULT_VARIABLE CMD_RESULT
    OUTPUT_VARIABLE CMD_OUTPUT
    ERROR_VARIABLE  CMD_ERROR)
  message(STATUS "RESULT: ${CMD_RESULT}")
  message(STATUS "OUTPUT: ${CMD_OUTPUT}")
  message(STATUS "ERROR: ${CMD_ERROR}")
else()
  execute_process(COMMAND ${cmd})
endif()
