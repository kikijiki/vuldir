string(TOLOWER "${config}" config)
set(cmd "${cmd_${config}}")

separate_arguments(cmd)

execute_process(COMMAND ${cmd})
