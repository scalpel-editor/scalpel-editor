// Scintilla source code edit control
/** @file EditorDecorations.cxx
 ** Indicators, brace highlighting, hotspots, character representations, and annotations.
 **
 ** Indicators draw extra information over styled text (underlines, boxes, strike-outs, or hidden tracking ranges). Indicator numbers 0..7 are conventionally for lexers, 8..31 for the container, 32..35 for IME, and 36..43 for change history. Each indicator has a normal and hover style/colour, optional under-text drawing, fill and outline alpha, stroke width, and flags (for example ValueFore colours by range value). Range fill and clear use the current indicator and value, or an explicit indicator for queries.
 **
 ** BraceHighlight / BraceBadLight mark up to two positions with StyleBraceLight or StyleBraceBad (or optional indicators). BraceMatch finds the matching (), [], {}, or <> pair with nested braces and matching style.
 **
 ** Hotspot active colours and underline apply when the pointer is over hotspot-styled text. Character representations replace control and other hard-to-see characters with a short UTF-8 string (default blob look); ClearAllRepresentations restores the defaults. Control-char symbol can replace C0 mnemonics with one glyph when set to 32..255.
 **
 ** Annotations are read-only multi-line text under a document line; EOL annotations sit at the end of a line. Visibility modes and style offsets keep their styles off lexer ranges. SetAnnotationHeights (still in Editor.cxx) adjusts display heights when annotations are visible.
 **
 ** The macOS-only FindIndicatorShow / Flash / Hide platform animation messages are not implemented and have no named operations here.
 **/
// Copyright 1998-2011 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cmath>

#include <stdexcept>
#include <utility>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <set>
#include <forward_list>
#include <optional>
#include <algorithm>
#include <iterator>
#include <memory>
#include <chrono>
#include <atomic>
#include <mutex>
#include <thread>
#include <future>

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
#include "CaseConvert.h"
#include "Document.h"
#include "UniConversion.h"
#include "Selection.h"
#include "PositionCache.h"
#include "EditModel.h"
#include "MarginView.h"
#include "EditView.h"
#include "Editor.h"
#include "ElapsedPeriod.h"

using namespace Scintilla;
using namespace Scintilla::Internal;

namespace {

// Matches the historical uptr_t message check (wParam <= IndicatorMax).
bool ValidIndicator(size_t indicator) noexcept {
	return indicator <= static_cast<size_t>(IndicatorMax);
}

}

void Editor::SetRepresentations() {
	reprs->SetDefaultRepresentations();
}

// Indicator style (normal and hover together). Out-of-range indicators are ignored.
void Editor::IndicSetStyle(size_t indicator, IndicatorStyle style) {
	if (ValidIndicator(indicator)) {
		vs.indicators[indicator].sacNormal.style = style;
		vs.indicators[indicator].sacHover.style = style;
		InvalidateStyleRedraw();
	}
}

// Style for indicator, or Plain (0) when out of range.
IndicatorStyle Editor::IndicGetStyle(size_t indicator) const noexcept {
	if (ValidIndicator(indicator))
		return vs.indicators[indicator].sacNormal.style;
	return static_cast<IndicatorStyle>(0);
}

// Opaque foreground for normal and hover. Out-of-range indicators are ignored.
void Editor::IndicSetFore(size_t indicator, int rgb) {
	if (ValidIndicator(indicator)) {
		vs.indicators[indicator].sacNormal.fore = ColourRGBA::FromIpRGB(rgb);
		vs.indicators[indicator].sacHover.fore = ColourRGBA::FromIpRGB(rgb);
		InvalidateStyleRedraw();
	}
}

// Opaque RGB for indicator, or 0 when out of range.
int Editor::IndicGetFore(size_t indicator) const noexcept {
	if (ValidIndicator(indicator))
		return vs.indicators[indicator].sacNormal.fore.OpaqueRGB();
	return 0;
}

// Hover style only. Out-of-range indicators are ignored.
void Editor::IndicSetHoverStyle(size_t indicator, IndicatorStyle style) {
	if (ValidIndicator(indicator)) {
		vs.indicators[indicator].sacHover.style = style;
		InvalidateStyleRedraw();
	}
}

