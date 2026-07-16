// scalpel-editor test code
/** @file EditorSearchTest.cxx
 ** Focused behavior tests for target search and replace.
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

TEST_CASE("Target range and SearchInTarget find text") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "hello world hello");

	editor.TargetWholeDocument();
	CHECK(editor.GetTargetStart() == 0);
	CHECK(editor.GetTargetEnd() == editor.GetTextLength());

	const Sci::Position pos = editor.SearchInTarget("world");
	CHECK(pos == 6);
	CHECK(editor.GetTargetStart() == 6);
	CHECK(editor.GetTargetEnd() == 11);
	CHECK(editor.GetTargetText() == "world");
}

TEST_CASE("SearchInTarget message path matches named method") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "abc def abc");
	editor.TargetWholeDocument();
	const char *needle = "def";
	const Sci::Position viaMessage = static_cast<Sci::Position>(
		editor.WndProc(Message::SearchInTarget, 3, reinterpret_cast<sptr_t>(needle)));
	editor.TargetWholeDocument();
	const Sci::Position viaMethod = editor.SearchInTarget("def");
	CHECK(viaMessage == viaMethod);
	CHECK(viaMethod == 4);
}

TEST_CASE("ReplaceTarget replaces the target range") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "one two three");
	editor.SetTargetRange(4, 7);
	CHECK(editor.GetTargetText() == "two");
	editor.WndProc(Message::ReplaceTarget, 1, reinterpret_cast<sptr_t>("2"));
	CHECK(editor.GetText() == "one 2 three");
}

TEST_CASE("Search flags round-trip") {
	TestHost host;
	TestEditor editor(host);
	editor.SetSearchFlags(FindOption::MatchCase);
	CHECK(editor.GetSearchFlags() == FindOption::MatchCase);
	CHECK(static_cast<FindOption>(editor.WndProc(Message::GetSearchFlags, 0, 0))
		== FindOption::MatchCase);
}

TEST_CASE("ReplaceSel replaces the selection") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "abcdef");
	editor.SetSel(2, 4);
	const char *rep = "ZZ";
	editor.WndProc(Message::ReplaceSel, 0, reinterpret_cast<sptr_t>(rep));
	CHECK(editor.GetText() == "abZZef");
}

TEST_CASE("FindText messages are no longer dispatched") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "text");
	// Deleted structure-unpacking search messages fall through DefWndProc.
	const sptr_t r = editor.WndProc(Message::FindText, 0, 0);
	// DefWndProc returns 0 for unknown handling in the test host path.
	CHECK(r == 0);
}
