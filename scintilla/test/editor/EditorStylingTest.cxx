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

TEST_CASE("Style definition round-trip and clear redraw") {
	TestHost host;
	TestEditor editor(host);

	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::StyleSetFore, 1, 0x0000FF);
	CHECK(editor.WndProc(Message::StyleGetFore, 1, 0) == 0x0000FF);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.WndProc(Message::StyleSetBack, 1, 0x00FF00);
	CHECK(editor.WndProc(Message::StyleGetBack, 1, 0) == 0x00FF00);
	editor.WndProc(Message::StyleSetBold, 1, 1);
	CHECK(editor.WndProc(Message::StyleGetBold, 1, 0) != 0);
	editor.WndProc(Message::StyleSetSize, 1, 14);
	CHECK(editor.WndProc(Message::StyleGetSize, 1, 0) == 14);
	editor.WndProc(Message::StyleSetFont, 1, reinterpret_cast<sptr_t>("Courier"));
	char font[32] = {};
	const sptr_t n = editor.WndProc(Message::StyleGetFont, 1, reinterpret_cast<sptr_t>(font));
	CHECK(n == 7);
	CHECK(std::string(font) == "Courier");

	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::StyleClearAll, 0, 0);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
}

TEST_CASE("Start styling SetStyling GetStyleAt end-styled and failure") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("abcdef");

	editor.WndProc(Message::StartStyling, 0, 0);
	editor.WndProc(Message::SetStyling, 3, 5);
	CHECK(editor.WndProc(Message::GetStyleAt, 0, 0) == 5);
	CHECK(editor.WndProc(Message::GetStyleAt, 2, 0) == 5);
	CHECK(editor.WndProc(Message::GetStyleIndexAt, 1, 0) == 5);
	CHECK(editor.WndProc(Message::GetEndStyled, 0, 0) >= 3);

	// Negative length is rejected.
	editor.WndProc(Message::SetStyling, static_cast<uptr_t>(-1), 1);
	// Past-end style query returns 0.
	CHECK(editor.WndProc(Message::GetStyleAt, 100, 0) == 0);

	const char styles[] = {7, 7, 7};
	editor.WndProc(Message::StartStyling, 3, 0);
	editor.WndProc(Message::SetStylingEx, 3, reinterpret_cast<sptr_t>(styles));
	CHECK(editor.WndProc(Message::GetStyleAt, 3, 0) == 7);
	CHECK(editor.WndProc(Message::GetStyleAt, 5, 0) == 7);

	// Full style bytes: values above the old 5-bit style partition (0..31) still store and read back.
	constexpr int styleAboveOldBits = 40;
	editor.WndProc(Message::StartStyling, 0, 0);
	editor.WndProc(Message::SetStyling, 2, styleAboveOldBits);
	CHECK(editor.WndProc(Message::GetStyleAt, 0, 0) == styleAboveOldBits);
	CHECK(editor.WndProc(Message::GetStyleIndexAt, 1, 0) == styleAboveOldBits);
	editor.WndProc(Message::StyleSetFore, styleAboveOldBits, 0x00C0FF);
	CHECK(editor.WndProc(Message::StyleGetFore, styleAboveOldBits, 0) == 0x00C0FF);
}

TEST_CASE("SetBidirectional stores mode and GetBidirectional reports it") {
	TestHost host;
	TestEditor editor(host);

	// Default off. Do not paint while bidi is enabled: the test surface has no
	// screen-line layout implementation, and full bidi layout is out of roadmap scope.
	CHECK(static_cast<Bidirectional>(editor.WndProc(Message::GetBidirectional, 0, 0)) == Bidirectional::Disabled);

	editor.ClearObservations();
	editor.WndProc(Message::SetBidirectional, static_cast<uptr_t>(Bidirectional::L2R), 0);
	CHECK(static_cast<Bidirectional>(editor.WndProc(Message::GetBidirectional, 0, 0)) == Bidirectional::L2R);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.WndProc(Message::SetBidirectional, static_cast<uptr_t>(Bidirectional::R2L), 0);
	CHECK(static_cast<Bidirectional>(editor.WndProc(Message::GetBidirectional, 0, 0)) == Bidirectional::R2L);

	// Unchanged mode skips invalidation.
	editor.ClearObservations();
	editor.WndProc(Message::SetBidirectional, static_cast<uptr_t>(Bidirectional::R2L), 0);
	CHECK(editor.Snapshot().invalidatedRectangles == 0);

	editor.WndProc(Message::SetBidirectional, static_cast<uptr_t>(Bidirectional::Disabled), 0);
	CHECK(static_cast<Bidirectional>(editor.WndProc(Message::GetBidirectional, 0, 0)) == Bidirectional::Disabled);
}

TEST_CASE("ClearDocumentStyle resets styles without clearing text") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("xyz");
	editor.WndProc(Message::StartStyling, 0, 0);
	editor.WndProc(Message::SetStyling, 3, 4);
	CHECK(editor.WndProc(Message::GetStyleAt, 0, 0) == 4);

	editor.WndProc(Message::ClearDocumentStyle, 0, 0);
	CHECK(editor.GetText() == "xyz");
	CHECK(editor.WndProc(Message::GetStyleAt, 0, 0) == 0);
	CHECK(editor.WndProc(Message::GetStyleAt, 2, 0) == 0);
}

