set(scintilla_source_dir "${PROJECT_SOURCE_DIR}/Dependencies/scintilla")

# Keep Scintilla and its OnlyWayUi platform port separate from the public library targets.
add_library(owui_scintilla STATIC
	"${CMAKE_CURRENT_SOURCE_DIR}/ElementScintilla.cxx"
	"${CMAKE_CURRENT_SOURCE_DIR}/ElementScintilla.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/PlatOWUI.cxx"
	"${CMAKE_CURRENT_SOURCE_DIR}/PlatOWUI.h"
	"${CMAKE_CURRENT_SOURCE_DIR}/ScintillaOWUI.cxx"
	"${CMAKE_CURRENT_SOURCE_DIR}/ScintillaOWUI.h"
	"${scintilla_source_dir}/src/AutoComplete.cxx"
	"${scintilla_source_dir}/src/CallTip.cxx"
	"${scintilla_source_dir}/src/CaseConvert.cxx"
	"${scintilla_source_dir}/src/CaseFolder.cxx"
	"${scintilla_source_dir}/src/CellBuffer.cxx"
	"${scintilla_source_dir}/src/ChangeHistory.cxx"
	"${scintilla_source_dir}/src/CharacterCategoryMap.cxx"
	"${scintilla_source_dir}/src/CharacterType.cxx"
	"${scintilla_source_dir}/src/CharClassify.cxx"
	"${scintilla_source_dir}/src/ContractionState.cxx"
	"${scintilla_source_dir}/src/DBCS.cxx"
	"${scintilla_source_dir}/src/Decoration.cxx"
	"${scintilla_source_dir}/src/Document.cxx"
	"${scintilla_source_dir}/src/EditModel.cxx"
	"${scintilla_source_dir}/src/Editor.cxx"
	"${scintilla_source_dir}/src/EditView.cxx"
	"${scintilla_source_dir}/src/Geometry.cxx"
	"${scintilla_source_dir}/src/Indicator.cxx"
	"${scintilla_source_dir}/src/KeyMap.cxx"
	"${scintilla_source_dir}/src/LineMarker.cxx"
	"${scintilla_source_dir}/src/MarginView.cxx"
	"${scintilla_source_dir}/src/PerLine.cxx"
	"${scintilla_source_dir}/src/PositionCache.cxx"
	"${scintilla_source_dir}/src/RESearch.cxx"
	"${scintilla_source_dir}/src/RunStyles.cxx"
	"${scintilla_source_dir}/src/ScintillaBase.cxx"
	"${scintilla_source_dir}/src/Selection.cxx"
	"${scintilla_source_dir}/src/Style.cxx"
	"${scintilla_source_dir}/src/UndoHistory.cxx"
	"${scintilla_source_dir}/src/UniConversion.cxx"
	"${scintilla_source_dir}/src/UniqueString.cxx"
	"${scintilla_source_dir}/src/ViewStyle.cxx"
	"${scintilla_source_dir}/src/XPM.cxx"
)

target_compile_features(owui_scintilla PUBLIC cxx_std_17)
target_include_directories(owui_scintilla PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${scintilla_source_dir}/include"
	"${scintilla_source_dir}/src"
)
target_link_libraries(owui_scintilla PRIVATE owui_core)
set_target_properties(owui_scintilla PROPERTIES
	CXX_EXTENSIONS OFF
	POSITION_INDEPENDENT_CODE ON
)
