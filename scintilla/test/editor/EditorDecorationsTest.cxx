// scalpel-editor test code
/** @file EditorDecorationsTest.cxx
 ** Focused behavior tests for indicators, braces, hotspots, representations, and annotations.
 **/

#include <array>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

std::string MessageStringGet(TestEditor &editor, Message msg, uptr_t wParam) {
	const sptr_t len = editor.WndProc(msg, wParam, 0);
	if (len <= 0)
		return {};
	std::string buf(static_cast<size_t>(len) + 1, '\0');
	editor.WndProc(msg, wParam, reinterpret_cast<sptr_t>(buf.data()));
	buf.resize(static_cast<size_t>(len));
	return buf;
}

}  // namespace

TEST_CASE("Indicator style colour hover flags under alpha stroke and out-of-range") {
	TestHost host;
	TestEditor editor(host);

	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::IndicSetStyle, 0, static_cast<sptr_t>(IndicatorStyle::Squiggle));
	CHECK(static_cast<IndicatorStyle>(editor.WndProc(Message::IndicGetStyle, 0, 0))
		== IndicatorStyle::Squiggle);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.WndProc(Message::IndicSetFore, 0, 0x0000FF);
	CHECK(editor.WndProc(Message::IndicGetFore, 0, 0) == 0x0000FF);

	editor.WndProc(Message::IndicSetHoverStyle, 1, static_cast<sptr_t>(IndicatorStyle::Box));
	CHECK(static_cast<IndicatorStyle>(editor.WndProc(Message::IndicGetHoverStyle, 1, 0))
		== IndicatorStyle::Box);
	editor.WndProc(Message::IndicSetHoverFore, 1, 0x00FF00);
	CHECK(editor.WndProc(Message::IndicGetHoverFore, 1, 0) == 0x00FF00);

	editor.WndProc(Message::IndicSetFlags, 2, static_cast<sptr_t>(IndicFlag::ValueFore));
	CHECK(static_cast<IndicFlag>(editor.WndProc(Message::IndicGetFlags, 2, 0)) == IndicFlag::ValueFore);

	editor.WndProc(Message::IndicSetUnder, 3, 1);
	CHECK(editor.WndProc(Message::IndicGetUnder, 3, 0) != 0);
	editor.WndProc(Message::IndicSetUnder, 3, 0);
	CHECK(editor.WndProc(Message::IndicGetUnder, 3, 0) == 0);

	editor.WndProc(Message::IndicSetAlpha, 0, 128);
	CHECK(editor.WndProc(Message::IndicGetAlpha, 0, 0) == 128);
	editor.WndProc(Message::IndicSetOutlineAlpha, 0, 64);
	CHECK(editor.WndProc(Message::IndicGetOutlineAlpha, 0, 0) == 64);
	// Out of 0..255 ignored.
	editor.WndProc(Message::IndicSetAlpha, 0, 300);
	CHECK(editor.WndProc(Message::IndicGetAlpha, 0, 0) == 128);

	editor.WndProc(Message::IndicSetStrokeWidth, 0, 250);
	CHECK(editor.WndProc(Message::IndicGetStrokeWidth, 0, 0) == 250);

	// Out-of-range: no assignment, zero gets.
	const size_t bad = static_cast<size_t>(IndicatorMax) + 1;
	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::IndicSetStyle, bad, static_cast<sptr_t>(IndicatorStyle::FullBox));
	CHECK(editor.WndProc(Message::IndicGetStyle, bad, 0) == 0);
	CHECK(editor.WndProc(Message::IndicGetFore, bad, 0) == 0);

	if constexpr (sizeof(uptr_t) > 4) {
		constexpr uptr_t huge = static_cast<uptr_t>(UINT32_MAX) + 1u;
		editor.WndProc(Message::IndicSetStyle, 0, static_cast<sptr_t>(IndicatorStyle::Squiggle));
		editor.WndProc(Message::IndicSetStyle, huge, static_cast<sptr_t>(IndicatorStyle::FullBox));
		CHECK(static_cast<IndicatorStyle>(editor.WndProc(Message::IndicGetStyle, 0, 0))
			== IndicatorStyle::Squiggle);
		CHECK(editor.WndProc(Message::IndicGetStyle, huge, 0) == 0);
	}
}

