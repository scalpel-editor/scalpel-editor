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

// Apply a fixed decoration sequence through named TestEditor wrappers.
struct DecorationsSnapshot {
	IndicatorStyle indicatorStyle = IndicatorStyle::Plain;
	int indicatorFore = 0;
	int currentIndicator = 0;
	int currentValue = 0;
	int valueAt1 = 0;
	int valueAt2 = 0;
	Sci::Position rangeStart = 0;
	Sci::Position rangeEnd = 0;
	Sci::Position braceMatch = 0;
	int controlCharSymbol = 0;
	std::string representation;
	int hotspotFore = 0;
	std::string annotationText;
	int annotationStyle = 0;
	AnnotationVisible annotationVisible = AnnotationVisible::Hidden;
	std::string eolAnnotationText;
	size_t invalidatedRectangles = 0;
};

DecorationsSnapshot CaptureDecorations() {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("(ab)");
	editor.PaintAll();
	editor.ClearObservations();

	const char *ohm = "\xe2\x84\xa6";
	const char *label = "OHM";

	editor.IndicSetStyle(8, IndicatorStyle::Squiggle);
	editor.IndicSetFore(8, 0x0000FF);
	editor.SetIndicatorCurrent(8);
	editor.SetIndicatorValue(3);
	editor.IndicatorFillRange(1, 2);
	editor.BraceHighlight(0, 3);
	editor.SetControlCharSymbol(42);
	editor.SetRepresentation(ohm, label);
	editor.SetHotspotActiveFore(true, 0x00FF00);
	editor.AnnotationSetText(0, "note");
	editor.AnnotationSetStyle(0, 7);
	editor.SetAnnotationVisible(AnnotationVisible::Standard);
	editor.EOLAnnotationSetText(0, "eol");

	DecorationsSnapshot s;
	s.indicatorStyle = editor.IndicGetStyle(8);
	s.indicatorFore = editor.IndicGetFore(8);
	s.currentIndicator = editor.GetIndicatorCurrent();
	s.currentValue = editor.GetIndicatorValue();
	s.valueAt1 = editor.IndicatorValueAt(8, 1);
	s.valueAt2 = editor.IndicatorValueAt(8, 2);
	s.rangeStart = editor.IndicatorStart(8, 1);
	s.rangeEnd = editor.IndicatorEnd(8, 1);
	s.braceMatch = editor.BraceMatch(0, 0);
	s.controlCharSymbol = editor.GetControlCharSymbol();
	char reprBuf[16]{};
	const int reprLen = editor.GetRepresentation(ohm, reprBuf);
	s.representation.assign(reprBuf, static_cast<size_t>(reprLen > 0 ? reprLen : 0));
	s.hotspotFore = editor.GetHotspotActiveFore();
	s.annotationText = editor.AnnotationGetText(0);
	s.annotationStyle = editor.AnnotationGetStyle(0);
	s.annotationVisible = editor.AnnotationGetVisible();
	s.eolAnnotationText = editor.EOLAnnotationGetText(0);
	s.invalidatedRectangles = editor.Snapshot().invalidatedRectangles;
	return s;
}

}  // namespace

