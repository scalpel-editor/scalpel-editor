// scalpel-editor test code
/** @file EditorLinesTest.cxx
 ** Focused behavior tests for EOL policy, lines, indentation, line queries,
 ** and blockquote prefixes.
 **/

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
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

#include "TestPlatform.h"
#include "TestEditor.h"

#include "catch.hpp"

using namespace Scintilla;
using namespace Scintilla::Internal;

namespace {

std::string LineText(TestEditor &editor, Sci::Line line) {
	const Sci::Position len = editor.GetLine(line, nullptr);
	std::string buffer(static_cast<size_t>(len), '\0');
	editor.GetLine(line, buffer.data());
	return buffer;
}

}

TEST_CASE("EOL mode defaults and round-trips") {
	TestHost host;
	TestEditor editor(host);
	const EndOfLine initial = editor.GetEOLMode();
	CHECK(initial == editor.GetEOLMode());

	editor.SetEOLMode(EndOfLine::Cr);
	CHECK(editor.GetEOLMode() == EndOfLine::Cr);

	editor.SetEOLMode(EndOfLine::Lf);
	CHECK(editor.GetEOLMode() == EndOfLine::Lf);
}

TEST_CASE("ConvertEOLs rewrites mixed endings and marks modified") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "a\r\nb\rc\n");
	CHECK_FALSE(editor.GetModify());

	editor.ConvertEOLs(EndOfLine::Lf);
	CHECK(editor.GetText() == "a\nb\nc\n");
	CHECK(editor.GetModify());
	CHECK(editor.GetLineCount() == 4);

	LoadClean(editor, "x\r\ny\r");
	editor.ConvertEOLs(EndOfLine::CrLf);
	CHECK(editor.GetText() == "x\r\ny\r\n");
}

TEST_CASE("Line queries report counts, positions, and line text") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "ab\ncde\n");

	CHECK(editor.GetLineCount() == 3);
	CHECK(editor.LineFromPosition(0) == 0);
	CHECK(editor.LineFromPosition(3) == 1);
	CHECK(editor.PositionFromLine(1) == 3);
	CHECK(editor.LineLength(0) == 3);
	CHECK(editor.GetLineEndPosition(0) == 2);
	CHECK(editor.GetColumn(4) == 1);
	CHECK(LineText(editor, 1) == "cde\n");
	CHECK(editor.CountCharacters(0, 3) == 3);
	CHECK(editor.CountCodeUnits(0, 3) == 3);
}

TEST_CASE("Indent and tab settings round-trip and change indentation") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "hello");

	editor.SetTabWidth(4);
	CHECK(editor.GetTabWidth() == 4);

	editor.SetIndent(2);
	CHECK(editor.GetIndent() == 2);
	editor.SetUseTabs(false);
	CHECK_FALSE(editor.GetUseTabs());

	editor.SetLineIndentation(0, 4);
	CHECK(editor.GetLineIndentation(0) == 4);
	CHECK(editor.GetText().substr(0, 4) == "    ");
	CHECK(editor.GetLineIndentPosition(0) == 4);

	editor.SetTabIndents(true);
	CHECK(editor.GetTabIndents());
	editor.SetBackSpaceUnIndents(true);
	CHECK(editor.GetBackSpaceUnIndents());
}

TEST_CASE("ViewEOL and SelEOLFilled options round-trip") {
	TestHost host;
	TestEditor editor(host);
	CHECK_FALSE(editor.GetViewEOL());
	editor.SetViewEOL(true);
	CHECK(editor.GetViewEOL());

	editor.SetSelEOLFilled(true);
	CHECK(editor.GetSelEOLFilled());
}

TEST_CASE("LinesJoin merges target lines with a separating space") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "one\ntwo\nthree\n");
	editor.TargetWholeDocument();
	editor.RunCommand(EditorCommand::LinesJoin);
	// Joining also removes a trailing line end inside the target, inserting a space.
	CHECK(editor.GetText() == "one two three ");
}

TEST_CASE("Word character classes are configurable") {
	TestHost host;
	TestEditor editor(host);
	editor.SetWordChars("ab");
	unsigned char wordBuf[16] = {};
	const Sci::Position nWord = editor.GetWordChars(wordBuf);
	CHECK(nWord >= 2);
	const std::string words(reinterpret_cast<char *>(wordBuf), static_cast<size_t>(nWord));
	CHECK(words.find('a') != std::string::npos);
	CHECK(words.find('b') != std::string::npos);
}

TEST_CASE("Line character index can be allocated and released") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "hi\n");
	CHECK(editor.GetLineCharacterIndex() == LineCharacterIndexType::None);
	editor.AllocateLineCharacterIndex(LineCharacterIndexType::Utf16);
	CHECK(FlagSet(editor.GetLineCharacterIndex(), LineCharacterIndexType::Utf16));
	editor.ReleaseLineCharacterIndex(LineCharacterIndexType::Utf16);
	CHECK(editor.GetLineCharacterIndex() == LineCharacterIndexType::None);
}

