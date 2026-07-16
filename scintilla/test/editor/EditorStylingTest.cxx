// scalpel-editor test code
/** @file EditorStylingTest.cxx
 ** Focused behavior tests for styles, elements, whitespace view, zoom, and edges.
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

TEST_CASE("Style definition round-trip and clear redraw") {
	TestHost host;
	TestEditor editor(host);

	editor.PaintAll();
	editor.ClearObservations();
	editor.StyleSetFore(1, 0x0000FF);
	CHECK(editor.StyleGetFore(1) == 0x0000FF);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.StyleSetBack(1, 0x00FF00);
	CHECK(editor.StyleGetBack(1) == 0x00FF00);
	editor.StyleSetBold(1, true);
	CHECK(editor.StyleGetBold(1) != 0);
	editor.StyleSetSize(1, 14);
	CHECK(editor.StyleGetSize(1) == 14);
	editor.StyleSetFont(1, "Courier");
	char font[32] = {};
	const int n = editor.StyleGetFont(1, font);
	CHECK(n == 7);
	CHECK(std::string(font) == "Courier");

	editor.PaintAll();
	editor.ClearObservations();
	editor.StyleClearAll();
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
}

TEST_CASE("Start styling SetStyling GetStyleAt end-styled and failure") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("abcdef");

	editor.StartStyling(0);
	editor.SetStyling(3, 5);
	CHECK(editor.GetStyleAt(0) == 5);
	CHECK(editor.GetStyleAt(2) == 5);
	CHECK(editor.GetStyleIndexAt(1) == 5);
	CHECK(editor.GetEndStyled() >= 3);

	// Negative length is rejected.
	editor.SetStyling(-1, 1);
	// Past-end style query returns 0.
	CHECK(editor.GetStyleAt(100) == 0);

	const char styles[] = {7, 7, 7};
	editor.StartStyling(3);
	editor.SetStylingEx(3, styles);
	CHECK(editor.GetStyleAt(3) == 7);
	CHECK(editor.GetStyleAt(5) == 7);

	// Full style bytes: values above the old 5-bit style partition (0..31) still store and read back.
	constexpr int styleAboveOldBits = 40;
	editor.StartStyling(0);
	editor.SetStyling(2, styleAboveOldBits);
	CHECK(editor.GetStyleAt(0) == styleAboveOldBits);
	CHECK(editor.GetStyleIndexAt(1) == styleAboveOldBits);
	editor.StyleSetFore(styleAboveOldBits, 0x00C0FF);
	CHECK(editor.StyleGetFore(styleAboveOldBits) == 0x00C0FF);
}

TEST_CASE("SetBidirectional stores mode and GetBidirectional reports it") {
	TestHost host;
	TestEditor editor(host);

	// Default off. Do not paint while bidi is enabled: the test surface has no
	// screen-line layout implementation, and full bidi layout is out of roadmap scope.
	CHECK(static_cast<Bidirectional>(editor.GetBidirectional()) == Bidirectional::Disabled);

	editor.ClearObservations();
	editor.SetBidirectional(Bidirectional::L2R);
	CHECK(static_cast<Bidirectional>(editor.GetBidirectional()) == Bidirectional::L2R);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.SetBidirectional(Bidirectional::R2L);
	CHECK(static_cast<Bidirectional>(editor.GetBidirectional()) == Bidirectional::R2L);

	// Unchanged mode skips invalidation.
	editor.ClearObservations();
	editor.SetBidirectional(Bidirectional::R2L);
	CHECK(editor.Snapshot().invalidatedRectangles == 0);

	editor.SetBidirectional(Bidirectional::Disabled);
	CHECK(static_cast<Bidirectional>(editor.GetBidirectional()) == Bidirectional::Disabled);
}

TEST_CASE("ClearDocumentStyle resets styles without clearing text") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("xyz");
	editor.StartStyling(0);
	editor.SetStyling(3, 4);
	CHECK(editor.GetStyleAt(0) == 4);

	editor.ClearDocumentStyle();
	CHECK(editor.GetText() == "xyz");
	CHECK(editor.GetStyleAt(0) == 0);
	CHECK(editor.GetStyleAt(2) == 0);
}

TEST_CASE("View whitespace whitespace size and selection colours redraw") {
	TestHost host;
	TestEditor editor(host);
	editor.PaintAll();

	editor.ClearObservations();
	editor.SetViewWS(WhiteSpace::VisibleAlways);
	CHECK(static_cast<WhiteSpace>(editor.GetViewWS()) == WhiteSpace::VisibleAlways);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.PaintAll();
	editor.ClearObservations();
	editor.SetWhitespaceSize(static_cast<int>(3));
	CHECK(editor.GetWhitespaceSize() == 3);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.PaintAll();
	editor.ClearObservations();
	editor.SetSelFore(true, 0x112233);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
	editor.SetSelBack(true, 0x445566);
	editor.SetSelAlpha(static_cast<int>(128));
	CHECK(editor.GetSelAlpha() == 128);
}

TEST_CASE("Element colours idle styling layout cache and phases") {
	TestHost host;
	TestEditor editor(host);

	editor.PaintAll();
	editor.ClearObservations();
	editor.SetElementColour(Element::Caret, 0xFF0000FF);
	CHECK(editor.GetElementIsSet(Element::Caret) != 0);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
	editor.ResetElementColour(Element::Caret);
	CHECK(editor.GetElementIsSet(Element::Caret) == 0);

	editor.SetIdleStyling(IdleStyling::ToVisible);
	CHECK(static_cast<IdleStyling>(editor.GetIdleStyling()) == IdleStyling::ToVisible);

	editor.SetLayoutCache(LineCache::Page);
	CHECK(static_cast<LineCache>(editor.GetLayoutCache()) == LineCache::Page);

	editor.SetPositionCache(static_cast<int>(1024));
	CHECK(editor.GetPositionCache() == 1024);

	editor.PaintAll();
	editor.ClearObservations();
	editor.SetPhasesDraw(static_cast<int>(2));
	CHECK(editor.GetPhasesDraw() == 2);

	editor.PaintAll();
	editor.ClearObservations();
	editor.SetExtraAscent(static_cast<int>(2));
	CHECK(editor.GetExtraAscent() == 2);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
	editor.SetExtraDescent(static_cast<int>(3));
	CHECK(editor.GetExtraDescent() == 3);
}

TEST_CASE("Zoom edge mode multi-edge highlight guide and extended styles") {
	TestHost host;
	TestEditor editor(host);

	editor.PaintAll();
	editor.ClearObservations();
	editor.SetZoom(static_cast<int>(4));
	CHECK(editor.GetZoom() == 4);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
	// Same zoom should not re-invalidate via SetAppearance.
	editor.PaintAll();
	editor.ClearObservations();
	editor.SetZoom(static_cast<int>(4));
	CHECK(editor.Snapshot().invalidatedRectangles == 0);

	editor.SetEdgeMode(EdgeVisualStyle::Line);
	CHECK(static_cast<EdgeVisualStyle>(editor.GetEdgeMode()) == EdgeVisualStyle::Line);
	editor.SetEdgeColour(static_cast<int>(0x00AABB));
	CHECK(editor.GetEdgeColour() == 0x00AABB);

	editor.MultiEdgeAddLine(40, 0x112233);
	editor.MultiEdgeAddLine(80, 0x445566);
	CHECK(editor.GetMultiEdgeColumn(0) == 40);
	CHECK(editor.GetMultiEdgeColumn(1) == 80);
	editor.MultiEdgeClearAll();
	CHECK(editor.GetMultiEdgeColumn(0) < 0);

	editor.PaintAll();
	editor.ClearObservations();
	editor.SetHighlightGuide(static_cast<int>(4));
	CHECK(editor.GetHighlightGuide() == 4);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	const int first = editor.AllocateExtendedStyles(static_cast<int>(8));
	CHECK(first >= 256);
	editor.ReleaseAllExtendedStyles();
	const int again = editor.AllocateExtendedStyles(static_cast<int>(8));
	CHECK(again == first);
}

TEST_CASE("Text width preserves embedded NUL bytes and zoom round-trips") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("MMMM");
	editor.StyleSetFore(0, 0x010203);
	CHECK(editor.StyleGetFore(0) == 0x010203);

	const long w = editor.TextWidth(0, "MM");
	CHECK(w > 0);
	const std::string_view textWithNul("M\0M", 3);
	CHECK(editor.TextWidth(0, textWithNul) == 3 * editor.TextWidth(0, "M"));

	editor.SetZoom(6);
	CHECK(editor.GetZoom() == 6);

	editor.SetZoom(static_cast<int>(2));
	CHECK(editor.GetZoom() == 2);
}
