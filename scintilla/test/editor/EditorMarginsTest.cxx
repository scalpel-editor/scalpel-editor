// scalpel-editor test code
/** @file EditorMarginsTest.cxx
 ** Focused behavior tests for margin widths, sensitivity, text, and fold colours.
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

TEST_CASE("Margin width type mask and left gap round-trip with redraw") {
	TestHost host;
	TestEditor editor(host);

	const int defaultLeft = editor.GetMarginLeft();
	CHECK(defaultLeft >= 0);

	editor.ClearObservations();
	editor.SetMarginWidthN(1, 24);
	CHECK(editor.GetMarginWidthN(1) == 24);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
	editor.PaintAll();

	// Same width must not request another full style redraw.
	editor.ClearObservations();
	editor.SetMarginWidthN(1, 24);
	CHECK(editor.Snapshot().invalidatedRectangles == 0);

	editor.SetMarginTypeN(1, MarginType::Number);
	CHECK(editor.GetMarginTypeN(1) == MarginType::Number);

	editor.SetMarginMaskN(1, MaskFolders);
	CHECK(editor.GetMarginMaskN(1) == MaskFolders);

	editor.PaintAll();
	editor.ClearObservations();
	editor.SetMarginLeft(8);
	CHECK(editor.GetMarginLeft() == 8);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.SetMarginRight(6);
	CHECK(editor.GetMarginRight() == 6);
}

TEST_CASE("Margin sensitivity cursor back and count options") {
	TestHost host;
	TestEditor editor(host);

	CHECK_FALSE(editor.GetMarginSensitiveN(1));
	editor.SetMarginSensitiveN(1, true);
	CHECK(editor.GetMarginSensitiveN(1));

	editor.SetMarginCursorN(1, CursorShape::Arrow);
	CHECK(editor.GetMarginCursorN(1) == CursorShape::Arrow);

	const int colour = 0x00FF00;
	editor.SetMarginTypeN(1, MarginType::Colour);
	editor.SetMarginBackN(1, colour);
	CHECK(editor.GetMarginBackN(1) == colour);

	const size_t before = editor.GetMargins();
	CHECK(before >= 1);
	editor.SetMargins(before + 1);
	CHECK(editor.GetMargins() == before + 1);

	editor.SetMarginOptions(MarginOption::SubLineSelect);
	CHECK(editor.GetMarginOptions() == MarginOption::SubLineSelect);
	editor.SetMarginOptions(MarginOption::None);
	CHECK(editor.GetMarginOptions() == MarginOption::None);

	// Out-of-range margin index: no effect on set, zero get.
	const size_t bad = before + 5;
	editor.SetMarginWidthN(bad, 99);
	CHECK(editor.GetMarginWidthN(bad) == 0);
}

TEST_CASE("Margin text style offset clear and change notification") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("alpha\nbeta\ngamma\n");

	editor.SetMarginTypeN(0, MarginType::Text);
	editor.SetMarginWidthN(0, 40);

	editor.ClearObservations();
	editor.MarginSetText(1, "rev");

	const std::string marginText = editor.MarginGetText(1);
	CHECK(marginText == "rev");

	const bool sawMarginChange = std::any_of(
		editor.observations.notifications.begin(),
		editor.observations.notifications.end(),
		[](const TestNotification &n) {
			return n.code == Notification::Modified
				&& FlagSet(n.modificationType, ModificationFlags::ChangeMargin);
		});
	CHECK(sawMarginChange);

	editor.MarginSetStyle(1, 5);
	CHECK(editor.MarginGetStyle(1) == 5);

	editor.MarginSetStyleOffset(256);
	CHECK(editor.MarginGetStyleOffset() == 256);

	const unsigned char styles[] = {1, 2, 3};
	editor.MarginSetStyles(1, styles);
	char styleBuf[8] = {};
	const Sci::Position styleLen = editor.MarginGetStyles(1, styleBuf);
	REQUIRE(styleLen >= 3);
	CHECK(static_cast<unsigned char>(styleBuf[0]) == 1);
	CHECK(static_cast<unsigned char>(styleBuf[1]) == 2);
	CHECK(static_cast<unsigned char>(styleBuf[2]) == 3);

	// Single shared style (no per-character array): return 0 and clear the first output byte.
	editor.MarginSetStyle(1, 7);
	styleBuf[0] = static_cast<char>(0x5A);
	styleBuf[1] = static_cast<char>(0x5A);
	const Sci::Position singleLen = editor.MarginGetStyles(1, styleBuf);
	CHECK(singleLen == 0);
	CHECK(styleBuf[0] == 0);
	CHECK(styleBuf[1] == static_cast<char>(0x5A));

	editor.MarginTextClearAll();
	CHECK(editor.MarginGetText(1).empty());
}

TEST_CASE("SetMargins shrink of a visible margin invalidates layout") {
	TestHost host;
	TestEditor editor(host);

	const size_t count = editor.GetMargins();
	REQUIRE(count >= 2);
	// Make the last allocated margin visible so shrinking drops real width.
	editor.SetMarginWidthN(count - 1, 20);
	editor.PaintAll();
	editor.ClearObservations();

	editor.SetMargins(count - 1);
	CHECK(editor.GetMargins() == count - 1);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	// Same count is a no-op for layout.
	editor.PaintAll();
	editor.ClearObservations();
	editor.SetMargins(count - 1);
	CHECK(editor.Snapshot().invalidatedRectangles == 0);
}

TEST_CASE("Fold margin colours request style redraw") {
	TestHost host;
	TestEditor editor(host);

	editor.ClearObservations();
	editor.SetFoldMarginColour(true, 0x112233);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
	editor.PaintAll();

	editor.ClearObservations();
	editor.SetFoldMarginHiColour(true, 0x445566);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
	editor.PaintAll();

	// Clearing the override still invalidates so the platform default returns.
	editor.ClearObservations();
	editor.SetFoldMarginColour(false, 0);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
}