TEST_CASE("FindColumn and edge column round-trip") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "\thello");
	editor.SetTabWidth(4);
	// Column of 'h' after one tab of width 4 is 4.
	CHECK(editor.GetColumn(1) == 4);
	CHECK(editor.FindColumn(0, 4) == 1);

	editor.SetEdgeColumn(80);
	CHECK(editor.GetEdgeColumn() == 80);
}

TEST_CASE("quote adds a marker on the caret line") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "foo");
	editor.GotoPos(1);

	editor.AddBlockQuote();
	CHECK(editor.GetText() == "> foo");
	CHECK(editor.CurrentPos() == 3);
	CHECK(editor.GetModify());
}

TEST_CASE("quote nests an existing marker") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "> foo");
	editor.GotoPos(2);

	editor.AddBlockQuote();
	CHECK(editor.GetText() == "> > foo");
}

TEST_CASE("quote an in-line selection quotes the whole line") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "abcdef");
	editor.SetSel(2, 4);

	editor.AddBlockQuote();
	CHECK(editor.GetText() == "> abcdef");
	CHECK(editor.GetAnchor() == 4);
	CHECK(editor.CurrentPos() == 6);
}

TEST_CASE("quote a multi-line selection prefixes each selected line") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "one\ntwo\nthree\n");
	editor.SetSel(0, 8);

	editor.AddBlockQuote();
	CHECK(editor.GetText() == "> one\n> two\nthree\n");
	CHECK(editor.GetAnchor() == 0);
	CHECK(editor.CurrentPos() == 12);
}

TEST_CASE("quote Select All on a trailing newline omits the empty last line") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "hello\n");
	editor.RunCommand(EditorCommand::SelectAll);

	editor.AddBlockQuote();
	CHECK(editor.GetText() == "> hello\n");
}

TEST_CASE("quote empty lines receive a marker") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "a\n\nb");
	editor.SetSel(0, 4);

	editor.AddBlockQuote();
	CHECK(editor.GetText() == "> a\n> \n> b");
}

TEST_CASE("quote unquote strips nested, compact, and spaceless markers") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "> > foo\n>>bar\n>baz\n>");
	editor.RunCommand(EditorCommand::SelectAll);

	editor.RemoveBlockQuote();
	CHECK(editor.GetText() == "> foo\n>bar\nbaz\n");
}

TEST_CASE("quote unquote treats 0-3 leading spaces as the marker") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, " > a\n  > b\n   > c\n    > d");
	editor.RunCommand(EditorCommand::SelectAll);

	editor.RemoveBlockQuote();
	CHECK(editor.GetText() == "a\nb\nc\n    > d");
}

TEST_CASE("quote unquote with no marker leaves text and undo unchanged") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "plain");
	editor.GotoPos(0);

	editor.RemoveBlockQuote();
	CHECK(editor.GetText() == "plain");
	CHECK_FALSE(editor.GetModify());
	CHECK_FALSE(editor.CanUndo());
}

TEST_CASE("quote add and remove are one undo action") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "one\ntwo");
	editor.SetSel(0, 7);

	editor.AddBlockQuote();
	CHECK(editor.GetText() == "> one\n> two");
	editor.RunCommand(EditorCommand::Undo);
	CHECK(editor.GetText() == "one\ntwo");
	CHECK_FALSE(editor.GetModify());

	LoadClean(editor, "> one\n> two");
	editor.SetSel(0, 11);
	editor.RemoveBlockQuote();
	CHECK(editor.GetText() == "one\ntwo");
	editor.RunCommand(EditorCommand::Undo);
	CHECK(editor.GetText() == "> one\n> two");
}

TEST_CASE("quote is a no-op on a read-only document") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "foo");
	editor.SetReadOnly(true);

	editor.AddBlockQuote();
	CHECK(editor.GetText() == "foo");
	CHECK_FALSE(editor.GetModify());
	CHECK_FALSE(editor.CanUndo());

	editor.SetReadOnly(false);
	LoadClean(editor, "> foo");
	editor.SetReadOnly(true);
	editor.RemoveBlockQuote();
	CHECK(editor.GetText() == "> foo");
	CHECK_FALSE(editor.GetModify());
}

TEST_CASE("quote keeps the caret on the same content character") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "foo");
	editor.GotoPos(1);

	editor.AddBlockQuote();
	CHECK(editor.GetText() == "> foo");
	CHECK(editor.CurrentPos() == 3);
	CHECK(editor.GetText()[static_cast<size_t>(editor.CurrentPos())] == 'o');

	editor.RemoveBlockQuote();
	CHECK(editor.GetText() == "foo");
	CHECK(editor.CurrentPos() == 1);
}

TEST_CASE("quote unquote leaves invalid UTF-8 after the marker unchanged") {
	TestHost host;
	TestEditor editor(host);
	const char raw[] = {'>', ' ', static_cast<char>(0xff), 'x'};
	LoadClean(editor, std::string(raw, sizeof(raw)));
	editor.GotoPos(0);

	editor.RemoveBlockQuote();
	CHECK(editor.GetText() == (std::string{static_cast<char>(0xff), 'x'}));
}