TEST_CASE("Indicator fill clear query current value") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("abcdef");

	editor.WndProc(Message::SetIndicatorCurrent, 8, 0);
	CHECK(editor.WndProc(Message::GetIndicatorCurrent, 0, 0) == 8);
	editor.WndProc(Message::SetIndicatorValue, 3, 0);
	CHECK(editor.WndProc(Message::GetIndicatorValue, 0, 0) == 3);

	editor.WndProc(Message::IndicatorFillRange, 1, 3);
	CHECK(editor.WndProc(Message::IndicatorValueAt, 8, 1) == 3);
	CHECK(editor.WndProc(Message::IndicatorValueAt, 8, 2) == 3);
	CHECK(editor.WndProc(Message::IndicatorValueAt, 8, 3) == 3);
	CHECK(editor.WndProc(Message::IndicatorValueAt, 8, 0) == 0);
	CHECK(editor.WndProc(Message::IndicatorValueAt, 8, 4) == 0);
	CHECK(editor.WndProc(Message::IndicatorStart, 8, 2) == 1);
	CHECK(editor.WndProc(Message::IndicatorEnd, 8, 2) == 4);
	CHECK((editor.WndProc(Message::IndicatorAllOnFor, 2, 0) & (1 << 8)) != 0);
	CHECK((editor.WndProc(Message::IndicatorAllOnFor, 0, 0) & (1 << 8)) == 0);

	editor.WndProc(Message::SetIndicatorCurrent, 9, 0);
	editor.WndProc(Message::SetIndicatorValue, 5, 0);
	editor.WndProc(Message::IndicatorFillRange, 4, 2);
	CHECK(editor.WndProc(Message::IndicatorValueAt, 9, 4) == 5);
	CHECK(editor.WndProc(Message::IndicatorValueAt, 9, 5) == 5);
	CHECK(editor.WndProc(Message::IndicatorStart, 9, 5) == 4);
	CHECK(editor.WndProc(Message::IndicatorEnd, 9, 5) == 6);

	editor.WndProc(Message::SetIndicatorCurrent, 8, 0);
	editor.WndProc(Message::IndicatorClearRange, 1, 3);
	CHECK(editor.WndProc(Message::IndicatorValueAt, 8, 1) == 0);
	CHECK(editor.WndProc(Message::IndicatorValueAt, 8, 2) == 0);

	editor.WndProc(Message::SetIndicatorCurrent, 9, 0);
	editor.WndProc(Message::IndicatorClearRange, 4, 2);
	CHECK(editor.WndProc(Message::IndicatorValueAt, 9, 4) == 0);
}

TEST_CASE("Brace highlight match bad light and indicators") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("(a[b]c)");

	const Sci::Position openParen = 0;
	const Sci::Position closeParen = 6;
	const Sci::Position openBracket = 2;
	const Sci::Position closeBracket = 4;

	CHECK(editor.WndProc(Message::BraceMatch, openParen, 0) == closeParen);
	CHECK(editor.WndProc(Message::BraceMatch, closeParen, 0) == openParen);
	CHECK(editor.WndProc(Message::BraceMatch, openBracket, 0) == closeBracket);
	CHECK(editor.WndProc(Message::BraceMatch, 1, 0) == -1);

	CHECK(editor.WndProc(Message::BraceMatchNext, openParen, openParen + 1) == closeParen);

	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::BraceHighlight, openParen, closeParen);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.WndProc(Message::BraceBadLight, openParen, 0);
	editor.WndProc(Message::BraceBadLight, static_cast<uptr_t>(-1), 0);

	editor.WndProc(Message::BraceHighlightIndicator, 1, 10);
	editor.WndProc(Message::BraceBadLightIndicator, 1, 11);
	// Out-of-range indicator ignored (no crash).
	editor.WndProc(Message::BraceHighlightIndicator, 1, static_cast<sptr_t>(IndicatorMax) + 1);

	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::BraceHighlight, openBracket, closeBracket);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
}

