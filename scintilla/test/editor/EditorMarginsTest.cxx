// scalpel-editor test code
/** @file EditorMarginsTest.cxx
 ** Focused behavior tests for margin widths, sensitivity, text, and fold colours.
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

TEST_CASE("Margin width type mask and left gap round-trip with redraw") {
	TestHost host;
	TestEditor editor(host);

	const int defaultLeft = static_cast<int>(editor.WndProc(Message::GetMarginLeft, 0, 0));
	CHECK(defaultLeft >= 0);

	editor.ClearObservations();
	editor.WndProc(Message::SetMarginWidthN, 1, 24);
	CHECK(editor.WndProc(Message::GetMarginWidthN, 1, 0) == 24);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
	editor.PaintAll();

	// Same width must not request another full style redraw.
	editor.ClearObservations();
	editor.WndProc(Message::SetMarginWidthN, 1, 24);
	CHECK(editor.Snapshot().invalidatedRectangles == 0);

	editor.WndProc(Message::SetMarginTypeN, 1, static_cast<sptr_t>(MarginType::Number));
	CHECK(static_cast<MarginType>(editor.WndProc(Message::GetMarginTypeN, 1, 0)) == MarginType::Number);

	editor.WndProc(Message::SetMarginMaskN, 1, MaskFolders);
	CHECK(editor.WndProc(Message::GetMarginMaskN, 1, 0) == MaskFolders);

	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::SetMarginLeft, 0, 8);
	CHECK(editor.WndProc(Message::GetMarginLeft, 0, 0) == 8);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.WndProc(Message::SetMarginRight, 0, 6);
	CHECK(editor.WndProc(Message::GetMarginRight, 0, 0) == 6);
}

TEST_CASE("Margin sensitivity cursor back and count options") {
	TestHost host;
	TestEditor editor(host);

	CHECK(editor.WndProc(Message::GetMarginSensitiveN, 1, 0) == 0);
	editor.WndProc(Message::SetMarginSensitiveN, 1, 1);
	CHECK(editor.WndProc(Message::GetMarginSensitiveN, 1, 0) != 0);

	editor.WndProc(Message::SetMarginCursorN, 1, static_cast<sptr_t>(CursorShape::Arrow));
	CHECK(static_cast<CursorShape>(editor.WndProc(Message::GetMarginCursorN, 1, 0)) == CursorShape::Arrow);

	const int colour = 0x00FF00;
	editor.WndProc(Message::SetMarginTypeN, 1, static_cast<sptr_t>(MarginType::Colour));
	editor.WndProc(Message::SetMarginBackN, 1, colour);
	CHECK(editor.WndProc(Message::GetMarginBackN, 1, 0) == colour);

	const size_t before = static_cast<size_t>(editor.WndProc(Message::GetMargins, 0, 0));
	CHECK(before >= 1);
	editor.WndProc(Message::SetMargins, before + 1, 0);
	CHECK(static_cast<size_t>(editor.WndProc(Message::GetMargins, 0, 0)) == before + 1);

	editor.WndProc(Message::SetMarginOptions, static_cast<uptr_t>(MarginOption::SubLineSelect), 0);
	CHECK(static_cast<MarginOption>(editor.WndProc(Message::GetMarginOptions, 0, 0)) == MarginOption::SubLineSelect);
	editor.WndProc(Message::SetMarginOptions, static_cast<uptr_t>(MarginOption::None), 0);
	CHECK(static_cast<MarginOption>(editor.WndProc(Message::GetMarginOptions, 0, 0)) == MarginOption::None);

	// Out-of-range margin index: no effect on set, zero get.
	const uptr_t bad = static_cast<uptr_t>(before + 5);
	editor.WndProc(Message::SetMarginWidthN, bad, 99);
	CHECK(editor.WndProc(Message::GetMarginWidthN, bad, 0) == 0);
}

TEST_CASE("Margin text style offset clear and change notification") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("alpha\nbeta\ngamma\n");

	editor.WndProc(Message::SetMarginTypeN, 0, static_cast<sptr_t>(MarginType::Text));
	editor.WndProc(Message::SetMarginWidthN, 0, 40);

	editor.ClearObservations();
	const char *rev = "rev";
	editor.WndProc(Message::MarginSetText, 1, reinterpret_cast<sptr_t>(rev));

	char textBuf[16] = {};
	const sptr_t textLen = editor.WndProc(Message::MarginGetText, 1, reinterpret_cast<sptr_t>(textBuf));
	CHECK(textLen == 3);
	CHECK(std::string(textBuf, static_cast<size_t>(textLen)) == "rev");

	const bool sawMarginChange = std::any_of(
		editor.observations.notifications.begin(),
		editor.observations.notifications.end(),
		[](const TestNotification &n) {
			return n.code == Notification::Modified
				&& FlagSet(n.modificationType, ModificationFlags::ChangeMargin);
		});
	CHECK(sawMarginChange);

	editor.WndProc(Message::MarginSetStyle, 1, 5);
	CHECK(editor.WndProc(Message::MarginGetStyle, 1, 0) == 5);

	editor.WndProc(Message::MarginSetStyleOffset, 256, 0);
	CHECK(editor.WndProc(Message::MarginGetStyleOffset, 0, 0) == 256);

	const unsigned char styles[] = {1, 2, 3};
	editor.WndProc(Message::MarginSetStyles, 1, reinterpret_cast<sptr_t>(styles));
	char styleBuf[8] = {};
	const sptr_t styleLen = editor.WndProc(Message::MarginGetStyles, 1, reinterpret_cast<sptr_t>(styleBuf));
	REQUIRE(styleLen >= 3);
	CHECK(static_cast<unsigned char>(styleBuf[0]) == 1);
	CHECK(static_cast<unsigned char>(styleBuf[1]) == 2);
	CHECK(static_cast<unsigned char>(styleBuf[2]) == 3);

	// Single shared style (no per-character array): return 0 and clear the first output byte.
	editor.WndProc(Message::MarginSetStyle, 1, 7);
	styleBuf[0] = static_cast<char>(0x5A);
	styleBuf[1] = static_cast<char>(0x5A);
	const sptr_t singleLen = editor.WndProc(Message::MarginGetStyles, 1, reinterpret_cast<sptr_t>(styleBuf));
	CHECK(singleLen == 0);
	CHECK(styleBuf[0] == 0);
	CHECK(styleBuf[1] == static_cast<char>(0x5A));

	editor.WndProc(Message::MarginTextClearAll, 0, 0);
	const sptr_t clearedLen = editor.WndProc(Message::MarginGetText, 1, 0);
	CHECK(clearedLen == 0);
}

TEST_CASE("SetMargins shrink of a visible margin invalidates layout") {
	TestHost host;
	TestEditor editor(host);

	const size_t count = static_cast<size_t>(editor.WndProc(Message::GetMargins, 0, 0));
	REQUIRE(count >= 2);
	// Make the last allocated margin visible so shrinking drops real width.
	editor.WndProc(Message::SetMarginWidthN, count - 1, 20);
	editor.PaintAll();
	editor.ClearObservations();

	editor.WndProc(Message::SetMargins, count - 1, 0);
	CHECK(static_cast<size_t>(editor.WndProc(Message::GetMargins, 0, 0)) == count - 1);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	// Same count is a no-op for layout.
	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::SetMargins, count - 1, 0);
	CHECK(editor.Snapshot().invalidatedRectangles == 0);
}

TEST_CASE("Fold margin colours request style redraw") {
	TestHost host;
	TestEditor editor(host);

	editor.ClearObservations();
	editor.WndProc(Message::SetFoldMarginColour, 1, 0x112233);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
	editor.PaintAll();

	editor.ClearObservations();
	editor.WndProc(Message::SetFoldMarginHiColour, 1, 0x445566);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
	editor.PaintAll();

	// Clearing the override still invalidates so the platform default returns.
	editor.ClearObservations();
	editor.WndProc(Message::SetFoldMarginColour, 0, 0);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
}
