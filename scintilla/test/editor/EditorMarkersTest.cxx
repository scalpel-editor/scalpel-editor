// scalpel-editor test code
/** @file EditorMarkersTest.cxx
 ** Focused behavior tests for marker definitions, placement, handles, and search.
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

bool SawChangeMarker(const TestEditor &editor) {
	return std::any_of(
		editor.observations.notifications.begin(),
		editor.observations.notifications.end(),
		[](const TestNotification &n) {
			return n.code == Notification::Modified
				&& FlagSet(n.modificationType, ModificationFlags::ChangeMarker);
		});
}

// Minimal 1x1 red XPM in text form.
const char *kTinyXpm =
	"/* XPM */\n"
	"static char *xpm[] = {\n"
	"\"1 1 1 1\",\n"
	"\". c #FF0000\",\n"
	"\".\"};\n";

}  // namespace

TEST_CASE("Marker define symbol colour stroke layer and out-of-range") {
	TestHost host;
	TestEditor editor(host);

	// Default symbol is Circle.
	CHECK(static_cast<MarkerSymbol>(editor.WndProc(Message::MarkerSymbolDefined, 0, 0))
		== MarkerSymbol::Circle);

	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::MarkerDefine, 0, static_cast<sptr_t>(MarkerSymbol::Arrow));
	CHECK(static_cast<MarkerSymbol>(editor.WndProc(Message::MarkerSymbolDefined, 0, 0))
		== MarkerSymbol::Arrow);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.WndProc(Message::MarkerDefine, 1, static_cast<sptr_t>(MarkerSymbol::Bookmark));
	CHECK(static_cast<MarkerSymbol>(editor.WndProc(Message::MarkerSymbolDefined, 1, 0))
		== MarkerSymbol::Bookmark);

	editor.WndProc(Message::MarkerSetFore, 0, 0x0000FF);
	editor.WndProc(Message::MarkerSetBack, 0, 0x00FF00);
	editor.WndProc(Message::MarkerSetBackSelected, 0, 0xFF0000);
	editor.WndProc(Message::MarkerSetStrokeWidth, 0, 200);

	// Translucent colours accept packed colouralpha.
	editor.WndProc(Message::MarkerSetForeTranslucent, 2, 0x800000FF);
	editor.WndProc(Message::MarkerSetBackTranslucent, 2, 0x8000FF00);
	editor.WndProc(Message::MarkerSetBackSelectedTranslucent, 2, 0x80FF0000);

	editor.WndProc(Message::MarkerSetLayer, 0, static_cast<sptr_t>(Layer::OverText));
	CHECK(static_cast<Layer>(editor.WndProc(Message::MarkerGetLayer, 0, 0)) == Layer::OverText);

	editor.WndProc(Message::MarkerSetAlpha, 0, static_cast<sptr_t>(Alpha::NoAlpha));
	CHECK(static_cast<Layer>(editor.WndProc(Message::MarkerGetLayer, 0, 0)) == Layer::Base);

	editor.WndProc(Message::MarkerSetAlpha, 0, 128);
	CHECK(static_cast<Layer>(editor.WndProc(Message::MarkerGetLayer, 0, 0)) == Layer::OverText);

	// Out-of-range: no assignment, zero-ish get, still invalidates on define.
	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::MarkerDefine, MarkerMax + 1, static_cast<sptr_t>(MarkerSymbol::Plus));
	CHECK(editor.WndProc(Message::MarkerSymbolDefined, MarkerMax + 1, 0) == 0);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
	CHECK(editor.WndProc(Message::MarkerGetLayer, MarkerMax + 1, 0) == 0);
}

