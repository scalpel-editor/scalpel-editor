// scalpel-editor test code
/** @file EditorScrollingTest.cxx
 ** Focused behavior tests for view scrolling and scrollbar options.
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

TEST_CASE("X offset and first-visible-line round-trip") {
	TestHost host;
	TestEditor editor(host, PRectangle(0, 0, 160, 40));
	LoadClean(editor,
		"line 00\nline 01\nline 02\nline 03\nline 04\nline 05\n"
		"line 06\nline 07\nline 08\nline 09\nline 10\nline 11\n"
		"line 12\nline 13\nline 14\nline 15\nline 16\nline 17\n");
	CHECK(editor.GetFirstVisibleLine() == 0);

	editor.SetXOffset(12);
	CHECK(editor.GetXOffset() == 12);
	editor.SetXOffset(0);
	CHECK(editor.GetXOffset() == 0);

	// Named application scroll operations move an observable view origin.
	editor.SetFirstVisibleLine(3);
	CHECK(editor.GetFirstVisibleLine() == 3);
	editor.LineScroll(2, 2);
	CHECK(editor.GetFirstVisibleLine() == 5);
	CHECK(editor.GetXOffset() > 0);
	editor.ScrollVertical(8, 0);
	CHECK(editor.GetFirstVisibleLine() == 8);
}

TEST_CASE("Scroll width and end-at-last-line options") {
	TestHost host;
	TestEditor editor(host);
	editor.SetScrollWidth(400);
	CHECK(editor.GetScrollWidth() == 400);
	editor.SetScrollWidthTracking(true);
	CHECK(editor.GetScrollWidthTracking());
	editor.SetEndAtLastLine(false);
	CHECK_FALSE(editor.GetEndAtLastLine());
}

TEST_CASE("Scroll bar visibility options round-trip") {
	TestHost host;
	TestEditor editor(host);
	editor.SetHScrollBar(false);
	CHECK_FALSE(editor.GetHScrollBar());
	editor.SetVScrollBar(false);
	CHECK_FALSE(editor.GetVScrollBar());
	editor.SetHScrollBar(true);
	editor.SetVScrollBar(true);
	CHECK(editor.GetHScrollBar());
	CHECK(editor.GetVScrollBar());
}

TEST_CASE("TextHeight reports a positive line height") {
	TestHost host;
	TestEditor editor(host);
	CHECK(editor.TextHeightPixels() > 0);
}
