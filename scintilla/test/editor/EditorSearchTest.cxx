// scalpel-editor test code
/** @file EditorSearchTest.cxx
 ** Focused behavior tests for target search and replace.
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

TEST_CASE("Target range and SearchInTarget find text") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "hello world hello");

	editor.SetTargetStart(0);
	editor.SetTargetEnd(editor.GetTextLength());
	CHECK(editor.GetTargetStart() == 0);
	CHECK(editor.GetTargetEnd() == editor.GetTextLength());

	const Sci::Position pos = editor.SearchInTarget("world");
	CHECK(pos == 6);
	CHECK(editor.GetTargetStart() == 6);
	CHECK(editor.GetTargetEnd() == 11);
	CHECK(editor.GetTargetText() == "world");
}

TEST_CASE("SearchInTarget finds the needle after TargetWholeDocument") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "abc def abc");
	editor.TargetWholeDocument();
	const Sci::Position pos = editor.SearchInTarget("def");
	CHECK(pos == 4);
	CHECK(editor.GetTargetStart() == 4);
	CHECK(editor.GetTargetEnd() == 7);
}

TEST_CASE("ReplaceTarget replaces the target range") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "one two three");
	editor.SetTargetRange(4, 7);
	CHECK(editor.GetTargetText() == "two");
	CHECK(editor.ReplaceTargetBasic("2") == 1);
	CHECK(editor.GetText() == "one 2 three");
	editor.SetTargetRange(4, 5);
	CHECK(editor.ReplaceTargetBasic("two") == 3);
	CHECK(editor.GetText() == "one two three");
}

TEST_CASE("Search flags round-trip") {
	TestHost host;
	TestEditor editor(host);
	editor.SetSearchFlags(FindOption::MatchCase);
	CHECK(editor.GetSearchFlags() == FindOption::MatchCase);
	editor.SetSearchFlags(FindOption::None);
	CHECK(editor.GetSearchFlags() == FindOption::None);
}

TEST_CASE("ReplaceSel replaces the selection") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "abcdef");
	editor.SetSel(2, 4);
	editor.ReplaceSel("ZZ");
	CHECK(editor.GetText() == "abZZef");
	editor.SetSel(2, 4);
	editor.ReplaceSel("cd");
	CHECK(editor.GetText() == "abcdef");
}