TEST_CASE("Hotspot active colours underline single-line") {
	TestHost host;
	TestEditor editor(host);

	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::SetHotspotActiveFore, 1, 0x0000FF);
	CHECK(editor.WndProc(Message::GetHotspotActiveFore, 0, 0) == 0x0000FF);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.WndProc(Message::SetHotspotActiveBack, 1, 0x00FF00);
	CHECK(editor.WndProc(Message::GetHotspotActiveBack, 0, 0) == 0x00FF00);

	editor.WndProc(Message::SetHotspotActiveUnderline, 1, 0);
	CHECK(editor.WndProc(Message::GetHotspotActiveUnderline, 0, 0) != 0);
	editor.WndProc(Message::SetHotspotActiveUnderline, 0, 0);
	CHECK(editor.WndProc(Message::GetHotspotActiveUnderline, 0, 0) == 0);

	editor.WndProc(Message::SetHotspotSingleLine, 1, 0);
	CHECK(editor.WndProc(Message::GetHotspotSingleLine, 0, 0) != 0);
	editor.WndProc(Message::SetHotspotSingleLine, 0, 0);
	CHECK(editor.WndProc(Message::GetHotspotSingleLine, 0, 0) == 0);

	// Clear optional colours.
	editor.WndProc(Message::SetHotspotActiveFore, 0, 0);
	editor.WndProc(Message::SetHotspotActiveBack, 0, 0);
}

TEST_CASE("Representation set get clear appearance colour control-char") {
	TestHost host;
	TestEditor editor(host);

	const char *ohm = "\xe2\x84\xa6";  // U+2126
	const char *label = "OHM";

	editor.WndProc(Message::SetRepresentation, reinterpret_cast<uptr_t>(ohm),
		reinterpret_cast<sptr_t>(label));
	CHECK(MessageStringGet(editor, Message::GetRepresentation, reinterpret_cast<uptr_t>(ohm)) == label);

	editor.WndProc(Message::SetRepresentationAppearance, reinterpret_cast<uptr_t>(ohm),
		static_cast<sptr_t>(RepresentationAppearance::Plain));
	CHECK(static_cast<RepresentationAppearance>(
		editor.WndProc(Message::GetRepresentationAppearance, reinterpret_cast<uptr_t>(ohm), 0))
		== RepresentationAppearance::Plain);

	editor.WndProc(Message::SetRepresentationAppearance, reinterpret_cast<uptr_t>(ohm),
		static_cast<sptr_t>(RepresentationAppearance::Blob));
	CHECK(static_cast<RepresentationAppearance>(
		editor.WndProc(Message::GetRepresentationAppearance, reinterpret_cast<uptr_t>(ohm), 0))
		== RepresentationAppearance::Blob);

	// Packed colouralpha as int: compare as unsigned so the alpha high bit is not sign-extended.
	editor.WndProc(Message::SetRepresentationColour, reinterpret_cast<uptr_t>(ohm), 0x800000FF);
	CHECK(static_cast<uint32_t>(editor.WndProc(Message::GetRepresentationColour,
		reinterpret_cast<uptr_t>(ohm), 0)) == 0x800000FFu);

	editor.WndProc(Message::ClearRepresentation, reinterpret_cast<uptr_t>(ohm), 0);
	CHECK(editor.WndProc(Message::GetRepresentation, reinterpret_cast<uptr_t>(ohm), 0) == 0);

	// Control char symbol.
	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::SetControlCharSymbol, 42, 0);
	CHECK(editor.WndProc(Message::GetControlCharSymbol, 0, 0) == 42);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
	editor.WndProc(Message::SetControlCharSymbol, 0, 0);
	CHECK(editor.WndProc(Message::GetControlCharSymbol, 0, 0) == 0);

	// Clear all restores defaults.
	const char *letterA = "A";
	editor.WndProc(Message::SetRepresentation, reinterpret_cast<uptr_t>(letterA),
		reinterpret_cast<sptr_t>("AY"));
	editor.WndProc(Message::ClearAllRepresentations, 0, 0);
	CHECK(editor.WndProc(Message::GetRepresentation, reinterpret_cast<uptr_t>(letterA), 0) == 0);
}

