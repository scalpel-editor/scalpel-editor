// scalpel-editor test code
/** @file EditorLinesTest.cxx
 ** Focused behavior tests for EOL policy, lines, indentation, and line queries.
 **/

#include <array>
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
#include "ScintillaMessages.h"
#include "ScintillaStructures.h"
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

void LoadClean(TestEditor &editor, std::string_view text) {
	editor.SetText(text);
	editor.WndProc(Message::EmptyUndoBuffer, 0, 0);
	editor.SetSavePoint();
}

void SetTargetWholeDocument(TestEditor &editor) {
	editor.WndProc(Message::TargetWholeDocument, 0, 0);
}

std::string LineText(TestEditor &editor, Sci::Line line) {
	const Sci::Position len = static_cast<Sci::Position>(
		editor.WndProc(Message::GetLine, static_cast<uptr_t>(line), 0));
	std::string buffer(static_cast<size_t>(len), '\0');
	editor.WndProc(Message::GetLine, static_cast<uptr_t>(line),
		reinterpret_cast<sptr_t>(buffer.data()));
	return buffer;
}

}

TEST_CASE("EOL mode defaults round-trip through named methods and messages") {
	TestHost host;
	TestEditor editor(host);
	const EndOfLine initial = editor.GetEOLMode();
	CHECK(static_cast<EndOfLine>(editor.WndProc(Message::GetEOLMode, 0, 0)) == initial);

	editor.SetEOLMode(EndOfLine::Cr);
	CHECK(editor.GetEOLMode() == EndOfLine::Cr);
	CHECK(static_cast<EndOfLine>(editor.WndProc(Message::GetEOLMode, 0, 0)) == EndOfLine::Cr);

	editor.WndProc(Message::SetEOLMode, static_cast<uptr_t>(EndOfLine::Lf), 0);
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
	CHECK(editor.WndProc(Message::GetLineCount, 0, 0) == 4);

	// Message path matches the named method.
	LoadClean(editor, "x\r\ny\r");
	editor.WndProc(Message::ConvertEOLs, static_cast<uptr_t>(EndOfLine::CrLf), 0);
	CHECK(editor.GetText() == "x\r\ny\r\n");
}

TEST_CASE("Line queries report counts, positions, and line text") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "ab\ncde\n");

	CHECK(editor.WndProc(Message::GetLineCount, 0, 0) == 3);
	CHECK(editor.WndProc(Message::LineFromPosition, 0, 0) == 0);
	CHECK(editor.WndProc(Message::LineFromPosition, 3, 0) == 1);
	CHECK(editor.WndProc(Message::PositionFromLine, 1, 0) == 3);
	CHECK(editor.WndProc(Message::LineLength, 0, 0) == 3);
	CHECK(editor.WndProc(Message::GetLineEndPosition, 0, 0) == 2);
	CHECK(editor.WndProc(Message::GetColumn, 4, 0) == 1);
	CHECK(LineText(editor, 1) == "cde\n");
	CHECK(editor.WndProc(Message::CountCharacters, 0, 3) == 3);
	CHECK(editor.WndProc(Message::CountCodeUnits, 0, 3) == 3);
}

TEST_CASE("Indent and tab settings round-trip and change indentation") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "hello");

	editor.WndProc(Message::SetTabWidth, 4, 0);
	CHECK(editor.WndProc(Message::GetTabWidth, 0, 0) == 4);

	editor.WndProc(Message::SetIndent, 2, 0);
	CHECK(editor.WndProc(Message::GetIndent, 0, 0) == 2);
	editor.WndProc(Message::SetUseTabs, 0, 0);
	CHECK(editor.WndProc(Message::GetUseTabs, 0, 0) == 0);

	editor.WndProc(Message::SetLineIndentation, 0, 4);
	CHECK(editor.WndProc(Message::GetLineIndentation, 0, 0) == 4);
	CHECK(editor.GetText().substr(0, 4) == "    ");
	CHECK(editor.WndProc(Message::GetLineIndentPosition, 0, 0) == 4);

	editor.WndProc(Message::SetTabIndents, 1, 0);
	CHECK(editor.WndProc(Message::GetTabIndents, 0, 0) != 0);
	editor.WndProc(Message::SetBackSpaceUnIndents, 1, 0);
	CHECK(editor.WndProc(Message::GetBackSpaceUnIndents, 0, 0) != 0);
}

TEST_CASE("ViewEOL and SelEOLFilled options round-trip") {
	TestHost host;
	TestEditor editor(host);
	CHECK(editor.WndProc(Message::GetViewEOL, 0, 0) == 0);
	editor.WndProc(Message::SetViewEOL, 1, 0);
	CHECK(editor.WndProc(Message::GetViewEOL, 0, 0) != 0);

	editor.WndProc(Message::SetSelEOLFilled, 1, 0);
	CHECK(editor.WndProc(Message::GetSelEOLFilled, 0, 0) != 0);
}

TEST_CASE("LinesJoin merges target lines with a separating space") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "one\ntwo\nthree\n");
	SetTargetWholeDocument(editor);
	editor.RunCommand(EditorCommand::LinesJoin);
	// Joining also removes a trailing line end inside the target, inserting a space.
	CHECK(editor.GetText() == "one two three ");
}

TEST_CASE("Word character classes are configurable through messages") {
	TestHost host;
	TestEditor editor(host);
	const char *wordChars = "ab";
	editor.WndProc(Message::SetWordChars, 0, reinterpret_cast<sptr_t>(wordChars));
	unsigned char wordBuf[16] = {};
	const Sci::Position nWord = static_cast<Sci::Position>(
		editor.WndProc(Message::GetWordChars, 0, reinterpret_cast<sptr_t>(wordBuf)));
	CHECK(nWord >= 2);
	const std::string words(reinterpret_cast<char *>(wordBuf), static_cast<size_t>(nWord));
	CHECK(words.find('a') != std::string::npos);
	CHECK(words.find('b') != std::string::npos);
}

TEST_CASE("Line character index can be allocated and released") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "hi\n");
	CHECK(static_cast<LineCharacterIndexType>(
		editor.WndProc(Message::GetLineCharacterIndex, 0, 0)) == LineCharacterIndexType::None);
	editor.WndProc(Message::AllocateLineCharacterIndex,
		static_cast<uptr_t>(LineCharacterIndexType::Utf16), 0);
	CHECK(FlagSet(static_cast<LineCharacterIndexType>(
		editor.WndProc(Message::GetLineCharacterIndex, 0, 0)), LineCharacterIndexType::Utf16));
	editor.WndProc(Message::ReleaseLineCharacterIndex,
		static_cast<uptr_t>(LineCharacterIndexType::Utf16), 0);
	CHECK(static_cast<LineCharacterIndexType>(
		editor.WndProc(Message::GetLineCharacterIndex, 0, 0)) == LineCharacterIndexType::None);
}

TEST_CASE("FindColumn and edge column round-trip") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "\thello");
	editor.WndProc(Message::SetTabWidth, 4, 0);
	// Column of 'h' after one tab of width 4 is 4.
	CHECK(editor.WndProc(Message::GetColumn, 1, 0) == 4);
	CHECK(editor.WndProc(Message::FindColumn, 0, 4) == 1);

	editor.WndProc(Message::SetEdgeColumn, 80, 0);
	CHECK(editor.WndProc(Message::GetEdgeColumn, 0, 0) == 80);
}
