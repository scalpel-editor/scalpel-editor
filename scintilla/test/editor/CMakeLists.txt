# Editor tests link the whole editor path against a deterministic test host.

add_executable(editorTest
  EditorWrappingTest.cxx
  TestEditor.cxx
  TestEditorTest.cxx
  TestPlatform.cxx
)

target_include_directories(editorTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ../unit)
target_link_libraries(editorTest PRIVATE scintilla_core)
target_compile_options(editorTest PRIVATE -Wall -Wextra)

add_test(NAME editor COMMAND editorTest)