TEST_CASE("View whitespace whitespace size and selection colours redraw") {
	TestHost host;
	TestEditor editor(host);
	editor.PaintAll();

	editor.ClearObservations();
	editor.WndProc(Message::SetViewWS, static_cast<uptr_t>(WhiteSpace::VisibleAlways), 0);
	CHECK(static_cast<WhiteSpace>(editor.WndProc(Message::GetViewWS, 0, 0)) == WhiteSpace::VisibleAlways);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::SetWhitespaceSize, 3, 0);
	CHECK(editor.WndProc(Message::GetWhitespaceSize, 0, 0) == 3);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::SetSelFore, 1, 0x112233);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
	editor.WndProc(Message::SetSelBack, 1, 0x445566);
	editor.WndProc(Message::SetSelAlpha, 128, 0);
	CHECK(editor.WndProc(Message::GetSelAlpha, 0, 0) == 128);
}

TEST_CASE("Element colours idle styling layout cache and phases") {
	TestHost host;
	TestEditor editor(host);

	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::SetElementColour, static_cast<uptr_t>(Element::Caret), 0xFF0000FF);
	CHECK(editor.WndProc(Message::GetElementIsSet, static_cast<uptr_t>(Element::Caret), 0) != 0);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
	editor.WndProc(Message::ResetElementColour, static_cast<uptr_t>(Element::Caret), 0);
	CHECK(editor.WndProc(Message::GetElementIsSet, static_cast<uptr_t>(Element::Caret), 0) == 0);

	editor.WndProc(Message::SetIdleStyling, static_cast<uptr_t>(IdleStyling::ToVisible), 0);
	CHECK(static_cast<IdleStyling>(editor.WndProc(Message::GetIdleStyling, 0, 0)) == IdleStyling::ToVisible);

	editor.WndProc(Message::SetLayoutCache, static_cast<uptr_t>(LineCache::Page), 0);
	CHECK(static_cast<LineCache>(editor.WndProc(Message::GetLayoutCache, 0, 0)) == LineCache::Page);

	editor.WndProc(Message::SetPositionCache, 1024, 0);
	CHECK(editor.WndProc(Message::GetPositionCache, 0, 0) == 1024);

	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::SetPhasesDraw, 2, 0);
	CHECK(editor.WndProc(Message::GetPhasesDraw, 0, 0) == 2);

	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::SetExtraAscent, 2, 0);
	CHECK(editor.WndProc(Message::GetExtraAscent, 0, 0) == 2);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
	editor.WndProc(Message::SetExtraDescent, 3, 0);
	CHECK(editor.WndProc(Message::GetExtraDescent, 0, 0) == 3);
}

TEST_CASE("Zoom edge mode multi-edge highlight guide and extended styles") {
	TestHost host;
	TestEditor editor(host);

	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::SetZoom, 4, 0);
	CHECK(editor.WndProc(Message::GetZoom, 0, 0) == 4);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
	// Same zoom should not re-invalidate via SetAppearance.
	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::SetZoom, 4, 0);
	CHECK(editor.Snapshot().invalidatedRectangles == 0);

	editor.WndProc(Message::SetEdgeMode, static_cast<uptr_t>(EdgeVisualStyle::Line), 0);
	CHECK(static_cast<EdgeVisualStyle>(editor.WndProc(Message::GetEdgeMode, 0, 0)) == EdgeVisualStyle::Line);
	editor.WndProc(Message::SetEdgeColour, 0x00AABB, 0);
	CHECK(editor.WndProc(Message::GetEdgeColour, 0, 0) == 0x00AABB);

	editor.WndProc(Message::MultiEdgeAddLine, 40, 0x112233);
	editor.WndProc(Message::MultiEdgeAddLine, 80, 0x445566);
	CHECK(editor.WndProc(Message::GetMultiEdgeColumn, 0, 0) == 40);
	CHECK(editor.WndProc(Message::GetMultiEdgeColumn, 1, 0) == 80);
	editor.WndProc(Message::MultiEdgeClearAll, 0, 0);
	CHECK(editor.WndProc(Message::GetMultiEdgeColumn, 0, 0) < 0);

	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::SetHighlightGuide, 4, 0);
	CHECK(editor.WndProc(Message::GetHighlightGuide, 0, 0) == 4);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	const sptr_t first = editor.WndProc(Message::AllocateExtendedStyles, 8, 0);
	CHECK(first >= 256);
	editor.WndProc(Message::ReleaseAllExtendedStyles, 0, 0);
	const sptr_t again = editor.WndProc(Message::AllocateExtendedStyles, 8, 0);
	CHECK(again == first);
}

TEST_CASE("TextWidth and named zoom parity with message path") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("MMMM");
	editor.WndProc(Message::StyleSetFore, 0, 0x010203);
	CHECK(editor.WndProc(Message::StyleGetFore, 0, 0) == 0x010203);

	const long w = editor.WndProc(Message::TextWidth, 0, reinterpret_cast<sptr_t>("MM"));
	CHECK(w > 0);
	const std::string_view textWithNul("M\0M", 3);
	CHECK(editor.TextWidth(0, textWithNul) == 3 * editor.TextWidth(0, "M"));

	editor.SetZoom(6);
	CHECK(editor.GetZoom() == 6);
	CHECK(editor.WndProc(Message::GetZoom, 0, 0) == 6);

	// Message path matches named setter.
	editor.WndProc(Message::SetZoom, 2, 0);
	CHECK(editor.GetZoom() == 2);
}
