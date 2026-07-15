# The Scintilla core as one static library. Everything in src/ compiles
# against Platform.h alone; the concrete platform implementation arrives in a
# later phase, so nothing here links into a runnable program yet. The unit
# tests link only the subset of objects they need, which the static library
# provides on demand.

add_library(scintilla_core STATIC
  src/AutoComplete.cxx
  src/CallTip.cxx
  src/CaseConvert.cxx
  src/CaseFolder.cxx
  src/CellBuffer.cxx
  src/ChangeHistory.cxx
  src/CharacterCategoryMap.cxx
  src/CharacterType.cxx
  src/CharClassify.cxx
  src/ContractionState.cxx
  src/Decoration.cxx
  src/Document.cxx
  src/EditModel.cxx
  src/Editor.cxx
  src/EditorAutocomplete.cxx
  src/EditorCallTips.cxx
  src/EditorCaret.cxx
  src/EditorClipboard.cxx
  src/EditorCommands.cxx
  src/EditorDecorations.cxx
  src/EditorDocument.cxx
  src/EditorFolding.cxx
  src/EditorHistory.cxx
  src/EditorLines.cxx
  src/EditorMargins.cxx
  src/EditorMarkers.cxx
  src/EditorScrolling.cxx
  src/EditorSearch.cxx
  src/EditorSelection.cxx
  src/EditorStyling.cxx
  src/EditorWrapping.cxx
  src/EditView.cxx
  src/Geometry.cxx
  src/Indicator.cxx
  src/KeyMap.cxx
  src/LineMarker.cxx
  src/MarginView.cxx
  src/PerLine.cxx
  src/PositionCache.cxx
  src/RESearch.cxx
  src/RunStyles.cxx
  src/ScintillaBase.cxx
  src/Selection.cxx
  src/Style.cxx
  src/UndoHistory.cxx
  src/UniConversion.cxx
  src/UniqueString.cxx
  src/ViewStyle.cxx
  src/XPM.cxx
)

target_include_directories(scintilla_core PUBLIC include src)

target_compile_options(scintilla_core PRIVATE -Wall -Wextra)

add_subdirectory(test/unit)
add_subdirectory(test/editor)