TEST_CASE("Indicator style colour hover flags under alpha stroke and out-of-range") {
	TestHost host;
	TestEditor editor(host);

	editor.PaintAll();
	editor.ClearObservations();
	editor.IndicSetStyle(0, IndicatorStyle::Squiggle);
	CHECK(static_cast<IndicatorStyle>(editor.IndicGetStyle(0))
		== IndicatorStyle::Squiggle);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.IndicSetFore(0, 0x0000FF);
	CHECK(editor.IndicGetFore(0) == 0x0000FF);

	editor.IndicSetHoverStyle(1, IndicatorStyle::Box);
	CHECK(static_cast<IndicatorStyle>(editor.IndicGetHoverStyle(1))
		== IndicatorStyle::Box);
	editor.IndicSetHoverFore(1, 0x00FF00);
	CHECK(editor.IndicGetHoverFore(1) == 0x00FF00);

	editor.IndicSetFlags(2, IndicFlag::ValueFore);
	CHECK(static_cast<IndicFlag>(editor.IndicGetFlags(2)) == IndicFlag::ValueFore);

	editor.IndicSetUnder(3, true);
	CHECK(editor.IndicGetUnder(3) != 0);
	editor.IndicSetUnder(3, false);
	CHECK(editor.IndicGetUnder(3) == 0);

	editor.IndicSetAlpha(0, 128);
	CHECK(editor.IndicGetAlpha(0) == 128);
	editor.IndicSetOutlineAlpha(0, 64);
	CHECK(editor.IndicGetOutlineAlpha(0) == 64);
	// Out of 0..255 ignored.
	editor.IndicSetAlpha(0, 300);
	CHECK(editor.IndicGetAlpha(0) == 128);

	editor.IndicSetStrokeWidth(0, 250);
	CHECK(editor.IndicGetStrokeWidth(0) == 250);

	// Out-of-range: no assignment, zero gets.
	const size_t bad = static_cast<size_t>(IndicatorMax) + 1;
	editor.PaintAll();
	editor.ClearObservations();
	editor.IndicSetStyle(bad, IndicatorStyle::FullBox);
	CHECK(static_cast<int>(editor.IndicGetStyle(bad)) == 0);
	CHECK(editor.IndicGetFore(bad) == 0);

	if constexpr (sizeof(uptr_t) > 4) {
		constexpr uptr_t huge = static_cast<uptr_t>(UINT32_MAX) + 1u;
		editor.IndicSetStyle(0, IndicatorStyle::Squiggle);
		editor.IndicSetStyle(huge, IndicatorStyle::FullBox);
		CHECK(static_cast<IndicatorStyle>(editor.IndicGetStyle(0))
			== IndicatorStyle::Squiggle);
		CHECK(static_cast<int>(editor.IndicGetStyle(huge)) == 0);
	}
}

TEST_CASE("Indicator fill clear query current value") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("abcdef");

	editor.SetIndicatorCurrent(static_cast<int>(8));
	CHECK(editor.GetIndicatorCurrent() == 8);
	editor.SetIndicatorValue(static_cast<int>(3));
	CHECK(editor.GetIndicatorValue() == 3);

	editor.IndicatorFillRange(1, 3);
	CHECK(editor.IndicatorValueAt(static_cast<int>(8), 1) == 3);
	CHECK(editor.IndicatorValueAt(static_cast<int>(8), 2) == 3);
	CHECK(editor.IndicatorValueAt(static_cast<int>(8), 3) == 3);
	CHECK(editor.IndicatorValueAt(static_cast<int>(8), 0) == 0);
	CHECK(editor.IndicatorValueAt(static_cast<int>(8), 4) == 0);
	CHECK(editor.IndicatorStart(static_cast<int>(8), 2) == 1);
	CHECK(editor.IndicatorEnd(static_cast<int>(8), 2) == 4);
	CHECK((editor.IndicatorAllOnFor(2) & (1 << 8)) != 0);
	CHECK((editor.IndicatorAllOnFor(0) & (1 << 8)) == 0);

	editor.SetIndicatorCurrent(static_cast<int>(9));
	editor.SetIndicatorValue(static_cast<int>(5));
	editor.IndicatorFillRange(4, 2);
	CHECK(editor.IndicatorValueAt(static_cast<int>(9), 4) == 5);
	CHECK(editor.IndicatorValueAt(static_cast<int>(9), 5) == 5);
	CHECK(editor.IndicatorStart(static_cast<int>(9), 5) == 4);
	CHECK(editor.IndicatorEnd(static_cast<int>(9), 5) == 6);

	editor.SetIndicatorCurrent(static_cast<int>(8));
	editor.IndicatorClearRange(1, 3);
	CHECK(editor.IndicatorValueAt(static_cast<int>(8), 1) == 0);
	CHECK(editor.IndicatorValueAt(static_cast<int>(8), 2) == 0);

	editor.SetIndicatorCurrent(static_cast<int>(9));
	editor.IndicatorClearRange(4, 2);
	CHECK(editor.IndicatorValueAt(static_cast<int>(9), 4) == 0);
}

