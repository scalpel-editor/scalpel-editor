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
	CHECK(static_cast<MarkerSymbol>(editor.MarkerSymbolDefined(0))
		== MarkerSymbol::Circle);

	editor.PaintAll();
	editor.ClearObservations();
	editor.MarkerDefine(0, MarkerSymbol::Arrow);
	CHECK(static_cast<MarkerSymbol>(editor.MarkerSymbolDefined(0))
		== MarkerSymbol::Arrow);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.MarkerDefine(1, MarkerSymbol::Bookmark);
	CHECK(static_cast<MarkerSymbol>(editor.MarkerSymbolDefined(1))
		== MarkerSymbol::Bookmark);

	editor.MarkerSetFore(0, 0x0000FF);
	editor.MarkerSetBack(0, 0x00FF00);
	editor.MarkerSetBackSelected(0, 0xFF0000);
	editor.MarkerSetStrokeWidth(0, 200);

	// Translucent colours accept packed colouralpha.
	editor.MarkerSetForeTranslucent(2, 0x800000FF);
	editor.MarkerSetBackTranslucent(2, 0x8000FF00);
	editor.MarkerSetBackSelectedTranslucent(2, 0x80FF0000);

	editor.MarkerSetLayer(0, Layer::OverText);
	CHECK(static_cast<Layer>(editor.MarkerGetLayer(0)) == Layer::OverText);

	editor.MarkerSetAlpha(0, Alpha::NoAlpha);
	CHECK(static_cast<Layer>(editor.MarkerGetLayer(0)) == Layer::Base);

	editor.MarkerSetAlpha(0, static_cast<Alpha>(128));
	CHECK(static_cast<Layer>(editor.MarkerGetLayer(0)) == Layer::OverText);

	// Out-of-range: no assignment, zero-ish get, still invalidates on define.
	editor.PaintAll();
	editor.ClearObservations();
	editor.MarkerDefine(MarkerMax + 1, MarkerSymbol::Plus);
	CHECK(static_cast<int>(editor.MarkerSymbolDefined(MarkerMax + 1)) == 0);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
	CHECK(static_cast<int>(editor.MarkerGetLayer(MarkerMax + 1)) == 0);

	// Values above UINT32_MAX must not wrap into marker 0 when cast through int.
	// 0x1'0000'0000 is 2^32; on 64-bit size_t this is a distinct large index.
	if constexpr (sizeof(size_t) > 4) {
		constexpr size_t huge = static_cast<size_t>(UINT32_MAX) + 1u;
		REQUIRE(huge > static_cast<size_t>(MarkerMax));
		// Marker 0 is Arrow from above; a wrapped int cast of huge would be 0.
		editor.MarkerDefine(0, MarkerSymbol::Arrow);
		editor.MarkerSetLayer(0, Layer::Base);
		editor.MarkerSetFore(0, 0x0000FF);

		editor.PaintAll();
		editor.ClearObservations();
		editor.MarkerDefine(huge, MarkerSymbol::Plus);
		editor.MarkerSetLayer(huge, Layer::OverText);
		editor.MarkerSetFore(huge, 0x00FF00);
		editor.MarkerDefinePixmap(huge, kTinyXpm);

		CHECK(static_cast<MarkerSymbol>(editor.MarkerSymbolDefined(0))
			== MarkerSymbol::Arrow);
		CHECK(static_cast<Layer>(editor.MarkerGetLayer(0)) == Layer::Base);
		CHECK(static_cast<int>(editor.MarkerSymbolDefined(huge)) == 0);
		CHECK(static_cast<int>(editor.MarkerGetLayer(huge)) == 0);
		// Historical out-of-range define still refreshes style data.
		CHECK(editor.Snapshot().invalidatedRectangles > 0);
	}
}

