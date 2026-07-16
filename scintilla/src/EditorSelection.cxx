// Scintilla source code edit control
/** @file EditorSelection.cxx
 ** Selection ranges, multi-selection, and navigation into a selection.
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


// --- Application-facing selection surface ---

// Caret position of the main selection, or of the rectangular caret when the
// selection is rectangular.
Sci::Position Editor::GetCurrentPos() noexcept {
	return sel.IsRectangular() ? sel.Rectangular().caret.Position() : sel.MainCaret();
}

// Anchor position of the main selection, or of the rectangular anchor when
// rectangular.
Sci::Position Editor::GetAnchor() noexcept {
	return sel.IsRectangular() ? sel.Rectangular().anchor.Position() : sel.MainAnchor();
}

// Moves the main caret (or rectangular caret) to pos without changing the
// opposite end of the selection.
void Editor::SetCurrentPos(Sci::Position pos) {
	if (sel.IsRectangular()) {
		sel.Rectangular().caret.SetPosition(pos);
		SetRectangularRange();
		Redraw();
	} else {
		SetSelection(pos, sel.MainAnchor());
	}
}

// Moves the main anchor (or rectangular anchor) to pos without changing the
// caret end of the selection.
void Editor::SetAnchor(Sci::Position pos) {
	if (sel.IsRectangular()) {
		sel.Rectangular().anchor.SetPosition(pos);
		SetRectangularRange();
		Redraw();
	} else {
		SetSelection(sel.MainCaret(), pos);
	}
}

// Sets the lower bound of the main selection. The caret becomes max(caret, pos)
// and the anchor becomes pos.
void Editor::SetSelectionStart(Sci::Position pos) {
	SetSelection(std::max(sel.MainCaret(), pos), pos);
}

// Start of the main selection, or of the rectangular extent when rectangular.
Sci::Position Editor::GetSelectionStart() const noexcept {
	return sel.LimitsForRectangularElseMain().start.Position();
}

// Sets the upper bound of the main selection. The caret becomes pos and the
// anchor becomes min(anchor, pos).
void Editor::SetSelectionEnd(Sci::Position pos) {
	SetSelection(pos, std::min(sel.MainAnchor(), pos));
}

// End of the main selection, or of the rectangular extent when rectangular.
Sci::Position Editor::GetSelectionEnd() const noexcept {
	return sel.LimitsForRectangularElseMain().end.Position();
}

// Replaces the selection with a stream range from start to end. A negative end
// means the document end. A negative start collapses to an empty selection at
// end. Ensures the caret is visible.
void Editor::SetSel(Sci::Position start, Sci::Position end) {
	if (end < 0)
		end = pdoc->Length();
	if (start < 0)
		start = end;
	InvalidateSelection(SelectionRange(start, end));
	sel.Clear();
	sel.selType = Selection::SelTypes::stream;
	SetSelection(end, start);
	EnsureCaretVisible();
}

// Text of the current selection(s), joined with the copy separator for multi
// stream selections or the document EOL for rectangular selections.
std::string Editor::GetSelText() {
	SelectionText selectedText;
	CopySelectionRange(&selectedText);
	if (selectedText.Length() == 0) {
		return {};
	}
	return std::string(selectedText.Data(), selectedText.Length());
}

// True when every selection range is empty (caret only).
bool Editor::GetSelectionEmpty() const noexcept {
	return sel.Empty();
}

// Moves an empty selection to the start of lineNo (clamped) and scrolls it into view.
void Editor::GotoLine(Sci::Line lineNo) {
	if (lineNo > pdoc->LinesTotal())
		lineNo = pdoc->LinesTotal();
	if (lineNo < 0)
		lineNo = 0;
	SetEmptySelection(pdoc->LineStart(lineNo));
	ShowCaretAtCurrentPosition();
	EnsureCaretVisible();
}

// Places an empty selection at pos and ensures the caret is visible.
void Editor::GotoPos(Sci::Position pos) {
	SetEmptySelection(pos);
	EnsureCaretVisible();
}

// --- Moved selection helpers ---


// Hide or show the selection drawing without changing selection positions.
void Editor::HideSelection(bool hide) {
	vs.selection.visible = !hide;
	Redraw();
}

// True when selection drawing is suppressed (HideSelection).
bool Editor::GetSelectionHidden() const noexcept {
	return !vs.selection.visible;
}

// Sets the search/replace target to the main selection range.
void Editor::TargetFromSelection() {
	targetRange.start = sel.RangeMain().Start();
	targetRange.end = sel.RangeMain().End();
}

// True when the selection is rectangular.
bool Editor::SelectionIsRectangle() const noexcept {
	return sel.selType == Selection::SelTypes::rectangle;
}

// Stream, Rectangle, Lines, or Thin selection mode.
SelectionMode Editor::GetSelectionMode() const noexcept {
	switch (sel.selType) {
	case Selection::SelTypes::stream:
		return SelectionMode::Stream;
	case Selection::SelTypes::rectangle:
		return SelectionMode::Rectangle;
	case Selection::SelTypes::lines:
		return SelectionMode::Lines;
	case Selection::SelTypes::thin:
		return SelectionMode::Thin;
	default:
		return SelectionMode::Stream;
	}
}

// When true, caret moves extend the selection (like holding Shift).
void Editor::SetMoveExtendsSelection(bool moveExtends) {
	sel.SetMoveExtends(moveExtends);
}

// True when caret moves extend the selection.
bool Editor::GetMoveExtendsSelection() const noexcept {
	return sel.MoveExtends();
}

// When true, Alt+drag can switch a stream selection into rectangular.
void Editor::SetMouseSelectionRectangularSwitch(bool enable) {
	mouseSelectionRectangularSwitch = enable;
}

// True when mouse may switch into rectangular selection.
bool Editor::GetMouseSelectionRectangularSwitch() const noexcept {
	return mouseSelectionRectangularSwitch;
}

// When true, multiple selections are allowed.
void Editor::SetMultipleSelection(bool enable) {
	multipleSelection = enable;
	InvalidateCaret();
}

// True when multiple selections are allowed.
bool Editor::GetMultipleSelection() const noexcept {
	return multipleSelection;
}

// When true, typing and deletion affect every selection, not only the main one.
void Editor::SetAdditionalSelectionTyping(bool enable) {
	additionalSelectionTyping = enable;
	InvalidateCaret();
}

// True when typing applies to every selection, not only the main one.
bool Editor::GetAdditionalSelectionTyping() const noexcept {
	return additionalSelectionTyping;
}

// Number of selection ranges (at least 1).
size_t Editor::GetSelections() const noexcept {
	return sel.Count();
}

// Collapse to a single empty selection at the main caret.
void Editor::ClearSelections() {
	sel.Clear();
	ContainerNeedsUpdate(Update::Selection);
	Redraw();
}

// Message SetSelection: replace the selection model with one stream range.
void Editor::SetStreamSelection(Sci::Position caret, Sci::Position anchor) {
	sel.SetSelection(SelectionRange(caret, anchor));
	Redraw();
}

// Append a caret/anchor range and make it the main selection.
void Editor::AddSelection(Sci::Position caret, Sci::Position anchor) {
	sel.AddSelection(SelectionRange(caret, anchor));
	ContainerNeedsUpdate(Update::Selection);
	Redraw();
}

// Choose which selection range is main (receives typing and clipboard).
void Editor::SetMainSelection(size_t selection) {
	sel.SetMain(selection);
	ContainerNeedsUpdate(Update::Selection);
	Redraw();
}

// Index of the main selection range.
size_t Editor::GetMainSelection() const noexcept {
	return sel.Main();
}

// Draw selection background on Base, UnderText, or OverText.
void Editor::SetSelectionLayer(Layer layer) {
	if (vs.selection.layer != layer) {
		vs.selection.layer = layer;
		UpdateBaseElements();
		InvalidateStyleRedraw();
	}
}

// Layer used to draw the selection background.
Layer Editor::GetSelectionLayer() const noexcept {
	return vs.selection.layer;
}

// Whether undo/redo restores selection (and optionally scroll position).
void Editor::SetUndoSelectionHistory(UndoSelectionHistoryOption option) {
	ChangeUndoSelectionHistory(option);
}

// Current undo selection-history mode.
UndoSelectionHistoryOption Editor::GetUndoSelectionHistory() const noexcept {
	return undoSelectionHistoryOption;
}

// Restore selection ranges from a serialized string.
void Editor::SetSelectionSerialized(std::string_view serialized) {
	SetSelectionFromSerialized(std::string(serialized).c_str());
}

// Serialize all selection ranges to a string.
std::string Editor::GetSelectionSerialized() const {
	return sel.ToString();
}

// Caret position of the rectangular selection.
void Editor::SetRectangularSelectionCaret(Sci::Position pos) {
	if (!sel.IsRectangular())
		sel.Clear();
	sel.selType = Selection::SelTypes::rectangle;
	sel.Rectangular().caret.SetPosition(pos);
	SetRectangularRange();
	Redraw();
}

// Caret position of the rectangular selection.
Sci::Position Editor::GetRectangularSelectionCaret() noexcept {
	return sel.Rectangular().caret.Position();
}

// Anchor position of the rectangular selection.
void Editor::SetRectangularSelectionAnchor(Sci::Position pos) {
	if (!sel.IsRectangular())
		sel.Clear();
	sel.selType = Selection::SelTypes::rectangle;
	sel.Rectangular().anchor.SetPosition(pos);
	SetRectangularRange();
	Redraw();
}

// Anchor position of the rectangular selection.
Sci::Position Editor::GetRectangularSelectionAnchor() noexcept {
	return sel.Rectangular().anchor.Position();
}

// Virtual space of the rectangular caret.
void Editor::SetRectangularSelectionCaretVirtualSpace(Sci::Position space) {
	if (!sel.IsRectangular())
		sel.Clear();
	sel.selType = Selection::SelTypes::rectangle;
	sel.Rectangular().caret.SetVirtualSpace(space);
	SetRectangularRange();
	Redraw();
}

// Virtual space of the rectangular caret.
Sci::Position Editor::GetRectangularSelectionCaretVirtualSpace() noexcept {
	return sel.Rectangular().caret.VirtualSpace();
}

// Virtual space of the rectangular anchor.
void Editor::SetRectangularSelectionAnchorVirtualSpace(Sci::Position space) {
	if (!sel.IsRectangular())
		sel.Clear();
	sel.selType = Selection::SelTypes::rectangle;
	sel.Rectangular().anchor.SetVirtualSpace(space);
	SetRectangularRange();
	Redraw();
}

// Virtual space of the rectangular anchor.
Sci::Position Editor::GetRectangularSelectionAnchorVirtualSpace() noexcept {
	return sel.Rectangular().anchor.VirtualSpace();
}

// Caret position of selection range selection.
Sci::Position Editor::GetSelectionNCaret(size_t selection) const noexcept {
	return sel.Range(selection).caret.Position();
}

// Anchor position of selection range selection.
Sci::Position Editor::GetSelectionNAnchor(size_t selection) const noexcept {
	return sel.Range(selection).anchor.Position();
}

// Virtual space of caret for selection range selection.
Sci::Position Editor::GetSelectionNCaretVirtualSpace(size_t selection) const noexcept {
	return sel.Range(selection).caret.VirtualSpace();
}

// Virtual space of anchor for selection range selection.
Sci::Position Editor::GetSelectionNAnchorVirtualSpace(size_t selection) const noexcept {
	return sel.Range(selection).anchor.VirtualSpace();
}

// Ordered start of selection range selection.
Sci::Position Editor::GetSelectionNStart(size_t selection) const noexcept {
	return sel.Range(selection).Start().Position();
}

// Virtual space at the start of selection range selection.
Sci::Position Editor::GetSelectionNStartVirtualSpace(size_t selection) const noexcept {
	return sel.Range(selection).Start().VirtualSpace();
}

// Ordered end of selection range selection.
Sci::Position Editor::GetSelectionNEnd(size_t selection) const noexcept {
	return sel.Range(selection).End().Position();
}

// Virtual space at the end of selection range selection.
Sci::Position Editor::GetSelectionNEndVirtualSpace(size_t selection) const noexcept {
	return sel.Range(selection).End().VirtualSpace();
}

// Set the main selection to the caret/anchor range (positions or SelectionPosition).
void Editor::SetSelection(SelectionPosition currentPos_, SelectionPosition anchor_) {
	currentPos_ = ClampPositionIntoDocument(currentPos_);
	anchor_ = ClampPositionIntoDocument(anchor_);
	const Sci::Line currentLine = pdoc->SciLineFromPosition(currentPos_.Position());
	SelectionRange rangeNew(currentPos_, anchor_);
	if (sel.selType == Selection::SelTypes::lines) {
		rangeNew = LineSelectionRange(currentPos_, anchor_);
	}
	if (sel.Count() > 1 || !(sel.RangeMain() == rangeNew)) {
		InvalidateSelection(rangeNew);
	}
	sel.RangeMain() = rangeNew;
	SetRectangularRange();
	ClaimSelection();
	SetHoverIndicatorPosition(sel.MainCaret());

	if (marginView.highlightDelimiter.NeedsDrawing(currentLine)) {
		RedrawSelMargin();
	}
	QueueIdleWork(WorkItems::updateUI);
}

// Set the main selection to the caret/anchor range (positions or SelectionPosition).
void Editor::SetSelection(Sci::Position currentPos_, Sci::Position anchor_) {
	SetSelection(SelectionPosition(currentPos_), SelectionPosition(anchor_));
}

// Just move the caret on the main selection
void Editor::SetSelection(SelectionPosition currentPos_) {
	currentPos_ = ClampPositionIntoDocument(currentPos_);
	const Sci::Line currentLine = pdoc->SciLineFromPosition(currentPos_.Position());
	if (sel.Count() > 1 || !(sel.RangeMain().caret == currentPos_)) {
		InvalidateSelection(SelectionRange(currentPos_));
	}
	if (sel.IsRectangular()) {
		sel.Rectangular() =
			SelectionRange(SelectionPosition(currentPos_), sel.Rectangular().anchor);
		SetRectangularRange();
	} else if (sel.selType == Selection::SelTypes::lines) {
		sel.RangeMain() = LineSelectionRange(currentPos_, sel.RangeMain().anchor);
	} else {
		sel.RangeMain() =
			SelectionRange(SelectionPosition(currentPos_), sel.RangeMain().anchor);
	}
	ClaimSelection();
	SetHoverIndicatorPosition(sel.MainCaret());

	if (marginView.highlightDelimiter.NeedsDrawing(currentLine)) {
		RedrawSelMargin();
	}
	QueueIdleWork(WorkItems::updateUI);
}

// Collapse to an empty selection at caret (position or SelectionPosition).
void Editor::SetEmptySelection(SelectionPosition currentPos_) {
	const Sci::Line currentLine = pdoc->SciLineFromPosition(currentPos_.Position());
	SelectionRange rangeNew(ClampPositionIntoDocument(currentPos_));
	if (sel.Count() > 1 || !(sel.RangeMain() == rangeNew)) {
		InvalidateSelection(rangeNew);
	}
	sel.Clear();
	sel.RangeMain() = rangeNew;
	SetRectangularRange();
	ClaimSelection();
	SetHoverIndicatorPosition(sel.MainCaret());

	if (marginView.highlightDelimiter.NeedsDrawing(currentLine)) {
		RedrawSelMargin();
	}
	QueueIdleWork(WorkItems::updateUI);
}

// Collapse to an empty selection at caret (position or SelectionPosition).
void Editor::SetEmptySelection(Sci::Position currentPos_) {
	SetEmptySelection(SelectionPosition(currentPos_));
}

// Apply a previously serialized selection string.
void Editor::SetSelectionFromSerialized(const char *serialized) {
	if (serialized) {
		sel = Selection(serialized);
		sel.Truncate(pdoc->Length());
		SetRectangularRange();
		Redraw();
	}
}

// Add next or each occurrence of the main selection within the target.
void Editor::MultipleSelectAdd(AddNumber addNumber) {
	if (SelectionEmpty() || !multipleSelection) {
		// Select word at caret
		const Sci::Position startWord = pdoc->ExtendWordSelect(sel.MainCaret(), -1, true);
		const Sci::Position endWord = pdoc->ExtendWordSelect(startWord, 1, true);
		TrimAndSetSelection(endWord, startWord);

	} else {

		if (!pdoc->HasCaseFolder())
			pdoc->SetCaseFolder(std::make_unique<CaseFolderUnicode>());

		const Range rangeMainSelection(sel.RangeMain().Start().Position(), sel.RangeMain().End().Position());
		const std::string selectedText = RangeText(rangeMainSelection.start, rangeMainSelection.end);

		const Range rangeTarget(targetRange.start.Position(), targetRange.end.Position());
		std::vector<Range> searchRanges;
		// Search should be over the target range excluding the current selection so
		// may need to search 2 ranges, after the selection then before the selection.
		if (rangeTarget.Overlaps(rangeMainSelection)) {
			// Common case is that the selection is completely within the target but
			// may also have overlap at start or end.
			if (rangeMainSelection.end < rangeTarget.end)
				searchRanges.emplace_back(rangeMainSelection.end, rangeTarget.end);
			if (rangeTarget.start < rangeMainSelection.start)
				searchRanges.emplace_back(rangeTarget.start, rangeMainSelection.start);
		} else {
			// No overlap
			searchRanges.push_back(rangeTarget);
		}

		for (const Range range : searchRanges) {
			Sci::Position searchStart = range.start;
			const Sci::Position searchEnd = range.end;
			for (;;) {
				Sci::Position lengthFound = selectedText.length();
				const Sci::Position pos = pdoc->FindText(searchStart, searchEnd,
					selectedText.c_str(), searchFlags, &lengthFound);
				if (pos >= 0) {
					sel.AddSelection(SelectionRange(pos + lengthFound, pos));
					ContainerNeedsUpdate(Update::Selection);
					ScrollRange(sel.RangeMain());
					Redraw();
					if (addNumber == AddNumber::one)
						return;
					searchStart = pos + lengthFound;
				} else {
					break;
				}
			}
		}
	}
}

// Select the whole document as a single stream selection.
void Editor::SelectAll() {
	sel.Clear();
	SetSelection(0, pdoc->Length());
	Redraw();
}

// Dispatch a selection-N message number for temporary WndProc forwarding.
void Editor::SetSelectionNMessage(Message iMessage, uptr_t wParam, sptr_t lParam) {
	if (wParam >= sel.Count()) {
		return;
	}
	InvalidateRange(sel.Range(wParam).Start().Position(), sel.Range(wParam).End().Position());

	switch (iMessage) {
	case Message::SetSelectionNCaret:
		sel.Range(wParam).caret.SetPosition(lParam);
		break;

	case Message::SetSelectionNAnchor:
		sel.Range(wParam).anchor.SetPosition(lParam);
		break;

	case Message::SetSelectionNCaretVirtualSpace:
		sel.Range(wParam).caret.SetVirtualSpace(lParam);
		break;

	case Message::SetSelectionNAnchorVirtualSpace:
		sel.Range(wParam).anchor.SetVirtualSpace(lParam);
		break;

	case Message::SetSelectionNStart:
		sel.Range(wParam).StartSet(SelectionPosition(lParam));
		break;

	case Message::SetSelectionNEnd:
		sel.Range(wParam).EndSet(SelectionPosition(lParam));
		break;

	default:
		break;

	}

	InvalidateRange(sel.Range(wParam).Start().Position(), sel.Range(wParam).End().Position());
	ContainerNeedsUpdate(Update::Selection);
}

namespace {

constexpr Selection::SelTypes SelTypeFromMode(SelectionMode mode) {
	switch (mode) {
	case SelectionMode::Rectangle:
		return Selection::SelTypes::rectangle;
	case SelectionMode::Lines:
		return Selection::SelTypes::lines;
	case SelectionMode::Thin:
		return Selection::SelTypes::thin;
	case SelectionMode::Stream:
	default:
		return Selection::SelTypes::stream;
	}
}

}

// Set stream/rectangle/lines/thin mode; may convert the current selection.
void Editor::SetSelectionMode(uptr_t wParam, bool setMoveExtends) {
	const Selection::SelTypes newSelType = SelTypeFromMode(static_cast<SelectionMode>(wParam));
	if (setMoveExtends) {
		sel.SetMoveExtends(!sel.MoveExtends() || (sel.selType != newSelType));
	}
	sel.selType = newSelType;
	switch (sel.selType) {
	case Selection::SelTypes::rectangle:
		sel.Rectangular() = sel.RangeMain(); // adjust current selection
		break;
	case Selection::SelTypes::lines:
		SetSelection(sel.RangeMain().caret, sel.RangeMain().anchor); // adjust current selection
		break;
	default:
		break;
	}
	InvalidateWholeSelection();
}

// Selection index whose range contains the document point, or -1.
ptrdiff_t Editor::SelectionFromPoint(Point pt) {
	// Prioritize checking inside non-empty selections since each character will be inside only 1
	const SelectionPosition posChar = SPositionFromLocation(pt, true, true);
	for (size_t r = 0; r < sel.Count(); r++) {
		if (sel.Range(r).ContainsCharacter(posChar)) {
			return r;
		}
	}

	// Then check if near empty selections as may be near more than 1
	const SelectionPosition pos = SPositionFromLocation(pt, true, false);
	for (size_t r = 0; r < sel.Count(); r++) {
		const SelectionRange &range = sel.Range(r);
		if ((range.Empty()) && (pos == range.caret)) {
			return r;
		}
	}

	// No selection at point
	return -1;
}

// Remove one selection range by index; keeps at least one range.
void Editor::DropSelection(size_t part) {
	sel.DropSelection(part);
	ContainerNeedsUpdate(Update::Selection);
	Redraw();
}

// Virtual-space policy for caret movement and rectangular selection (none,
// rectangular only, user-accessible, and/or no wrap at line start).
void Editor::SetVirtualSpaceOptions(VirtualSpace options) {
	virtualSpaceOptions = options;
}

// Current virtual-space policy.
VirtualSpace Editor::GetVirtualSpaceOptions() const noexcept {
	return virtualSpaceOptions;
}

