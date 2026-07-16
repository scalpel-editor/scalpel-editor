# This directory is a subdirectory of the scalpel-editor build, never a
# project of its own. Configuring it directly (for example `cd scintilla &&
# cmake -B build`) makes a second, duplicate build tree that the presets in
# CMakePresets.json and the rules in .gitignore do not manage. Fail loudly and
# point the caller back at the sanctioned entry points.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	message(FATAL_ERROR
		"scintilla/ is built as part of scalpel-editor, not on its own. "
		"Configure from the repository root instead:\n"
		"  cmake --preset dev   (or ./check.sh for the full matrix)")
endif()

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
  src/EditorHost.cxx
  src/EditorInput.cxx
  src/EditorLexing.cxx
  src/EditorLines.cxx
  src/EditorMargins.cxx
  src/EditorMarkers.cxx
  src/EditorPrinting.cxx
  src/EditorScrolling.cxx
  src/EditorSearch.cxx
  src/EditorSelection.cxx
  src/EditorStyling.cxx
  src/EditorTextBoundaries.cxx
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
