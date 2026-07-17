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

# The Scintilla core as one static library. The concrete platform is arriving
# concern by concern in phase 6, starting with fonts. Nothing links into a
# runnable program yet. The unit tests link only the subset of objects they
# need, which the static library provides on demand.

find_package(PkgConfig REQUIRED)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)

# Font lookup, FreeType/HarfBuzz ownership, UTF-8 helpers used by shaping, and the
# shaped-run model. UniConversion lives here so shaping can walk invalid UTF-8
# without a circular link on scintilla_core.
add_library(scintilla_font STATIC
  src/FontPlatform.cxx
  src/ShapedRun.cxx
  src/UniConversion.cxx
)

target_include_directories(scintilla_font PUBLIC include src)
target_link_libraries(scintilla_font PUBLIC
  PkgConfig::FREETYPE
  PkgConfig::FONTCONFIG
  PkgConfig::HARFBUZZ
)
target_compile_options(scintilla_font PRIVATE -Wall -Wextra)

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
  src/EditorRecording.cxx
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
  src/UniqueString.cxx
  src/ViewStyle.cxx
  src/XPM.cxx
)

target_include_directories(scintilla_core PUBLIC include src)

target_link_libraries(scintilla_core PUBLIC scintilla_font)

target_compile_options(scintilla_core PRIVATE -Wall -Wextra)

add_subdirectory(test/unit)
add_subdirectory(test/editor)
add_subdirectory(test/font)
