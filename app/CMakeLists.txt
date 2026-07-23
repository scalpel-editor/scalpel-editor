# Generate the external Wayland protocol contracts used by the shell.
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(WAYLAND_EGL REQUIRED IMPORTED_TARGET wayland-egl)
pkg_check_modules(WAYLAND_CURSOR REQUIRED IMPORTED_TARGET wayland-cursor)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)

find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner REQUIRED)
pkg_get_variable(WAYLAND_PROTOCOLS_DATA_DIR wayland-protocols pkgdatadir)
set(XDG_SHELL_PROTOCOL "${WAYLAND_PROTOCOLS_DATA_DIR}/stable/xdg-shell/xdg-shell.xml")
set(XDG_DECORATION_PROTOCOL "${WAYLAND_PROTOCOLS_DATA_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml")
set(PRIMARY_SELECTION_PROTOCOL "${WAYLAND_PROTOCOLS_DATA_DIR}/unstable/primary-selection/primary-selection-unstable-v1.xml")
set(TEXT_INPUT_PROTOCOL "${WAYLAND_PROTOCOLS_DATA_DIR}/unstable/text-input/text-input-unstable-v3.xml")
if(NOT EXISTS "${XDG_SHELL_PROTOCOL}")
  message(FATAL_ERROR "Could not find stable xdg-shell protocol at ${XDG_SHELL_PROTOCOL}")
endif()
if(NOT EXISTS "${XDG_DECORATION_PROTOCOL}")
  message(FATAL_ERROR "Could not find xdg-decoration protocol at ${XDG_DECORATION_PROTOCOL}")
endif()
if(NOT EXISTS "${PRIMARY_SELECTION_PROTOCOL}")
  message(FATAL_ERROR "Could not find primary-selection protocol at ${PRIMARY_SELECTION_PROTOCOL}")
endif()
if(NOT EXISTS "${TEXT_INPUT_PROTOCOL}")
  message(FATAL_ERROR "Could not find text-input-v3 protocol at ${TEXT_INPUT_PROTOCOL}")
endif()

set(WAYLAND_PROTOCOL_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols")
set(XDG_SHELL_CLIENT_HEADER "${WAYLAND_PROTOCOL_OUTPUT_DIR}/xdg-shell-client-protocol.h")
set(XDG_SHELL_PRIVATE_CODE "${WAYLAND_PROTOCOL_OUTPUT_DIR}/xdg-shell-protocol.c")
set(XDG_DECORATION_CLIENT_HEADER "${WAYLAND_PROTOCOL_OUTPUT_DIR}/xdg-decoration-client-protocol.h")
set(XDG_DECORATION_PRIVATE_CODE "${WAYLAND_PROTOCOL_OUTPUT_DIR}/xdg-decoration-protocol.c")
set(PRIMARY_SELECTION_CLIENT_HEADER "${WAYLAND_PROTOCOL_OUTPUT_DIR}/primary-selection-client-protocol.h")
set(PRIMARY_SELECTION_PRIVATE_CODE "${WAYLAND_PROTOCOL_OUTPUT_DIR}/primary-selection-protocol.c")
set(TEXT_INPUT_CLIENT_HEADER "${WAYLAND_PROTOCOL_OUTPUT_DIR}/text-input-client-protocol.h")
set(TEXT_INPUT_PRIVATE_CODE "${WAYLAND_PROTOCOL_OUTPUT_DIR}/text-input-protocol.c")

add_custom_command(
  OUTPUT
    "${XDG_SHELL_CLIENT_HEADER}"
    "${XDG_SHELL_PRIVATE_CODE}"
    "${XDG_DECORATION_CLIENT_HEADER}"
    "${XDG_DECORATION_PRIVATE_CODE}"
    "${PRIMARY_SELECTION_CLIENT_HEADER}"
    "${PRIMARY_SELECTION_PRIVATE_CODE}"
    "${TEXT_INPUT_CLIENT_HEADER}"
    "${TEXT_INPUT_PRIVATE_CODE}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${WAYLAND_PROTOCOL_OUTPUT_DIR}"
  COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header
    "${XDG_SHELL_PROTOCOL}" "${XDG_SHELL_CLIENT_HEADER}"
  COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code
    "${XDG_SHELL_PROTOCOL}" "${XDG_SHELL_PRIVATE_CODE}"
  COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header
    "${XDG_DECORATION_PROTOCOL}" "${XDG_DECORATION_CLIENT_HEADER}"
  COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code
    "${XDG_DECORATION_PROTOCOL}" "${XDG_DECORATION_PRIVATE_CODE}"
  COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header
    "${PRIMARY_SELECTION_PROTOCOL}" "${PRIMARY_SELECTION_CLIENT_HEADER}"
  COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code
    "${PRIMARY_SELECTION_PROTOCOL}" "${PRIMARY_SELECTION_PRIVATE_CODE}"
  COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header
    "${TEXT_INPUT_PROTOCOL}" "${TEXT_INPUT_CLIENT_HEADER}"
  COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code
    "${TEXT_INPUT_PROTOCOL}" "${TEXT_INPUT_PRIVATE_CODE}"
  DEPENDS
    "${XDG_SHELL_PROTOCOL}"
    "${XDG_DECORATION_PROTOCOL}"
    "${PRIMARY_SELECTION_PROTOCOL}"
    "${TEXT_INPUT_PROTOCOL}"
  VERBATIM
)

add_library(scalpel_xdg_shell STATIC
  "${XDG_SHELL_PRIVATE_CODE}"
  "${XDG_DECORATION_PRIVATE_CODE}"
  "${PRIMARY_SELECTION_PRIVATE_CODE}"
  "${TEXT_INPUT_PRIVATE_CODE}"
)
target_include_directories(scalpel_xdg_shell PUBLIC "${WAYLAND_PROTOCOL_OUTPUT_DIR}")
target_link_libraries(scalpel_xdg_shell PUBLIC PkgConfig::WAYLAND_CLIENT)

add_library(scalpel_wayland STATIC
  WaylandClipboard.cxx
	WaylandCursor.cxx
  WaylandInput.cxx
  WaylandLifecycle.cxx
  WaylandPrimarySelection.cxx
  WaylandTextInput.cxx
  WaylandTransfer.cxx
  WaylandWindow.cxx
)
target_include_directories(scalpel_wayland PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/scintilla/src
)
target_link_libraries(scalpel_wayland PUBLIC
  scalpel_xdg_shell
  PkgConfig::WAYLAND_CLIENT
  PkgConfig::WAYLAND_EGL
  PkgConfig::WAYLAND_CURSOR
  PkgConfig::XKBCOMMON
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