// Hover style, or Plain (0) when out of range.
IndicatorStyle Editor::IndicGetHoverStyle(size_t indicator) const noexcept {
	if (ValidIndicator(indicator))
		return vs.indicators[indicator].sacHover.style;
	return static_cast<IndicatorStyle>(0);
}

// Hover foreground. Out-of-range indicators are ignored.
void Editor::IndicSetHoverFore(size_t indicator, int rgb) {
	if (ValidIndicator(indicator)) {
		vs.indicators[indicator].sacHover.fore = ColourRGBA::FromIpRGB(rgb);
		InvalidateStyleRedraw();
	}
}

// Hover RGB, or 0 when out of range.
int Editor::IndicGetHoverFore(size_t indicator) const noexcept {
	if (ValidIndicator(indicator))
		return vs.indicators[indicator].sacHover.fore.OpaqueRGB();
	return 0;
}

// Indicator flags (for example ValueFore). Out-of-range indicators are ignored.
void Editor::IndicSetFlags(size_t indicator, IndicFlag flags) {
	if (ValidIndicator(indicator)) {
		vs.indicators[indicator].SetFlags(flags);
		InvalidateStyleRedraw();
	}
}

// Flags for indicator, or None when out of range.
IndicFlag Editor::IndicGetFlags(size_t indicator) const noexcept {
	if (ValidIndicator(indicator))
		return vs.indicators[indicator].Flags();
	return static_cast<IndicFlag>(0);
}

// Draw under text when true. Out-of-range indicators are ignored.
void Editor::IndicSetUnder(size_t indicator, bool under) {
	if (ValidIndicator(indicator)) {
		vs.indicators[indicator].under = under;
		InvalidateStyleRedraw();
	}
}

// Whether the indicator draws under text.
bool Editor::IndicGetUnder(size_t indicator) const noexcept {
	if (ValidIndicator(indicator))
		return vs.indicators[indicator].under;
	return false;
}

// Fill alpha 0..255. Values outside that range or out-of-range indicators are ignored.
void Editor::IndicSetAlpha(size_t indicator, int alpha) {
	if (ValidIndicator(indicator) && alpha >= 0 && alpha <= 255) {
		vs.indicators[indicator].fillAlpha = alpha;
		InvalidateStyleRedraw();
	}
}

// Fill alpha, or 0 when out of range.
int Editor::IndicGetAlpha(size_t indicator) const noexcept {
	if (ValidIndicator(indicator))
		return vs.indicators[indicator].fillAlpha;
	return 0;
}

// Outline alpha 0..255. Values outside that range or out-of-range indicators are ignored.
void Editor::IndicSetOutlineAlpha(size_t indicator, int alpha) {
	if (ValidIndicator(indicator) && alpha >= 0 && alpha <= 255) {
		vs.indicators[indicator].outlineAlpha = alpha;
		InvalidateStyleRedraw();
	}
}

// Outline alpha, or 0 when out of range.
int Editor::IndicGetOutlineAlpha(size_t indicator) const noexcept {
	if (ValidIndicator(indicator))
		return vs.indicators[indicator].outlineAlpha;
	return 0;
}

// Stroke width in hundredths of a pixel (0..1000). Out of range is ignored.
void Editor::IndicSetStrokeWidth(size_t indicator, int hundredths) {
	if (ValidIndicator(indicator) && hundredths >= 0 && hundredths <= 1000) {
		vs.indicators[indicator].strokeWidth = static_cast<XYPOSITION>(hundredths) / 100.0;
		InvalidateStyleRedraw();
	}
}

// Stroke width in hundredths of a pixel, or 0 when out of range.
int Editor::IndicGetStrokeWidth(size_t indicator) const noexcept {
	if (ValidIndicator(indicator))
		return static_cast<int>(std::lround(vs.indicators[indicator].strokeWidth * 100));
	return 0;
}

// Indicator used by IndicatorFillRange / IndicatorClearRange.
void Editor::SetIndicatorCurrent(int indicator) {
	pdoc->DecorationSetCurrentIndicator(indicator);
}

// Current indicator number.
int Editor::GetIndicatorCurrent() const noexcept {
	return pdoc->decorations->GetCurrentIndicator();
}

// Value written by IndicatorFillRange for the current indicator.
void Editor::SetIndicatorValue(int value) {
	pdoc->decorations->SetCurrentValue(value);
}

// Current fill value.
int Editor::GetIndicatorValue() const noexcept {
	return pdoc->decorations->GetCurrentValue();
}

