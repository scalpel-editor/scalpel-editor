# Editor tests link the whole editor path against a deterministic test host.

add_executable(editorTest
  EditorAutocompleteTest.cxx
  EditorCallTipsTest.cxx
  EditorCaretTest.cxx
  EditorClipboardTest.cxx
  EditorCommandTest.cxx
  EditorDecorationsTest.cxx
  EditorDocumentTest.cxx
  EditorFoldingTest.cxx
  EditorHistoryTest.cxx
  EditorInputTest.cxx
  EditorLexingTest.cxx
  EditorLinesTest.cxx
  EditorMarginsTest.cxx
  EditorMarkersTest.cxx
  EditorPrintingTest.cxx
  EditorScrollingTest.cxx
  EditorSearchTest.cxx
  EditorSelectionTest.cxx
  EditorStylingTest.cxx
  EditorTextBoundariesTest.cxx
  EditorWrappingTest.cxx
  PopupHostTest.cxx
  TestEditor.cxx
  TestEditorTest.cxx
  TestPlatform.cxx
)

target_include_directories(editorTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ../unit)
target_link_libraries(editorTest PRIVATE scintilla_core)
target_compile_options(editorTest PRIVATE -Wall -Wextra)

add_test(NAME editor COMMAND editorTest)
