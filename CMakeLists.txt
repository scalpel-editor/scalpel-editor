cmake_minimum_required(VERSION 3.25)
project(scalpel-editor VERSION 1.0.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# So clangd and other tools get the real -I paths and flags (see .clangd).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(scintilla)
add_subdirectory(app)