// Fill lengthFill bytes from start with the current indicator and value.
void Editor::IndicatorFillRange(Sci::Position start, Sci::Position lengthFill) {
	pdoc->DecorationFillRange(start, pdoc->decorations->GetCurrentValue(), lengthFill);
}

// Clear the current indicator over lengthClear bytes from start (value 0).
void Editor::IndicatorClearRange(Sci::Position start, Sci::Position lengthClear) {
	pdoc->DecorationFillRange(start, 0, lengthClear);
}

// Bitset of indicators present at pos.
int Editor::IndicatorAllOnFor(Sci::Position pos) const {
	return pdoc->decorations->AllOnFor(pos);
}

// Value of indicator at pos.
int Editor::IndicatorValueAt(int indicator, Sci::Position pos) const {
	return pdoc->decorations->ValueAt(indicator, pos);
}

// Start of the run of indicator that contains pos.
Sci::Position Editor::IndicatorStart(int indicator, Sci::Position pos) const {
	return pdoc->decorations->Start(indicator, pos);
}

// End of the run of indicator that contains pos.
Sci::Position Editor::IndicatorEnd(int indicator, Sci::Position pos) const {
	return pdoc->decorations->End(indicator, pos);
}

// Highlight matching braces at pos0 and pos1 with StyleBraceLight.
void Editor::BraceHighlight(Sci::Position pos0, Sci::Position pos1) {
	SetBraceHighlight(pos0, pos1, StyleBraceLight);
}

// When useSetting is true, use indicator for matching braces instead of style change.
void Editor::BraceHighlightIndicator(bool useSetting, size_t indicator) {
	if (ValidIndicator(indicator)) {
		vs.braceHighlightIndicatorSet = useSetting;
		vs.braceHighlightIndicator = static_cast<int>(indicator);
	}
}

// Highlight a single unmatched brace with StyleBraceBad. pos -1 clears.
void Editor::BraceBadLight(Sci::Position pos) {
	SetBraceHighlight(pos, -1, StyleBraceBad);
}

// When useSetting is true, use indicator for an unmatched brace instead of style change.
void Editor::BraceBadLightIndicator(bool useSetting, size_t indicator) {
	if (ValidIndicator(indicator)) {
		vs.braceBadLightIndicatorSet = useSetting;
		vs.braceBadLightIndicator = static_cast<int>(indicator);
	}
}

// Matching brace for (), [], {}, or <> at pos, or -1. maxReStyle is reserved (use 0).
Sci::Position Editor::BraceMatch(Sci::Position pos, Sci::Position maxReStyle) const noexcept {
	return pdoc->BraceMatch(pos, maxReStyle, 0, false);
}

// Like BraceMatch but matching starts at startPos instead of pos ± 1.
Sci::Position Editor::BraceMatchNext(Sci::Position pos, Sci::Position startPos) const noexcept {
	return pdoc->BraceMatch(pos, 0, startPos, true);
}

void Editor::SetBraceHighlight(Sci::Position pos0, Sci::Position pos1, int matchStyle) {
	if ((pos0 != braces[0]) || (pos1 != braces[1]) || (matchStyle != bracesMatchStyle)) {
		if ((braces[0] != pos0) || (matchStyle != bracesMatchStyle)) {
			CheckForChangeOutsidePaint(Range(braces[0]));
			CheckForChangeOutsidePaint(Range(pos0));
			braces[0] = pos0;
		}
		if ((braces[1] != pos1) || (matchStyle != bracesMatchStyle)) {
			CheckForChangeOutsidePaint(Range(braces[1]));
			CheckForChangeOutsidePaint(Range(pos1));
			braces[1] = pos1;
		}
		bracesMatchStyle = matchStyle;
		if (paintState == PaintState::notPainting) {
			Redraw();
		}
	}
}

// Replace C0 control mnemonics with this ASCII glyph when symbol is 32..255; below 32 restores mnemonics.
void Editor::SetControlCharSymbol(int symbol) {
	vs.controlCharSymbol = symbol;
	InvalidateStyleRedraw();
}

// Current control-char symbol (default 0 = mnemonics).
int Editor::GetControlCharSymbol() const noexcept {
	return vs.controlCharSymbol;
}

