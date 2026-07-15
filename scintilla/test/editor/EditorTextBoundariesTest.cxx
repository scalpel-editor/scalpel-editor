// scalpel-editor test code
/** @file EditorTextBoundariesTest.cxx
 ** Focused tests for UTF-8 character/word boundaries, character classes,
 ** buffer pointer, current line, virtual space options, and line index mapping.
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

TEST_CASE("PositionBefore After and Relative step over UTF-8 characters") {
	TestHost host;
	TestEditor editor(host);
	// "a" + 2-byte é (C3 A9) + "b"
	editor.SetText("a\xC3\xA9" "b");
	CHECK(editor.GetTextLength() == 4);

	CHECK(editor.PositionAfter(0) == 1);   // past 'a'
	CHECK(editor.PositionAfter(1) == 3);   // past é
	CHECK(editor.PositionAfter(3) == 4);   // past 'b'

	CHECK(editor.PositionBefore(4) == 3);
	CHECK(editor.PositionBefore(3) == 1);
	CHECK(editor.PositionBefore(1) == 0);

	CHECK(editor.PositionRelative(0, 2) == 3);  // two characters → start of 'b'
	CHECK(editor.PositionRelative(3, -1) == 1);
	// Overshoot returns invalidPosition from the document, which the named API clamps to 0.
	CHECK(editor.PositionRelative(0, 100) == 0);
	CHECK(editor.PositionRelative(4, -100) == 0);
}

TEST_CASE("PositionRelativeCodeUnits counts UTF-16 units") {
	TestHost host;
	TestEditor editor(host);
	// BMP é is one UTF-16 unit, two UTF-8 bytes.
	editor.SetText("a\xC3\xA9" "b");
	CHECK(editor.PositionRelativeCodeUnits(0, 1) == 1);
	CHECK(editor.PositionRelativeCodeUnits(0, 2) == 3);
	CHECK(editor.PositionRelativeCodeUnits(0, 3) == 4);
}

TEST_CASE("Invalid UTF-8 acts as one-byte characters for boundaries") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("a\x80" "b");
	CHECK(editor.PositionAfter(0) == 1);
	CHECK(editor.PositionAfter(1) == 2);  // lone continuation byte
	CHECK(editor.PositionAfter(2) == 3);
	CHECK(editor.PositionBefore(2) == 1);
}

TEST_CASE("WordStart WordEnd and IsRangeWord") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("hello world");
	CHECK(editor.WordStartPosition(3, true) == 0);
	CHECK(editor.WordEndPosition(3, true) == 5);
	CHECK(editor.IsRangeWord(0, 5));
	CHECK_FALSE(editor.IsRangeWord(0, 4));
	CHECK(editor.WordStartPosition(7, true) == 6);
	CHECK(editor.WordEndPosition(7, true) == 11);
}

TEST_CASE("SetCharsDefault and character category optimization round-trip") {
	TestHost host;
	TestEditor editor(host);
	editor.SetCharacterCategoryOptimization(0x1000);
	CHECK(editor.GetCharacterCategoryOptimization() == 0x1000);
	editor.SetCharsDefault();
	// Optimization value is independent of char classes.
	CHECK(editor.GetCharacterCategoryOptimization() == 0x1000);
}

TEST_CASE("GetCharacterPointer returns document text") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("xyz");
	const char *p = editor.GetCharacterPointer();
	REQUIRE(p != nullptr);
	CHECK(std::string_view(p, 3) == "xyz");
}

TEST_CASE("GetCurLine copies the caret line and reports caret offset") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("one\ntwo\nthree");
	editor.GotoPos(5);  // on 'w' of two
	const Sci::Position need = editor.GetCurLine(nullptr, 0);
	CHECK(need == 4);  // "two\n"
	std::string buf(static_cast<size_t>(need) + 1, '\0');
	const Sci::Position offset = editor.GetCurLine(buf.data(), need);
	CHECK(buf.substr(0, static_cast<size_t>(need)) == "two\n");
	CHECK(offset == 1);  // caret at 'w'
}

TEST_CASE("Virtual space options round-trip") {
	TestHost host;
	TestEditor editor(host);
	CHECK(editor.GetVirtualSpaceOptions() == VirtualSpace::None);
	editor.SetVirtualSpaceOptions(VirtualSpace::UserAccessible);
	CHECK(editor.GetVirtualSpaceOptions() == VirtualSpace::UserAccessible);
}

TEST_CASE("Line index position mapping when UTF16 index allocated") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("a\xC3\xA9" "b\nc");
	editor.WndProc(Message::AllocateLineCharacterIndex,
		static_cast<uptr_t>(LineCharacterIndexType::Utf16), 0);
	// Index of start of line 1 should map back.
	const Sci::Position idx = editor.IndexPositionFromLine(1, LineCharacterIndexType::Utf16);
	CHECK(editor.LineFromIndexPosition(idx, LineCharacterIndexType::Utf16) == 1);
	editor.WndProc(Message::ReleaseLineCharacterIndex,
		static_cast<uptr_t>(LineCharacterIndexType::Utf16), 0);
}

TEST_CASE("Text boundary named path matches message path") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("a\xC3\xA9" " word");

	const Sci::Position pos = 1;
	CHECK(editor.PositionBefore(pos) ==
		static_cast<Sci::Position>(editor.WndProc(Message::PositionBefore, static_cast<uptr_t>(pos), 0)));
	CHECK(editor.PositionAfter(pos) ==
		static_cast<Sci::Position>(editor.WndProc(Message::PositionAfter, static_cast<uptr_t>(pos), 0)));
	CHECK(editor.PositionRelative(0, 2) ==
		static_cast<Sci::Position>(editor.WndProc(Message::PositionRelative, 0, 2)));
	CHECK(editor.WordStartPosition(3, true) ==
		static_cast<Sci::Position>(editor.WndProc(Message::WordStartPosition, 3, 1)));
	CHECK(editor.WordEndPosition(3, true) ==
		static_cast<Sci::Position>(editor.WndProc(Message::WordEndPosition, 3, 1)));

	editor.SetVirtualSpaceOptions(VirtualSpace::RectangularSelection);
	CHECK(static_cast<VirtualSpace>(editor.WndProc(Message::GetVirtualSpaceOptions, 0, 0)) ==
		VirtualSpace::RectangularSelection);
}
