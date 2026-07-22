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
target_link_libraries(scalpel-editor PRIVATE scalpel_application)
target_compile_options(scalpel-editor PRIVATE -Wall -Wextra)

add_subdirectory(test)