// Map one UTF-8 character (or the special "\r\n" pair) to a display string (max 200 bytes).
void Editor::SetRepresentation(std::string_view charBytes, std::string_view value) {
	reprs->SetRepresentation(charBytes, value);
}

// Copy the representation string into buffer when non-null; return length without NUL. Missing mapping returns 0.
int Editor::GetRepresentation(std::string_view charBytes, char *buffer) const {
	const Representation *repr = reprs->RepresentationFromCharacter(charBytes);
	if (!repr)
		return 0;
	const size_t len = repr->stringRep.size();
	if (buffer)
		memcpy(buffer, repr->stringRep.c_str(), len + 1);
	return static_cast<int>(len);
}

// Remove a custom representation for charBytes.
void Editor::ClearRepresentation(std::string_view charBytes) {
	reprs->ClearRepresentation(charBytes);
}

// Restore default representations (control mnemonics and invalid-byte hex).
void Editor::ClearAllRepresentations() {
	SetRepresentations();
}

// Appearance flags for a representation (Plain, Blob, Colour).
void Editor::SetRepresentationAppearance(std::string_view charBytes, RepresentationAppearance appearance) {
	reprs->SetRepresentationAppearance(charBytes, appearance);
}

// Appearance of the representation, or 0 when none.
RepresentationAppearance Editor::GetRepresentationAppearance(std::string_view charBytes) const {
	const Representation *repr = reprs->RepresentationFromCharacter(charBytes);
	if (repr)
		return repr->appearance;
	return static_cast<RepresentationAppearance>(0);
}

// Colour and alpha for a representation (used when Colour appearance is set).
void Editor::SetRepresentationColour(std::string_view charBytes, int colourAlpha) {
	reprs->SetRepresentationColour(charBytes, ColourRGBA(colourAlpha));
}

// Packed colouralpha, or 0 when none.
int Editor::GetRepresentationColour(std::string_view charBytes) const {
	const Representation *repr = reprs->RepresentationFromCharacter(charBytes);
	if (repr)
		return repr->colour.AsInteger();
	return 0;
}

// Active hotspot foreground. useSetting false clears the override.
void Editor::SetHotspotActiveFore(bool useSetting, int rgb) {
	if (vs.SetElementColourOptional(Element::HotSpotActive, useSetting, rgb)) {
		InvalidateStyleRedraw();
	}
}

// Opaque RGB of the active hotspot foreground, or 0 when unset.
int Editor::GetHotspotActiveFore() const {
	return vs.ElementColour(Element::HotSpotActive).value_or(ColourRGBA()).OpaqueRGB();
}

// Active hotspot background. useSetting false clears the override.
void Editor::SetHotspotActiveBack(bool useSetting, int rgb) {
	if (vs.SetElementColourOptional(Element::HotSpotActiveBack, useSetting, rgb)) {
		InvalidateStyleRedraw();
	}
}

// Opaque RGB of the active hotspot background, or 0 when unset.
int Editor::GetHotspotActiveBack() const {
	return vs.ElementColour(Element::HotSpotActiveBack).value_or(ColourRGBA()).OpaqueRGB();
}

// Underline hotspots when active.
void Editor::SetHotspotActiveUnderline(bool underline) {
	vs.hotspotUnderline = underline;
	InvalidateStyleRedraw();
}

// Whether hotspots use an underline when active.
bool Editor::GetHotspotActiveUnderline() const noexcept {
	return vs.hotspotUnderline;
}

// When true, hotspot runs stop at line ends.
void Editor::SetHotspotSingleLine(bool singleLine) {
	hotspotSingleLine = singleLine;
	InvalidateStyleRedraw();
}

// Whether hotspot runs are limited to one line.
bool Editor::GetHotspotSingleLine() const noexcept {
	return hotspotSingleLine;
}

// Read-only annotation text under line (UTF-8). nullptr clears.
void Editor::AnnotationSetText(Sci::Line line, const char *text) {
	pdoc->AnnotationSetText(line, text);
}

// Annotation text for line.
std::string Editor::AnnotationGetText(Sci::Line line) const {
	return std::string(pdoc->AnnotationStyledText(line).AsView());
}

// One style for the whole annotation on line (before style offset).
void Editor::AnnotationSetStyle(Sci::Line line, int style) {
	pdoc->AnnotationSetStyle(line, style);
}

