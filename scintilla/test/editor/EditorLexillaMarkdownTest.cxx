// scalpel-editor test code
/** @file EditorLexillaMarkdownTest.cxx
 ** Focused tests that attach the real Lexilla Markdown ILexer5 through
 ** SetILexer and inspect document style bytes.
 **/

#include <array>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <forward_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "EditorBasicTypes.h"
#include "EditorDocumentTypes.h"
#include "EditorStyleTypes.h"
#include "EditorInputTypes.h"
#include "EditorLayoutTypes.h"
#include "ILoader.h"
#include "ILexer.h"

#include "Debugging.h"
#include "Geometry.h"
#include "Platform.h"
#include "CharacterType.h"
#include "CharacterCategoryMap.h"
#include "Position.h"
#include "UniqueString.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "ContractionState.h"
#include "CellBuffer.h"
#include "PerLine.h"
#include "KeyMap.h"
#include "Indicator.h"
#include "LineMarker.h"
#include "Style.h"
#include "ViewStyle.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "Document.h"
#include "UniConversion.h"
#include "Selection.h"
#include "PositionCache.h"
#include "EditModel.h"
#include "MarginView.h"
#include "EditView.h"
#include "Editor.h"
#include "AutoComplete.h"
#include "CallTip.h"
#include "ScintillaBase.h"

#include "CreateLexer.h"

#include "TestPlatform.h"
#include "TestEditor.h"

#include "catch.hpp"

using namespace Scintilla;
using namespace Scintilla::Internal;

namespace {

// Lexilla 5.5.3 Markdown style numbers. Copied so this target does not
// include the private SciLexer.h used only while compiling Lexilla.
constexpr int kMarkdownDefault = 0;
constexpr int kMarkdownStrong1 = 2;
constexpr int kMarkdownStrong2 = 3;
constexpr int kMarkdownEm1 = 4;
constexpr int kMarkdownEm2 = 5;
constexpr int kMarkdownHeader1 = 6;
constexpr int kMarkdownUListItem = 13;
constexpr int kMarkdownOListItem = 14;
constexpr int kMarkdownBlockQuote = 15;
constexpr int kMarkdownHRule = 17;
constexpr int kMarkdownLink = 18;
constexpr int kMarkdownCode = 19;
constexpr int kMarkdownCode2 = 20;
constexpr int kMarkdownCodeBlock = 21;
constexpr int kMarkdownLexerId = 98;

// Lexilla 5.5.3 Markdown highlights delimited blocks opened with ~~~.
constexpr std::string_view kSampleMarkdown =
	"# Heading caf\xc3\xa9\n"
	"\n"
	"plain text\n"
	"\n"
	"**strong** and *em* and __strong2__ and _em2_\n"
	"\n"
	"- ulist\n"
	"1. olist\n"
	"\n"
	"> quote\n"
	"\n"
	"[link](https://example.com)\n"
	"\n"
	"`inline` and ``code2``\n"
	"\n"
	"~~~\n"
	"fenced\n"
	"~~~\n"
	"\n"
	"---\n";

void AttachMarkdownLexer(TestEditor &editor) {
	ILexer5 *lexer = CreateLexer("markdown");
	REQUIRE(lexer != nullptr);
	editor.SetILexer(lexer);
}

Sci::Position RequireFind(const std::string &text, std::string_view needle) {
	const auto pos = text.find(needle);
	REQUIRE(pos != std::string::npos);
	return static_cast<Sci::Position>(pos);
}

void CheckStyleAt(const TestEditor &editor, Sci::Position pos, int expected) {
	INFO("pos=" << pos << " ch=" << static_cast<int>(static_cast<unsigned char>(editor.GetCharAt(pos))));
	CHECK(editor.GetStyleAt(pos) == expected);
}

void CheckStyleRun(const TestEditor &editor, Sci::Position pos, Sci::Position length, int expected) {
	for (Sci::Position i = 0; i < length; ++i) {
		CheckStyleAt(editor, pos + i, expected);
	}
}

}

TEST_CASE("Lexilla Markdown Colourise styles representative UTF-8 tokens") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText(kSampleMarkdown);
	AttachMarkdownLexer(editor);

	CHECK(std::string(editor.GetLexerLanguage()) == "markdown");
	CHECK(editor.GetLexer() == kMarkdownLexerId);

	editor.Colourise(0, -1);
	CHECK(editor.GetEndStyled() >= editor.GetTextLength());

	const std::string text = editor.GetText();
	REQUIRE(text == kSampleMarkdown);

	CheckStyleAt(editor, RequireFind(text, "# Heading"), kMarkdownHeader1);
	const Sci::Position headingText = RequireFind(text, "Heading");
	CheckStyleAt(editor, headingText, kMarkdownDefault);
	CheckStyleRun(editor, RequireFind(text, "caf\xc3\xa9"), 5, kMarkdownDefault);

	CheckStyleRun(editor, RequireFind(text, "plain text"), 10, kMarkdownDefault);

	CheckStyleRun(editor, RequireFind(text, "**strong**"), 10, kMarkdownStrong1);
	CheckStyleRun(editor, RequireFind(text, "*em*"), 4, kMarkdownEm1);
	CheckStyleRun(editor, RequireFind(text, "__strong2__"), 11, kMarkdownStrong2);
	CheckStyleRun(editor, RequireFind(text, "_em2_"), 5, kMarkdownEm2);

	CheckStyleAt(editor, RequireFind(text, "- ulist"), kMarkdownUListItem);
	CheckStyleRun(editor, RequireFind(text, "ulist"), 5, kMarkdownDefault);
	CheckStyleRun(editor, RequireFind(text, "1."), 2, kMarkdownOListItem);
	CheckStyleRun(editor, RequireFind(text, "olist"), 5, kMarkdownDefault);

	CheckStyleAt(editor, RequireFind(text, "> quote"), kMarkdownBlockQuote);
	CheckStyleRun(editor, RequireFind(text, "quote"), 5, kMarkdownDefault);

	CheckStyleRun(editor, RequireFind(text, "[link](https://example.com)"), 27, kMarkdownLink);

	CheckStyleRun(editor, RequireFind(text, "`inline`"), 8, kMarkdownCode);
	CheckStyleRun(editor, RequireFind(text, "``code2``"), 9, kMarkdownCode2);

	const Sci::Position fence = RequireFind(text, "~~~\n");
	CheckStyleRun(editor, fence, 3, kMarkdownCodeBlock);
	CheckStyleRun(editor, RequireFind(text, "fenced"), 6, kMarkdownCodeBlock);
	const auto fenceClose = text.find("~~~\n", static_cast<size_t>(fence) + 1);
	REQUIRE(fenceClose != std::string::npos);
	CheckStyleRun(editor, static_cast<Sci::Position>(fenceClose), 3, kMarkdownCodeBlock);

	CheckStyleRun(editor, RequireFind(text, "---"), 3, kMarkdownHRule);
}

