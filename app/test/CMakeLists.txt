add_executable(applicationTest ApplicationEditorTest.cxx)
target_include_directories(applicationTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../scintilla/test/unit
)
target_link_libraries(applicationTest PRIVATE scalpel_application)
target_compile_options(applicationTest PRIVATE -Wall -Wextra)

add_test(NAME application COMMAND applicationTest)
