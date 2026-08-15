// scalpel-editor test code
/** @file LexillaFactoryTest.cxx
 ** Factory smoke test for the in-tree Lexilla Markdown lexer.
 **/

#include <cstring>

#include "ILexer.h"
#include "CreateLexer.h"
#include "SciLexer.h"

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

TEST_CASE("CreateLexer markdown") {
	Scintilla::ILexer5 *lexer = CreateLexer("markdown");
	REQUIRE(lexer != nullptr);
	REQUIRE(std::strcmp(lexer->GetName(), "markdown") == 0);
	REQUIRE(lexer->GetIdentifier() == SCLEX_MARKDOWN);
	lexer->Release();
}