// Style index for annotation text on line, or 0.
int Editor::AnnotationGetStyle(Sci::Line line) const noexcept {
	return static_cast<int>(pdoc->AnnotationStyledText(line).style);
}

// Per-byte style indices for annotation text.
void Editor::AnnotationSetStyles(Sci::Line line, const unsigned char *styles) {
	pdoc->AnnotationSetStyles(line, styles);
}

// Copy per-character style bytes into buffer. When the line uses one shared style, returns 0 and, if buffer is non-null and text exists, writes a single 0 byte for compatibility. A null-buffer call cannot measure that byte. When per-character styles exist, returns their length.
Sci::Position Editor::AnnotationGetStyles(Sci::Line line, char *buffer) const {
	const StyledText st = pdoc->AnnotationStyledText(line);
	if (buffer && st.length > 0) {
		if (st.styles)
			std::memcpy(buffer, st.styles, st.length);
		else
			*buffer = 0;
	}
	return st.styles ? static_cast<Sci::Position>(st.length) : 0;
}

// Number of display lines the annotation on line occupies.
int Editor::AnnotationGetLines(Sci::Line line) const noexcept {
	return pdoc->AnnotationLines(line);
}

// Clear all annotations in the document.
void Editor::AnnotationClearAll() {
	pdoc->AnnotationClearAll();
}

void Editor::SetAnnotationVisible(AnnotationVisible visible) {
	if (vs.annotationVisible != visible) {
		const bool changedFromOrToHidden = ((vs.annotationVisible != AnnotationVisible::Hidden) != (visible != AnnotationVisible::Hidden));
		vs.annotationVisible = visible;
		if (changedFromOrToHidden) {
			const int dir = (vs.annotationVisible != AnnotationVisible::Hidden) ? 1 : -1;
			for (Sci::Line line = 0; line < pdoc->LinesTotal(); line++) {
				const int annotationLines = pdoc->AnnotationLines(line);
				if (annotationLines > 0) {
					pcs->SetHeight(line, pcs->GetHeight(line) + annotationLines * dir);
				}
			}
			SetScrollBars();
		}
		Redraw();
	}
}

// Annotation visibility mode.
AnnotationVisible Editor::AnnotationGetVisible() const noexcept {
	return vs.annotationVisible;
}

// Added to annotation style numbers before look-up so they stay off lexer styles.
void Editor::AnnotationSetStyleOffset(int style) {
	vs.annotationStyleOffset = style;
	InvalidateStyleRedraw();
}

// Style index base for annotations.
int Editor::AnnotationGetStyleOffset() const noexcept {
	return vs.annotationStyleOffset;
}

// Read-only text at the end of line (UTF-8). nullptr clears.
void Editor::EOLAnnotationSetText(Sci::Line line, const char *text) {
	pdoc->EOLAnnotationSetText(line, text);
}

// EOL annotation text for line.
std::string Editor::EOLAnnotationGetText(Sci::Line line) const {
	return std::string(pdoc->EOLAnnotationStyledText(line).AsView());
}

// Style for the EOL annotation on line.
void Editor::EOLAnnotationSetStyle(Sci::Line line, int style) {
	pdoc->EOLAnnotationSetStyle(line, style);
}

// Style index for EOL annotation on line, or 0.
int Editor::EOLAnnotationGetStyle(Sci::Line line) const noexcept {
	return static_cast<int>(pdoc->EOLAnnotationStyledText(line).style);
}

// Clear all end-of-line annotations.
void Editor::EOLAnnotationClearAll() {
	pdoc->EOLAnnotationClearAll();
}

void Editor::SetEOLAnnotationVisible(EOLAnnotationVisible visible) {
	if (vs.eolAnnotationVisible != visible) {
		vs.eolAnnotationVisible = visible;
		Redraw();
	}
}

// EOL annotation visibility / shape mode.
EOLAnnotationVisible Editor::EOLAnnotationGetVisible() const noexcept {
	return vs.eolAnnotationVisible;
}

// Style index base for EOL annotations.
void Editor::EOLAnnotationSetStyleOffset(int style) {
	vs.eolAnnotationStyleOffset = style;
	InvalidateStyleRedraw();
}

// Style index base for EOL annotations.
int Editor::EOLAnnotationGetStyleOffset() const noexcept {
	return vs.eolAnnotationStyleOffset;
}
