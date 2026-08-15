// scalpel-editor test code
/** @file LexillaFactoryTest.cxx
 ** Factory smoke test for the in-tree Lexilla Markdown lexer.
 **/

#include <memory>
#include <string_view>

#include "ILexer.h"
#include "CreateLexer.h"

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

namespace {

void ReleaseLexer(Scintilla::ILexer5 *lexer) {
	if (lexer) {
		lexer->Release();
	}
}

using LexerPtr = std::unique_ptr<Scintilla::ILexer5, decltype(&ReleaseLexer)>;

constexpr int kMarkdownLexerId = 98;

}

TEST_CASE("CreateLexer markdown") {
	LexerPtr lexer(CreateLexer("markdown"), ReleaseLexer);
	REQUIRE(lexer != nullptr);
	REQUIRE(std::string_view(lexer->GetName()) == "markdown");
	REQUIRE(lexer->GetIdentifier() == kMarkdownLexerId);
}

TEST_CASE("CreateLexer rejects unsupported names") {
	CHECK(CreateLexer(nullptr) == nullptr);
	CHECK(CreateLexer("") == nullptr);
	CHECK(CreateLexer("Markdown") == nullptr);
	CHECK(CreateLexer("cpp") == nullptr);
}