TEST_CASE("Marker add get delete search handles and notification") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("one\ntwo\nthree\nfour\n");

	editor.ClearObservations();
	const int handle0 = static_cast<int>(editor.WndProc(Message::MarkerAdd, 0, 3));
	REQUIRE(handle0 >= 0);
	CHECK(SawChangeMarker(editor));
	CHECK((editor.WndProc(Message::MarkerGet, 0, 0) & (1 << 3)) != 0);
	CHECK(editor.WndProc(Message::MarkerLineFromHandle, handle0, 0) == 0);

	const int handleMsg = static_cast<int>(editor.WndProc(Message::MarkerAdd, 1, 5));
	REQUIRE(handleMsg >= 0);
	CHECK(editor.WndProc(Message::MarkerGet, 1, 0) == (1 << 5));

	// AddSet places multiple bits at once.
	editor.WndProc(Message::MarkerAddSet, 2, (1 << 1) | (1 << 4));
	CHECK((editor.WndProc(Message::MarkerGet, 2, 0) & (1 << 1)) != 0);
	CHECK((editor.WndProc(Message::MarkerGet, 2, 0) & (1 << 4)) != 0);
	// Zero set is a no-op.
	editor.WndProc(Message::MarkerAddSet, 2, 0);

	// Handle / number from line.
	CHECK(editor.WndProc(Message::MarkerHandleFromLine, 0, 0) == handle0);
	CHECK(editor.WndProc(Message::MarkerNumberFromLine, 0, 0) == 3);
	CHECK(editor.WndProc(Message::MarkerHandleFromLine, 0, 99) == -1);
	CHECK(editor.WndProc(Message::MarkerNumberFromLine, 0, 99) == -1);

	// Next / previous by mask.
	const int mask5 = 1 << 5;
	CHECK(editor.WndProc(Message::MarkerNext, 0, mask5) == 1);
	CHECK(editor.WndProc(Message::MarkerNext, 2, mask5) == -1);
	CHECK(editor.WndProc(Message::MarkerPrevious, 3, mask5) == 1);
	CHECK(editor.WndProc(Message::MarkerPrevious, 0, mask5) == -1);

	// Delete one occurrence, then by handle.
	editor.WndProc(Message::MarkerDelete, 2, 1);
	CHECK((editor.WndProc(Message::MarkerGet, 2, 0) & (1 << 1)) == 0);
	CHECK((editor.WndProc(Message::MarkerGet, 2, 0) & (1 << 4)) != 0);

	editor.WndProc(Message::MarkerDeleteHandle, handle0, 0);
	CHECK(editor.WndProc(Message::MarkerLineFromHandle, handle0, 0) == -1);
	CHECK((editor.WndProc(Message::MarkerGet, 0, 0) & (1 << 3)) == 0);

	// Delete all of one number.
	editor.WndProc(Message::MarkerAdd, 0, 7);
	editor.WndProc(Message::MarkerAdd, 3, 7);
	editor.WndProc(Message::MarkerDeleteAll, 7, 0);
	CHECK((editor.WndProc(Message::MarkerGet, 0, 0) & (1 << 7)) == 0);
	CHECK((editor.WndProc(Message::MarkerGet, 3, 0) & (1 << 7)) == 0);

	// Delete all markers on a line via -1.
	editor.WndProc(Message::MarkerAdd, 1, 2);
	editor.WndProc(Message::MarkerDelete, 1, -1);
	CHECK(editor.WndProc(Message::MarkerGet, 1, 0) == 0);

	// Clear document markers.
	editor.WndProc(Message::MarkerAdd, 0, 0);
	editor.WndProc(Message::MarkerDeleteAll, static_cast<uptr_t>(-1), 0);
	CHECK(editor.WndProc(Message::MarkerGet, 0, 0) == 0);
	CHECK(editor.WndProc(Message::MarkerGet, 2, 0) == 0);
}

TEST_CASE("Marker enable highlight pixmap and rgba image") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("alpha\nbeta\n");

	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::MarkerEnableHighlight, 1, 0);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	// Pixmap definition.
	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::MarkerDefinePixmap, 4, reinterpret_cast<sptr_t>(kTinyXpm));
	CHECK(static_cast<MarkerSymbol>(editor.WndProc(Message::MarkerSymbolDefined, 4, 0))
		== MarkerSymbol::Pixmap);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	// RGBA image uses shared dimension setters.
	const unsigned char pixel[4] = {0x11, 0x22, 0x33, 0xFF};
	editor.WndProc(Message::RGBAImageSetWidth, 1, 0);
	editor.WndProc(Message::RGBAImageSetHeight, 1, 0);
	editor.WndProc(Message::RGBAImageSetScale, 100, 0);
	editor.WndProc(Message::MarkerDefineRGBAImage, 6, reinterpret_cast<sptr_t>(pixel));
	CHECK(static_cast<MarkerSymbol>(editor.WndProc(Message::MarkerSymbolDefined, 6, 0))
		== MarkerSymbol::RgbaImage);

	// Define + add: usable handle and expected bitset (one host at a time).
	editor.SetText("x\ny\n");
	editor.WndProc(Message::MarkerDefine, 8, static_cast<sptr_t>(MarkerSymbol::SmallRect));
	const int handle = static_cast<int>(editor.WndProc(Message::MarkerAdd, 1, 8));
	CHECK(handle >= 0);
	CHECK(editor.WndProc(Message::MarkerGet, 1, 0) == (1 << 8));
	CHECK(editor.WndProc(Message::MarkerLineFromHandle, handle, 0) == 1);
	CHECK(static_cast<MarkerSymbol>(editor.WndProc(Message::MarkerSymbolDefined, 8, 0))
		== MarkerSymbol::SmallRect);
}
