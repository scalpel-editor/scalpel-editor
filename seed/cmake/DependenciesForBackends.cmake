#[[
	Set up external dependencies required by the Wayland backend.

	This file is only used by the OnlyWayUi CMake project.
]]

find_package(PkgConfig REQUIRED)

pkg_check_modules(OWUI_WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(OWUI_WAYLAND_CURSOR REQUIRED IMPORTED_TARGET wayland-cursor)
pkg_check_modules(OWUI_WAYLAND_EGL REQUIRED IMPORTED_TARGET wayland-egl)
pkg_check_modules(OWUI_WAYLAND_PROTOCOLS REQUIRED IMPORTED_TARGET wayland-protocols)
pkg_check_modules(OWUI_XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(OWUI_EGL REQUIRED IMPORTED_TARGET egl)
# libdbus-1 is the D-Bus client binding for the xdg-desktop-portal file dialog. It is the reference D-Bus library and is
# present wherever a session bus runs, so the portal path needs no dependency beyond it.
pkg_check_modules(OWUI_DBUS REQUIRED IMPORTED_TARGET dbus-1)

find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner)
if(NOT WAYLAND_SCANNER_EXECUTABLE)
	message(FATAL_ERROR "wayland-scanner could not be found.")
endif()

execute_process(
	COMMAND "${PKG_CONFIG_EXECUTABLE}" --variable=pkgdatadir wayland-protocols
	OUTPUT_VARIABLE OWUI_WAYLAND_PROTOCOLS_DIR
	OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(OWUI_XDG_SHELL_PROTOCOL "${OWUI_WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
if(NOT EXISTS "${OWUI_XDG_SHELL_PROTOCOL}")
	message(FATAL_ERROR "Could not find xdg-shell.xml at ${OWUI_XDG_SHELL_PROTOCOL}")
endif()
set(OWUI_XDG_DECORATION_PROTOCOL "${OWUI_WAYLAND_PROTOCOLS_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml")
if(NOT EXISTS "${OWUI_XDG_DECORATION_PROTOCOL}")
	message(FATAL_ERROR "Could not find xdg-decoration-unstable-v1.xml at ${OWUI_XDG_DECORATION_PROTOCOL}")
endif()
set(OWUI_PRESENTATION_TIME_PROTOCOL "${OWUI_WAYLAND_PROTOCOLS_DIR}/stable/presentation-time/presentation-time.xml")
if(NOT EXISTS "${OWUI_PRESENTATION_TIME_PROTOCOL}")
	message(FATAL_ERROR "Could not find presentation-time.xml at ${OWUI_PRESENTATION_TIME_PROTOCOL}")
endif()
set(OWUI_XDG_FOREIGN_PROTOCOL "${OWUI_WAYLAND_PROTOCOLS_DIR}/unstable/xdg-foreign/xdg-foreign-unstable-v2.xml")
if(NOT EXISTS "${OWUI_XDG_FOREIGN_PROTOCOL}")
	message(FATAL_ERROR "Could not find xdg-foreign-unstable-v2.xml at ${OWUI_XDG_FOREIGN_PROTOCOL}")
endif()

report_dependency_found("OWUI_WAYLAND_CLIENT" PkgConfig::OWUI_WAYLAND_CLIENT)
report_dependency_found("OWUI_WAYLAND_CURSOR" PkgConfig::OWUI_WAYLAND_CURSOR)
report_dependency_found("OWUI_WAYLAND_EGL" PkgConfig::OWUI_WAYLAND_EGL)
report_dependency_found("OWUI_WAYLAND_PROTOCOLS" PkgConfig::OWUI_WAYLAND_PROTOCOLS)
report_dependency_found("OWUI_XKBCOMMON" PkgConfig::OWUI_XKBCOMMON)
report_dependency_found("OWUI_EGL" PkgConfig::OWUI_EGL)
report_dependency_found("OWUI_DBUS" PkgConfig::OWUI_DBUS)
