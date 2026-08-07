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

# The Scintilla core is one static library. Fonts, shaping, and the GL renderer
# are separate supporting targets, while the application host lives under
# ../app. Unit tests link only the subset of objects they need, which the
# static library provides on demand.

find_package(PkgConfig REQUIRED)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
pkg_check_modules(HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)
pkg_check_modules(GL REQUIRED IMPORTED_TARGET gl)
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)

# Font lookup, FreeType/HarfBuzz ownership, UTF-8 helpers used by shaping, and the
# shaped-run model. UniConversion lives here so shaping can walk invalid UTF-8
# without a circular link on scintilla_core.
add_library(scintilla_font STATIC
  src/EmojiSequence.cxx
  src/FontPlatform.cxx
  src/MeasureSurface.cxx
  src/ShapedLayout.cxx
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

# Headless EGL/OpenGL context and the one drawing implementation.
# editorTest and the future application host link this for Surface::Allocate.
add_library(scintilla_render STATIC
  src/DrawSurface.cxx
  src/FixedGlyphBitmapReduce.cxx
  src/Geometry.cxx
  src/GlContext.cxx
  src/Renderer.cxx
)

target_include_directories(scintilla_render PUBLIC include src)
target_link_libraries(scintilla_render PUBLIC
  scintilla_font
  PkgConfig::GL
  PkgConfig::EGL
)
target_compile_options(scintilla_render PRIVATE -Wall -Wextra)

# ListBox and Menu stubs for the production host (step 8+). editorTest links
# TestPlatform's inspectable list box instead and must not link this library.
add_library(scintilla_platform_popups STATIC
  src/PlatformPopups.cxx
)
target_include_directories(scintilla_platform_popups PUBLIC include src)
target_compile_options(scintilla_platform_popups PRIVATE -Wall -Wextra)

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

if(BUILD_TESTING)
	add_subdirectory(test/unit)
	add_subdirectory(test/editor)
	add_subdirectory(test/font)
	add_subdirectory(test/layout)
	add_subdirectory(test/renderer)
	add_subdirectory(test/popup_stub)
endif()