TEST_CASE("Annotation text style styles lines visible offset clear") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("one\ntwo\nthree\n");

	editor.WndProc(Message::AnnotationSetText, 1, reinterpret_cast<sptr_t>("note"));
	CHECK(MessageStringGet(editor, Message::AnnotationGetText, 1) == "note");

	editor.WndProc(Message::AnnotationSetStyle, 1, 7);
	CHECK(editor.WndProc(Message::AnnotationGetStyle, 1, 0) == 7);

	// Multi-line annotation counts as display lines when visible.
	editor.WndProc(Message::AnnotationSetText, 0, reinterpret_cast<sptr_t>("a\nb"));
	CHECK(editor.WndProc(Message::AnnotationGetLines, 0, 0) == 2);

	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::AnnotationSetVisible, static_cast<uptr_t>(AnnotationVisible::Standard), 0);
	CHECK(static_cast<AnnotationVisible>(editor.WndProc(Message::AnnotationGetVisible, 0, 0))
		== AnnotationVisible::Standard);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.WndProc(Message::AnnotationSetStyleOffset, 256, 0);
	CHECK(editor.WndProc(Message::AnnotationGetStyleOffset, 0, 0) == 256);

	// Per-character styles.
	const unsigned char styles[] = {1, 2, 3, 4};
	editor.WndProc(Message::AnnotationSetText, 2, reinterpret_cast<sptr_t>("abcd"));
	editor.WndProc(Message::AnnotationSetStyles, 2, reinterpret_cast<sptr_t>(styles));
	char styleBuf[8]{};
	CHECK(editor.WndProc(Message::AnnotationGetStyles, 2, reinterpret_cast<sptr_t>(styleBuf)) == 4);
	CHECK(std::memcmp(styleBuf, styles, 4) == 0);

	editor.WndProc(Message::AnnotationClearAll, 0, 0);
	CHECK(editor.WndProc(Message::AnnotationGetText, 1, 0) == 0);
	CHECK(editor.WndProc(Message::AnnotationGetLines, 0, 0) == 0);

	editor.WndProc(Message::AnnotationSetText, 0, reinterpret_cast<sptr_t>("via-msg"));
	CHECK(MessageStringGet(editor, Message::AnnotationGetText, 0) == "via-msg");
	editor.WndProc(Message::AnnotationSetVisible, static_cast<uptr_t>(AnnotationVisible::Boxed), 0);
	CHECK(static_cast<AnnotationVisible>(editor.WndProc(Message::AnnotationGetVisible, 0, 0))
		== AnnotationVisible::Boxed);
	editor.WndProc(Message::AnnotationClearAll, 0, 0);
}

TEST_CASE("EOL annotation text style visible offset clear") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("alpha\nbeta\n");

	editor.WndProc(Message::EOLAnnotationSetText, 0, reinterpret_cast<sptr_t>("asm"));
	CHECK(MessageStringGet(editor, Message::EOLAnnotationGetText, 0) == "asm");

	editor.WndProc(Message::EOLAnnotationSetStyle, 0, 5);
	CHECK(editor.WndProc(Message::EOLAnnotationGetStyle, 0, 0) == 5);

	editor.PaintAll();
	editor.ClearObservations();
	editor.WndProc(Message::EOLAnnotationSetVisible,
		static_cast<uptr_t>(EOLAnnotationVisible::Standard), 0);
	CHECK(static_cast<EOLAnnotationVisible>(
		editor.WndProc(Message::EOLAnnotationGetVisible, 0, 0)) == EOLAnnotationVisible::Standard);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.WndProc(Message::EOLAnnotationSetStyleOffset, 512, 0);
	CHECK(editor.WndProc(Message::EOLAnnotationGetStyleOffset, 0, 0) == 512);

	editor.WndProc(Message::EOLAnnotationClearAll, 0, 0);
	CHECK(editor.WndProc(Message::EOLAnnotationGetText, 0, 0) == 0);

	editor.WndProc(Message::EOLAnnotationSetText, 1, reinterpret_cast<sptr_t>("end"));
	CHECK(MessageStringGet(editor, Message::EOLAnnotationGetText, 1) == "end");
	editor.WndProc(Message::EOLAnnotationSetStyle, 1, 9);
	CHECK(editor.WndProc(Message::EOLAnnotationGetStyle, 1, 0) == 9);
	editor.WndProc(Message::EOLAnnotationSetVisible,
		static_cast<uptr_t>(EOLAnnotationVisible::Boxed), 0);
	CHECK(static_cast<EOLAnnotationVisible>(
		editor.WndProc(Message::EOLAnnotationGetVisible, 0, 0)) == EOLAnnotationVisible::Boxed);
	editor.WndProc(Message::EOLAnnotationClearAll, 0, 0);
	CHECK(editor.WndProc(Message::EOLAnnotationGetText, 1, 0) == 0);
}