TEST_CASE("Brace highlight match bad light and indicators") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("(a[b]c)");

	const Sci::Position openParen = 0;
	const Sci::Position closeParen = 6;
	const Sci::Position openBracket = 2;
	const Sci::Position closeBracket = 4;

	CHECK(editor.BraceMatch(openParen, 0) == closeParen);
	CHECK(editor.BraceMatch(closeParen, 0) == openParen);
	CHECK(editor.BraceMatch(openBracket, 0) == closeBracket);
	CHECK(editor.BraceMatch(1, 0) == -1);

	CHECK(editor.BraceMatchNext(openParen, openParen + 1) == closeParen);

	editor.PaintAll();
	editor.ClearObservations();
	editor.BraceHighlight(openParen, closeParen);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.BraceBadLight(openParen);
	editor.BraceBadLight(-1);

	editor.BraceHighlightIndicator(1, 10);
	editor.BraceBadLightIndicator(1, 11);
	// Out-of-range indicator ignored (no crash).
	editor.BraceHighlightIndicator(1, static_cast<sptr_t>(IndicatorMax) + 1);

	editor.PaintAll();
	editor.ClearObservations();
	editor.BraceHighlight(openBracket, closeBracket);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
}

TEST_CASE("Hotspot active colours underline single-line") {
	TestHost host;
	TestEditor editor(host);

	editor.PaintAll();
	editor.ClearObservations();
	editor.SetHotspotActiveFore(true, 0x0000FF);
	CHECK(editor.GetHotspotActiveFore() == 0x0000FF);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.SetHotspotActiveBack(true, 0x00FF00);
	CHECK(editor.GetHotspotActiveBack() == 0x00FF00);

	editor.SetHotspotActiveUnderline(true);
	CHECK(editor.GetHotspotActiveUnderline());
	editor.SetHotspotActiveUnderline(false);
	CHECK_FALSE(editor.GetHotspotActiveUnderline());

	editor.SetHotspotSingleLine(true);
	CHECK(editor.GetHotspotSingleLine());
	editor.SetHotspotSingleLine(false);
	CHECK_FALSE(editor.GetHotspotSingleLine());

	// Clear optional colours.
	editor.SetHotspotActiveFore(false, 0);
	editor.SetHotspotActiveBack(false, 0);
}

TEST_CASE("Representation set get clear appearance colour control-char") {
	TestHost host;
	TestEditor editor(host);

	const char *ohm = "\xe2\x84\xa6";  // U+2126
	const char *label = "OHM";

	editor.SetRepresentation(ohm, label);
	CHECK(([&](){ char b[64]={}; editor.GetRepresentation(ohm, b); return std::string(b);}()) == label);

	editor.SetRepresentationAppearance(ohm, RepresentationAppearance::Plain);
	CHECK(static_cast<RepresentationAppearance>(editor.GetRepresentationAppearance(ohm))
		== RepresentationAppearance::Plain);

	editor.SetRepresentationAppearance(ohm, RepresentationAppearance::Blob);
	CHECK(static_cast<RepresentationAppearance>(editor.GetRepresentationAppearance(ohm))
		== RepresentationAppearance::Blob);

	// Packed colouralpha as int: compare as unsigned so the alpha high bit is not sign-extended.
	editor.SetRepresentationColour(ohm, 0x800000FF);
	CHECK(static_cast<uint32_t>(editor.GetRepresentationColour(ohm)) == 0x800000FFu);

	editor.ClearRepresentation(ohm);
	CHECK(editor.GetRepresentation(ohm, nullptr) == 0);

	// Control char symbol.
	editor.PaintAll();
	editor.ClearObservations();
	editor.SetControlCharSymbol(static_cast<int>(42));
	CHECK(editor.GetControlCharSymbol() == 42);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);
	editor.SetControlCharSymbol(static_cast<int>(0));
	CHECK(editor.GetControlCharSymbol() == 0);

	// Clear all restores defaults.
	const char *letterA = "A";
	editor.SetRepresentation(letterA, "AY");
	editor.ClearAllRepresentations();
	CHECK(editor.GetRepresentation(letterA, nullptr) == 0);
}

