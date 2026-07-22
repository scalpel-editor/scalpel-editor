# Generate only the stable xdg-shell client contract needed by the phase 6
# toplevel. Later protocols stay out of this target until their roadmap steps.
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(WAYLAND_EGL REQUIRED IMPORTED_TARGET wayland-egl)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)

find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner REQUIRED)
pkg_get_variable(WAYLAND_PROTOCOLS_DATA_DIR wayland-protocols pkgdatadir)
set(XDG_SHELL_PROTOCOL "${WAYLAND_PROTOCOLS_DATA_DIR}/stable/xdg-shell/xdg-shell.xml")
if(NOT EXISTS "${XDG_SHELL_PROTOCOL}")
  message(FATAL_ERROR "Could not find stable xdg-shell protocol at ${XDG_SHELL_PROTOCOL}")
endif()

set(WAYLAND_PROTOCOL_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols")
set(XDG_SHELL_CLIENT_HEADER "${WAYLAND_PROTOCOL_OUTPUT_DIR}/xdg-shell-client-protocol.h")
set(XDG_SHELL_PRIVATE_CODE "${WAYLAND_PROTOCOL_OUTPUT_DIR}/xdg-shell-protocol.c")

add_custom_command(
  OUTPUT "${XDG_SHELL_CLIENT_HEADER}" "${XDG_SHELL_PRIVATE_CODE}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${WAYLAND_PROTOCOL_OUTPUT_DIR}"
  COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header
    "${XDG_SHELL_PROTOCOL}" "${XDG_SHELL_CLIENT_HEADER}"
  COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code
    "${XDG_SHELL_PROTOCOL}" "${XDG_SHELL_PRIVATE_CODE}"
  DEPENDS "${XDG_SHELL_PROTOCOL}"
  VERBATIM
)

add_library(scalpel_xdg_shell STATIC "${XDG_SHELL_PRIVATE_CODE}")
target_include_directories(scalpel_xdg_shell PUBLIC "${WAYLAND_PROTOCOL_OUTPUT_DIR}")
target_link_libraries(scalpel_xdg_shell PUBLIC PkgConfig::WAYLAND_CLIENT)

add_library(scalpel_wayland STATIC WaylandWindow.cxx)
target_include_directories(scalpel_wayland PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(scalpel_wayland PUBLIC
  scalpel_xdg_shell
  PkgConfig::WAYLAND_CLIENT
  PkgConfig::WAYLAND_EGL
)
target_compile_options(scalpel_wayland PRIVATE -Wall -Wextra)
add_dependencies(scalpel_wayland scalpel_xdg_shell)

# Keep the platform definitions as object files in each final target. The core
# static archive refers back to Window and Platform functions supplied here.
add_library(scalpel_application OBJECT
  ApplicationEditor.cxx
  ApplicationPlatform.cxx
)

target_include_directories(scalpel_application PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(scalpel_application PUBLIC
  scintilla_core
  scintilla_render
  scintilla_platform_popups
)
target_compile_options(scalpel_application PRIVATE -Wall -Wextra)

add_executable(scalpel-editor main.cxx)
target_link_libraries(scalpel-editor PRIVATE scalpel_application scalpel_wayland)
target_compile_options(scalpel-editor PRIVATE -Wall -Wextra)

add_subdirectory(test)
