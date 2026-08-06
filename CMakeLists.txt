cmake_minimum_required(VERSION 3.25)
project(scalpel-editor LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# So clangd and other tools get the real -I paths and flags (see .clangd).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

add_subdirectory(scintilla)
add_subdirectory(app)
