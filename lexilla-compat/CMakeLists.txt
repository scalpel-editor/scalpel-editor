# Project-owned Lexilla glue. Upstream sources stay in ../lexilla and are
# not edited. The private Scintilla.h / SciLexer.h headers are visible only
# while compiling this target.

set(LEXILLA_ROOT "${PROJECT_SOURCE_DIR}/lexilla")

add_library(lexilla STATIC
	CreateLexer.cxx
	"${LEXILLA_ROOT}/lexers/LexMarkdown.cxx"
	"${LEXILLA_ROOT}/lexlib/Accessor.cxx"
	"${LEXILLA_ROOT}/lexlib/CharacterSet.cxx"
	"${LEXILLA_ROOT}/lexlib/LexAccessor.cxx"
	"${LEXILLA_ROOT}/lexlib/LexerBase.cxx"
	"${LEXILLA_ROOT}/lexlib/LexerModule.cxx"
	"${LEXILLA_ROOT}/lexlib/LexerSimple.cxx"
	"${LEXILLA_ROOT}/lexlib/PropSetSimple.cxx"
	"${LEXILLA_ROOT}/lexlib/StyleContext.cxx"
	"${LEXILLA_ROOT}/lexlib/WordList.cxx"
)

target_include_directories(lexilla
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}
		${PROJECT_SOURCE_DIR}/scintilla/include
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/private
		${LEXILLA_ROOT}/lexlib
)

target_compile_definitions(lexilla PRIVATE LEXILLA_NO_EXPORT)
target_compile_options(lexilla PRIVATE -Wall -Wextra)

if(BUILD_TESTING)
	add_executable(lexillaFactoryTest
		LexillaFactoryTest.cxx
	)
	target_include_directories(lexillaFactoryTest PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/private
		${PROJECT_SOURCE_DIR}/scintilla/test/unit
	)
	target_link_libraries(lexillaFactoryTest PRIVATE lexilla)
	target_compile_options(lexillaFactoryTest PRIVATE -Wall -Wextra)
	add_test(NAME lexilla-factory COMMAND lexillaFactoryTest)
endif()