TEST_CASE("Marker add get delete search handles and notification") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("one\ntwo\nthree\nfour\n");

	editor.ClearObservations();
	const int handle0 = editor.MarkerAdd(0, 3);
	REQUIRE(handle0 >= 0);
	CHECK(SawChangeMarker(editor));
	CHECK((editor.MarkerGet(0) & (1 << 3)) != 0);
	CHECK(editor.MarkerLineFromHandle(handle0) == 0);

	const int handleMsg = editor.MarkerAdd(1, 5);
	REQUIRE(handleMsg >= 0);
	CHECK(editor.MarkerGet(1) == (1 << 5));

	// AddSet places multiple bits at once.
	editor.MarkerAddSet(2, (1 << 1) | (1 << 4));
	CHECK((editor.MarkerGet(2) & (1 << 1)) != 0);
	CHECK((editor.MarkerGet(2) & (1 << 4)) != 0);
	// Zero set is a no-op.
	editor.MarkerAddSet(2, 0);

	// Handle / number from line.
	CHECK(editor.MarkerHandleFromLine(0, 0) == handle0);
	CHECK(editor.MarkerNumberFromLine(0, 0) == 3);
	CHECK(editor.MarkerHandleFromLine(0, 99) == -1);
	CHECK(editor.MarkerNumberFromLine(0, 99) == -1);

	// Next / previous by mask.
	const int mask5 = 1 << 5;
	CHECK(editor.MarkerNext(0, mask5) == 1);
	CHECK(editor.MarkerNext(2, mask5) == -1);
	CHECK(editor.MarkerPrevious(3, mask5) == 1);
	CHECK(editor.MarkerPrevious(0, mask5) == -1);

	// Delete one occurrence, then by handle.
	editor.MarkerDelete(2, 1);
	CHECK((editor.MarkerGet(2) & (1 << 1)) == 0);
	CHECK((editor.MarkerGet(2) & (1 << 4)) != 0);

	editor.MarkerDeleteHandle(static_cast<int>(handle0));
	CHECK(editor.MarkerLineFromHandle(handle0) == -1);
	CHECK((editor.MarkerGet(0) & (1 << 3)) == 0);

	// Delete all of one number.
	editor.MarkerAdd(0, 7);
	editor.MarkerAdd(3, 7);
	editor.MarkerDeleteAll(static_cast<int>(7));
	CHECK((editor.MarkerGet(0) & (1 << 7)) == 0);
	CHECK((editor.MarkerGet(3) & (1 << 7)) == 0);

	// Delete all markers on a line via -1.
	editor.MarkerAdd(1, 2);
	editor.MarkerDelete(1, -1);
	CHECK(editor.MarkerGet(1) == 0);

	// Clear document markers.
	editor.MarkerAdd(0, 0);
	editor.MarkerDeleteAll(-1);
	CHECK(editor.MarkerGet(0) == 0);
	CHECK(editor.MarkerGet(2) == 0);
}

TEST_CASE("Marker enable highlight pixmap and rgba image") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("alpha\nbeta\n");

	editor.PaintAll();
	editor.ClearObservations();
	editor.MarkerEnableHighlight(true);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	// Pixmap definition.
	editor.PaintAll();
	editor.ClearObservations();
	editor.MarkerDefinePixmap(4, kTinyXpm);
	CHECK(static_cast<MarkerSymbol>(editor.MarkerSymbolDefined(4))
		== MarkerSymbol::Pixmap);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	// RGBA image uses shared dimension setters.
	const unsigned char pixel[4] = {0x11, 0x22, 0x33, 0xFF};
	editor.RGBAImageSetWidth(1);
	editor.RGBAImageSetHeight(1);
	editor.RGBAImageSetScale(100);
	editor.MarkerDefineRGBAImage(6, pixel);
	CHECK(static_cast<MarkerSymbol>(editor.MarkerSymbolDefined(6))
		== MarkerSymbol::RgbaImage);

	// Define + add: usable handle and expected bitset (one host at a time).
	editor.SetText("x\ny\n");
	editor.MarkerDefine(8, MarkerSymbol::SmallRect);
	const int handle = editor.MarkerAdd(1, 8);
	CHECK(handle >= 0);
	CHECK(editor.MarkerGet(1) == (1 << 8));
	CHECK(editor.MarkerLineFromHandle(handle) == 1);
	CHECK(static_cast<MarkerSymbol>(editor.MarkerSymbolDefined(8))
		== MarkerSymbol::SmallRect);
}
