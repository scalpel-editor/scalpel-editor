// scalpel-editor test code
/** @file EditorLinesTest.cxx
 ** Focused behavior tests for EOL policy, lines, indentation, and line queries.
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

#include "ScintillaTypes.h"
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