TEST_CASE("Annotation text style styles lines visible offset clear") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("one\ntwo\nthree\n");

	editor.AnnotationSetText(1, "note");
	CHECK(editor.AnnotationGetText(1) == "note");

	editor.AnnotationSetStyle(1, 7);
	CHECK(editor.AnnotationGetStyle(1) == 7);

	// Multi-line annotation counts as display lines when visible.
	editor.AnnotationSetText(0, "a\nb");
	CHECK(editor.AnnotationGetLines(0) == 2);

	editor.PaintAll();
	editor.ClearObservations();
	editor.SetAnnotationVisible(AnnotationVisible::Standard);
	CHECK(static_cast<AnnotationVisible>(editor.AnnotationGetVisible())
		== AnnotationVisible::Standard);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.AnnotationSetStyleOffset(256);
	CHECK(editor.AnnotationGetStyleOffset() == 256);

	// Per-character styles.
	const unsigned char styles[] = {1, 2, 3, 4};
	editor.AnnotationSetText(2, "abcd");
	editor.AnnotationSetStyles(2, styles);
	char styleBuf[8]{};
	CHECK(editor.AnnotationGetStyles(2, styleBuf) == 4);
	CHECK(std::memcmp(styleBuf, styles, 4) == 0);

	editor.AnnotationClearAll();
	CHECK(editor.AnnotationGetText(1).empty());
	CHECK(editor.AnnotationGetLines(0) == 0);

	editor.AnnotationSetText(0, "via-msg");
	CHECK(editor.AnnotationGetText(0) == "via-msg");
	editor.SetAnnotationVisible(AnnotationVisible::Boxed);
	CHECK(static_cast<AnnotationVisible>(editor.AnnotationGetVisible())
		== AnnotationVisible::Boxed);
	editor.AnnotationClearAll();
}

TEST_CASE("EOL annotation text style visible offset clear") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("alpha\nbeta\n");

	editor.EOLAnnotationSetText(0, "asm");
	CHECK(editor.EOLAnnotationGetText(0) == "asm");

	editor.EOLAnnotationSetStyle(0, 5);
	CHECK(editor.EOLAnnotationGetStyle(0) == 5);

	editor.PaintAll();
	editor.ClearObservations();
	editor.SetEOLAnnotationVisible(EOLAnnotationVisible::Standard);
	CHECK(static_cast<EOLAnnotationVisible>(editor.EOLAnnotationGetVisible()) == EOLAnnotationVisible::Standard);
	CHECK(editor.Snapshot().invalidatedRectangles > 0);

	editor.EOLAnnotationSetStyleOffset(512);
	CHECK(editor.EOLAnnotationGetStyleOffset() == 512);

	editor.EOLAnnotationClearAll();
	CHECK(editor.EOLAnnotationGetText(0).empty());

	editor.EOLAnnotationSetText(1, "end");
	CHECK(editor.EOLAnnotationGetText(1) == "end");
	editor.EOLAnnotationSetStyle(1, 9);
	CHECK(editor.EOLAnnotationGetStyle(1) == 9);
	editor.SetEOLAnnotationVisible(EOLAnnotationVisible::Boxed);
	CHECK(static_cast<EOLAnnotationVisible>(editor.EOLAnnotationGetVisible()) == EOLAnnotationVisible::Boxed);
	editor.EOLAnnotationClearAll();
	CHECK(editor.EOLAnnotationGetText(1).empty());
}

TEST_CASE("Decoration named path captures indicator brace annotation and representation state") {
	const DecorationsSnapshot s = CaptureDecorations();
	CHECK(s.indicatorStyle == IndicatorStyle::Squiggle);
	CHECK(s.indicatorFore == 0x0000FF);
	CHECK(s.currentIndicator == 8);
	CHECK(s.currentValue == 3);
	CHECK(s.valueAt1 == 3);
	CHECK(s.controlCharSymbol == 42);
	CHECK(s.representation == "OHM");
	CHECK(s.annotationText == "note");
	CHECK(s.annotationStyle == 7);
	CHECK(s.annotationVisible == AnnotationVisible::Standard);
	CHECK(s.eolAnnotationText == "eol");
}