TEST_CASE("Lexilla Markdown restyles after insert delete undo and redo") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("plain text\n");
	AttachMarkdownLexer(editor);
	editor.EmptyUndoBuffer();
	editor.PaintAll();
	CHECK(editor.GetEndStyled() >= editor.GetTextLength());
	CheckStyleRun(editor, RequireFind(editor.GetText(), "plain text"), 10, kMarkdownDefault);

	editor.GotoPos(0);
	editor.InsertInput("**hi** ");
	editor.PaintAll();
	CHECK(editor.GetEndStyled() >= editor.GetTextLength());
	{
		const std::string text = editor.GetText();
		CHECK(text.find("**hi** plain text") == 0);
		CheckStyleRun(editor, RequireFind(text, "**hi**"), 6, kMarkdownStrong1);
		CheckStyleRun(editor, RequireFind(text, "plain text"), 10, kMarkdownDefault);
	}

	editor.DeleteRange(0, 2);
	editor.PaintAll();
	{
		const std::string text = editor.GetText();
		CHECK(text.find("hi** plain text") == 0);
		CheckStyleRun(editor, RequireFind(text, "hi**"), 4, kMarkdownDefault);
		CheckStyleRun(editor, RequireFind(text, "plain text"), 10, kMarkdownDefault);
	}

	editor.RunCommand(EditorCommand::Undo);
	editor.PaintAll();
	{
		const std::string text = editor.GetText();
		CheckStyleRun(editor, RequireFind(text, "**hi**"), 6, kMarkdownStrong1);
	}

	editor.RunCommand(EditorCommand::Redo);
	editor.PaintAll();
	{
		const std::string text = editor.GetText();
		CheckStyleRun(editor, RequireFind(text, "hi**"), 4, kMarkdownDefault);
	}
}

TEST_CASE("Lexilla Markdown restyles across fenced code boundaries") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("before\n\n~~~\ncode\n~~~\n\nafter\n");
	AttachMarkdownLexer(editor);
	editor.EmptyUndoBuffer();
	editor.PaintAll();
	CHECK(editor.GetEndStyled() >= editor.GetTextLength());

	std::string text = editor.GetText();
	CheckStyleRun(editor, RequireFind(text, "before"), 6, kMarkdownDefault);
	CheckStyleRun(editor, RequireFind(text, "code"), 4, kMarkdownCodeBlock);
	CheckStyleRun(editor, RequireFind(text, "after"), 5, kMarkdownDefault);

	const Sci::Position openingFence = RequireFind(text, "~~~\n");
	editor.DeleteRange(openingFence, 4);
	editor.PaintAll();
	text = editor.GetText();
	CHECK(text.find("~~~\ncode") == std::string::npos);
	CheckStyleRun(editor, RequireFind(text, "code"), 4, kMarkdownDefault);
	CheckStyleRun(editor, RequireFind(text, "after"), 5, kMarkdownDefault);

	editor.RunCommand(EditorCommand::Undo);
	editor.PaintAll();
	text = editor.GetText();
	CheckStyleRun(editor, RequireFind(text, "code"), 4, kMarkdownCodeBlock);

	editor.RunCommand(EditorCommand::Redo);
	editor.PaintAll();
	text = editor.GetText();
	CheckStyleRun(editor, RequireFind(text, "code"), 4, kMarkdownDefault);
}

TEST_CASE("Lexilla Markdown Colourise preserves invalid UTF-8 bytes") {
	TestHost host;
	TestEditor editor(host);
	const char raw[] = {
		'#', ' ', 'o', 'k', '\n',
		static_cast<char>(0xff), static_cast<char>(0x80), '\n',
		'*', '*', 'x', '*', '*', '\n',
	};
	const std::string bytes(raw, sizeof(raw));
	editor.SetText(bytes);
	AttachMarkdownLexer(editor);

	editor.Colourise(0, -1);
	CHECK(editor.GetText() == bytes);
	CHECK(editor.GetCharAt(5) == static_cast<char>(0xff));
	CHECK(editor.GetCharAt(6) == static_cast<char>(0x80));
	CheckStyleAt(editor, 0, kMarkdownHeader1);
	CheckStyleRun(editor, RequireFind(editor.GetText(), "**x**"), 5, kMarkdownStrong1);

	editor.PaintAll();
	CHECK(editor.GetText() == bytes);
	CHECK(editor.GetCharAt(5) == static_cast<char>(0xff));
	CHECK(editor.GetCharAt(6) == static_cast<char>(0x80));
}
