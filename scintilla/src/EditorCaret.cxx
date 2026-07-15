// Scintilla source code edit control
/** @file EditorCaret.cxx
 ** Caret appearance, blink, sticky policy, and caret-relative scrolling.
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


void Editor::DropCaret() {
	caret.active = false;
	FineTickerCancel(TickReason::caret);
	InvalidateCaret();
}


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


// Scroll so the caret is visible.
void Editor::ScrollCaret() {
	EnsureCaretVisible();
}

// Remember the current caret x as the preferred column for vertical motion.
void Editor::ChooseCaretX() {
	SetLastXChosen();
}

int Editor::GetCaretPeriod() const noexcept {
	return caret.period;
}

void Editor::SetCaretPeriod(int periodMilliseconds) {
	CaretSetPeriod(periodMilliseconds);
}

void Editor::SetCaretSticky(CaretSticky sticky) {
	if (sticky <= CaretSticky::WhiteSpace) {
		caretSticky = sticky;
	}
}

CaretSticky Editor::GetCaretSticky() const noexcept {
	return caretSticky;
}

void Editor::ToggleCaretSticky() {
	caretSticky = (caretSticky == CaretSticky::Off) ? CaretSticky::On : CaretSticky::Off;
}

bool Editor::GetCaretLineVisible() const noexcept {
	return static_cast<bool>(vs.ElementColour(Element::CaretLineBack));
}

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

bool Editor::GetCaretLineVisibleAlways() const noexcept {
	return vs.caretLine.alwaysShow;
}

void Editor::SetCaretLineVisibleAlways(bool alwaysShow) {
	vs.caretLine.alwaysShow = alwaysShow;
	InvalidateStyleRedraw();
}

bool Editor::GetCaretLineHighlightSubLine() const noexcept {
	return vs.caretLine.subLine;
}

void Editor::SetCaretLineHighlightSubLine(bool subLine) {
	vs.caretLine.subLine = subLine;
	InvalidateStyleRedraw();
}

int Editor::GetCaretLineFrame() const noexcept {
	return vs.caretLine.frame;
}

void Editor::SetCaretLineFrame(int width) {
	vs.caretLine.frame = width;
	InvalidateStyleRedraw();
}

int Editor::GetCaretLineBack() const noexcept {
	return vs.ElementColourForced(Element::CaretLineBack).OpaqueRGB();
}

void Editor::SetCaretLineBack(int rgb) {
	vs.SetElementRGB(Element::CaretLineBack, rgb);
	InvalidateStyleRedraw();
}

Layer Editor::GetCaretLineLayer() const noexcept {
	return vs.caretLine.layer;
}

void Editor::SetCaretLineLayer(Layer layer) {
	if (vs.caretLine.layer != layer) {
		vs.caretLine.layer = layer;
		UpdateBaseElements();
		InvalidateStyleRedraw();
	}
}

int Editor::GetCaretLineBackAlpha() const noexcept {
	if (vs.caretLine.layer == Layer::Base)
		return static_cast<int>(Alpha::NoAlpha);
	return vs.ElementColour(Element::CaretLineBack).value_or(ColourRGBA()).GetAlpha();
}

void Editor::SetCaretLineBackAlpha(int alpha) {
	const Layer layerNew = (static_cast<Alpha>(alpha) == Alpha::NoAlpha) ? Layer::Base : Layer::OverText;
	vs.caretLine.layer = layerNew;
	if (vs.ElementColour(Element::CaretLineBack)) {
		vs.SetElementAlpha(Element::CaretLineBack, alpha);
	}
	InvalidateStyleRedraw();
}

void Editor::SetXCaretPolicy(uptr_t policy, sptr_t slop) {
	caretPolicies.x = CaretPolicySlop(policy, slop);
}

void Editor::SetYCaretPolicy(uptr_t policy, sptr_t slop) {
	caretPolicies.y = CaretPolicySlop(policy, slop);
}

void Editor::SetCaretFore(int rgb) {
	vs.elementColours[Element::Caret] = ColourRGBA::FromIpRGB(rgb);
	InvalidateStyleRedraw();
}

int Editor::GetCaretFore() const noexcept {
	return vs.ElementColourForced(Element::Caret).OpaqueRGB();
}

void Editor::SetCaretStyle(CaretStyle style) {
	if (style <= (CaretStyle::Block | CaretStyle::OverstrikeBlock | CaretStyle::Curses | CaretStyle::BlockAfter))
		vs.caret.style = style;
	else
		vs.caret.style = CaretStyle::Line;
	InvalidateStyleRedraw();
}

CaretStyle Editor::GetCaretStyle() const noexcept {
	return vs.caret.style;
}

void Editor::SetCaretWidth(int pixelWidth) {
	vs.caret.width = std::clamp(pixelWidth, 0, 20);
	InvalidateStyleRedraw();
}

int Editor::GetCaretWidth() const noexcept {
	return vs.caret.width;
}

void Editor::SetAdditionalCaretsBlink(bool blink) {
	view.additionalCaretsBlink = blink;
	InvalidateCaret();
}

bool Editor::GetAdditionalCaretsBlink() const noexcept {
	return view.additionalCaretsBlink;
}

void Editor::SetAdditionalCaretsVisible(bool visible) {
	view.additionalCaretsVisible = visible;
	InvalidateCaret();
}

bool Editor::GetAdditionalCaretsVisible() const noexcept {
	return view.additionalCaretsVisible;
}

void Editor::SetAdditionalCaretFore(int rgb) {
	vs.elementColours[Element::CaretAdditional] = ColourRGBA::FromIpRGB(rgb);
	InvalidateStyleRedraw();
}

int Editor::GetAdditionalCaretFore() const noexcept {
	return vs.ElementColourForced(Element::CaretAdditional).OpaqueRGB();
}

