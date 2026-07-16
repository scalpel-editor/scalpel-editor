// Scintilla source code edit control
/** @file EditorCaret.cxx
 ** Caret appearance, blink, sticky policy, caret line highlight, and caret-relative scrolling.
 **
 ** The main caret colour and style apply to the primary caret. Additional carets (multi-selection)
 ** have separate colour, blink, and visibility settings. Caret-line background can fill the whole
 ** line or a frame, on the base layer or translucently over text. Sticky caret keeps the preferred
 ** column when moving vertically through short lines; WhiteSpace also sticks at indentation edges.
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

// Scroll so the main caret's document line is vertically centred in the view.
void Editor::VerticalCentreCaret() {
	const Sci::Line lineDoc =
		pdoc->SciLineFromPosition(sel.IsRectangular() ? sel.Rectangular().caret.Position() : sel.MainCaret());
	const Sci::Line lineDisplay = pcs->DisplayFromDoc(lineDoc);
	const Sci::Line newTop = lineDisplay - (LinesOnScreen() / 2);
	if (topLine != newTop) {
		SetTopLine(newTop > 0 ? newTop : 0);
		SetVerticalScrollPos();
		RedrawRect(GetClientRectangle());
	}
}

// If the caret is above or below the text area, move it to the nearest fully visible line
// at the remembered horizontal preference (lastXChosen). The selection is replaced by that caret.
// ensureVisible is passed through to MovePositionTo for follow-on scroll policy.
void Editor::MoveCaretInsideView(bool ensureVisible) {
	const PRectangle rcClient = GetTextRectangle();
	const Point pt = PointMainCaret();
	if (pt.y < rcClient.top) {
		MovePositionTo(SPositionFromLocation(
		            Point::FromInts(lastXChosen - xOffset, static_cast<int>(rcClient.top)),
					false, false, UserVirtualSpace()),
					Selection::SelTypes::none, ensureVisible);
	} else if ((pt.y + vs.lineHeight - 1) > rcClient.bottom) {
		const ptrdiff_t yOfLastLineFullyDisplayed = static_cast<ptrdiff_t>(rcClient.top) + ((LinesOnScreen() - 1) * vs.lineHeight);
		MovePositionTo(SPositionFromLocation(
		            Point::FromInts(lastXChosen - xOffset, static_cast<int>(rcClient.top + static_cast<XYPOSITION>(yOfLastLineFullyDisplayed))),
					false, false, UserVirtualSpace()),
		        Selection::SelTypes::none, ensureVisible);
	}
}

// Start or stop caret blinking for the focused window and force a caret redraw.
// Without focus the caret is inactive and not blinking.
void Editor::ShowCaretAtCurrentPosition() {
	if (hasFocus) {
		caret.active = true;
		caret.on = true;
		FineTickerCancel(TickReason::caret);
		if (caret.period > 0)
			FineTickerStart(TickReason::caret, caret.period, caret.period/10);
	} else {
		caret.active = false;
		caret.on = false;
		FineTickerCancel(TickReason::caret);
	}
	InvalidateCaret();
}

// Stop caret activity and blinking (for example while the host takes over drawing).
void Editor::DropCaret() {
	caret.active = false;
	FineTickerCancel(TickReason::caret);
	InvalidateCaret();
}

// Internal period setter used by SetCaretPeriod. period 0 stops blinking.
void Editor::CaretSetPeriod(int period) {
	if (caret.period != period) {
		caret.period = period;
		caret.on = true;
		FineTickerCancel(TickReason::caret);
		if ((caret.active) && (caret.period > 0))
			FineTickerStart(TickReason::caret, caret.period, caret.period/10);
		InvalidateCaret();
	}
}

// Invalidate the drag caret or every selection caret and update the system caret.
void Editor::InvalidateCaret() {
	if (posDrag.IsValid()) {
		InvalidateRange(posDrag.Position(), posDrag.Position() + 1);
	} else {
		for (size_t r=0; r<sel.Count(); r++) {
			InvalidateRange(sel.Range(r).caret.Position(), sel.Range(r).caret.Position() + 1);
		}
	}
	UpdateSystemCaret();
}

// Scroll so the caret is visible according to the current X/Y caret policies.
void Editor::ScrollCaret() {
	EnsureCaretVisible();
}

// Remember the current caret x as the preferred column for vertical motion.
void Editor::ChooseCaretX() {
	SetLastXChosen();
}

// Milliseconds the caret is visible or invisible before toggling. 0 means no blink. Default 500.
int Editor::GetCaretPeriod() const noexcept {
	return caret.period;
}

// Set caret blink period in milliseconds. 0 stops blinking.
void Editor::SetCaretPeriod(int periodMilliseconds) {
	CaretSetPeriod(periodMilliseconds);
}

// Sticky caret: Off (default) moves to the end of short lines; On keeps the preferred column
// in virtual space; WhiteSpace also sticks when leaving or entering indentation.
void Editor::SetCaretSticky(CaretSticky sticky) {
	if (sticky <= CaretSticky::WhiteSpace) {
		caretSticky = sticky;
	}
}

// Current sticky-caret mode.
CaretSticky Editor::GetCaretSticky() const noexcept {
	return caretSticky;
}

// Toggle between Off and On (does not cycle through WhiteSpace).
void Editor::ToggleCaretSticky() {
	caretSticky = (caretSticky == CaretSticky::Off) ? CaretSticky::On : CaretSticky::Off;
}

// True when a caret-line background colour is set (legacy visibility).
bool Editor::GetCaretLineVisible() const noexcept {
	return static_cast<bool>(vs.ElementColour(Element::CaretLineBack));
}

// Enable or disable the caret-line background using the legacy colour element.
// Prefer element colours when a full alpha channel is needed.
void Editor::SetCaretLineVisible(bool show) {
	if (show) {
		if (!vs.elementColours.count(Element::CaretLineBack)) {
			vs.elementColours[Element::CaretLineBack] = ColourRGBA(maximumByte, maximumByte, 0);
			InvalidateStyleRedraw();
		}
	} else {
		if (vs.ResetElement(Element::CaretLineBack)) {
			InvalidateStyleRedraw();
		}
	}
}

// When true, the caret line stays highlighted even without keyboard focus.
bool Editor::GetCaretLineVisibleAlways() const noexcept {
	return vs.caretLine.alwaysShow;
}

// Keep the caret-line highlight even without keyboard focus.
void Editor::SetCaretLineVisibleAlways(bool alwaysShow) {
	vs.caretLine.alwaysShow = alwaysShow;
	InvalidateStyleRedraw();
}

// When true, only the wrapped sub-line containing the caret is highlighted.
bool Editor::GetCaretLineHighlightSubLine() const noexcept {
	return vs.caretLine.subLine;
}

// Highlight only the wrapped sub-line that contains the caret.
void Editor::SetCaretLineHighlightSubLine(bool subLine) {
	vs.caretLine.subLine = subLine;
	InvalidateStyleRedraw();
}

// Frame thickness in pixels around the caret line; 0 fills the whole background (default).
int Editor::GetCaretLineFrame() const noexcept {
	return vs.caretLine.frame;
}

// Frame thickness in pixels; 0 fills the whole caret-line background.
void Editor::SetCaretLineFrame(int width) {
	vs.caretLine.frame = width;
	InvalidateStyleRedraw();
}

// Opaque RGB of the caret-line background (legacy API).
int Editor::GetCaretLineBack() const noexcept {
	return vs.ElementColourForced(Element::CaretLineBack).OpaqueRGB();
}

// Opaque RGB caret-line background (legacy API).
void Editor::SetCaretLineBack(int rgb) {
	vs.SetElementRGB(Element::CaretLineBack, rgb);
	InvalidateStyleRedraw();
}

// Layer for caret-line drawing: Base is opaque under text; OverText is translucent over glyphs.
Layer Editor::GetCaretLineLayer() const noexcept {
	return vs.caretLine.layer;
}

// Layer for caret-line drawing (Base or OverText).
void Editor::SetCaretLineLayer(Layer layer) {
	if (vs.caretLine.layer != layer) {
		vs.caretLine.layer = layer;
		UpdateBaseElements();
		InvalidateStyleRedraw();
	}
}

// Alpha of the caret-line colour, or NoAlpha when drawn on the base layer.
int Editor::GetCaretLineBackAlpha() const noexcept {
	if (vs.caretLine.layer == Layer::Base)
		return static_cast<int>(Alpha::NoAlpha);
	return vs.ElementColour(Element::CaretLineBack).value_or(ColourRGBA()).GetAlpha();
}

// Set caret-line alpha. NoAlpha forces the base (opaque) layer; other values use over-text.
void Editor::SetCaretLineBackAlpha(int alpha) {
	const Layer layerNew = (static_cast<Alpha>(alpha) == Alpha::NoAlpha) ? Layer::Base : Layer::OverText;
	vs.caretLine.layer = layerNew;
	if (vs.ElementColour(Element::CaretLineBack)) {
		vs.SetElementAlpha(Element::CaretLineBack, alpha);
	}
	InvalidateStyleRedraw();
}

// Horizontal caret visibility policy: combination of Slop, Strict, Jumps, Even, and a pixel slop.
void Editor::SetXCaretPolicy(CaretPolicy policy, int slop) {
	caretPolicies.x = CaretPolicySlop(policy, slop);
}

// Vertical caret visibility policy: same flags as X, with slop in lines.
void Editor::SetYCaretPolicy(CaretPolicy policy, int slop) {
	caretPolicies.y = CaretPolicySlop(policy, slop);
}

// Main caret colour (opaque RGB). Element::Caret also supports alpha when set through element APIs.
void Editor::SetCaretFore(int rgb) {
	vs.elementColours[Element::Caret] = ColourRGBA::FromIpRGB(rgb);
	InvalidateStyleRedraw();
}

// Opaque RGB of the main caret.
int Editor::GetCaretFore() const noexcept {
	return vs.ElementColourForced(Element::Caret).OpaqueRGB();
}

// Caret shape: line/block/invisible for insert mode, overstrike bar/block, and curses block options.
// Out-of-range values fall back to a line caret.
void Editor::SetCaretStyle(CaretStyle style) {
	if (style <= (CaretStyle::Block | CaretStyle::OverstrikeBlock | CaretStyle::Curses | CaretStyle::BlockAfter))
		vs.caret.style = style;
	else
		vs.caret.style = CaretStyle::Line;
	InvalidateStyleRedraw();
}

// Current caret style flags.
CaretStyle Editor::GetCaretStyle() const noexcept {
	return vs.caret.style;
}

// Line-caret width in pixels, clamped to 0..20. 0 hides a line caret. Block carets ignore this.
void Editor::SetCaretWidth(int pixelWidth) {
	vs.caret.width = std::clamp(pixelWidth, 0, 20);
	InvalidateStyleRedraw();
}

// Line-caret width in pixels.
int Editor::GetCaretWidth() const noexcept {
	return vs.caret.width;
}

// Whether additional (non-main) carets blink. Defaults to following the main caret period when true.
void Editor::SetAdditionalCaretsBlink(bool blink) {
	view.additionalCaretsBlink = blink;
	InvalidateCaret();
}

// True when additional carets blink.
bool Editor::GetAdditionalCaretsBlink() const noexcept {
	return view.additionalCaretsBlink;
}

// Whether additional carets are drawn. Default true.
void Editor::SetAdditionalCaretsVisible(bool visible) {
	view.additionalCaretsVisible = visible;
	InvalidateCaret();
}

// True when additional carets are drawn.
bool Editor::GetAdditionalCaretsVisible() const noexcept {
	return view.additionalCaretsVisible;
}

// Colour of additional carets so they can differ from the main caret.
void Editor::SetAdditionalCaretFore(int rgb) {
	vs.elementColours[Element::CaretAdditional] = ColourRGBA::FromIpRGB(rgb);
	InvalidateStyleRedraw();
}

// Opaque RGB of additional carets.
int Editor::GetAdditionalCaretFore() const noexcept {
	return vs.ElementColourForced(Element::CaretAdditional).OpaqueRGB();
}
