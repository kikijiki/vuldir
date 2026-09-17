# Generate xdg-shell client sources once and link them into the given target.
function(vd_add_wayland_protocols target)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)

  if(NOT TARGET vd_wayland_protocols)
    pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
    pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
    if(NOT WAYLAND_PROTOCOLS_DIR)
      message(FATAL_ERROR "wayland-protocols pkgdatadir not found (pkg-config)")
    endif()

    find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

    set(_xml "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
    if(NOT EXISTS "${_xml}")
      message(FATAL_ERROR "xdg-shell.xml not found at ${_xml}")
    endif()

    set(_gen_dir "${CMAKE_BINARY_DIR}/generated/wayland")
    file(MAKE_DIRECTORY "${_gen_dir}")
    set(_xdg_h "${_gen_dir}/xdg-shell-client-protocol.h")
    set(_xdg_c "${_gen_dir}/xdg-shell-protocol.c")

    add_custom_command(
      OUTPUT "${_xdg_h}"
      COMMAND "${WAYLAND_SCANNER}" client-header "${_xml}" "${_xdg_h}"
      DEPENDS "${_xml}"
      VERBATIM)
    add_custom_command(
      OUTPUT "${_xdg_c}"
      COMMAND "${WAYLAND_SCANNER}" private-code "${_xml}" "${_xdg_c}"
      DEPENDS "${_xml}"
      VERBATIM)

    add_library(vd_wayland_protocols STATIC "${_xdg_c}" "${_xdg_h}")
    target_include_directories(vd_wayland_protocols PUBLIC "${_gen_dir}")
    target_link_libraries(vd_wayland_protocols PUBLIC PkgConfig::WAYLAND_CLIENT)
    set_target_properties(vd_wayland_protocols PROPERTIES
      C_STANDARD 99
      POSITION_INDEPENDENT_CODE ON)
  endif()

  target_link_libraries(${target} PRIVATE vd_wayland_protocols)
endfunction()
