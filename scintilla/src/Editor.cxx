// Scintilla source code edit control
/** @file Editor.cxx
 ** Main code for the edit control.
 **/
// Copyright 1998-2011 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "CaseConvert.h"
#include "CharacterType.h"
#include "ContractionState.h"
#include "Debugging.h"
#include "Decoration.h"
#include "Document.h"
#include "EditModel.h"
#include "EditView.h"
#include "Editor.h"
#include "EditorBasicTypes.h"
#include "EditorCommands.h"
#include "EditorDocumentTypes.h"
#include "EditorInputTypes.h"
#include "EditorLayoutTypes.h"
#include "EditorStyleTypes.h"
#include "Geometry.h"
#include "Indicator.h"
#include "KeyMap.h"
#include "MarginView.h"
#include "Platform.h"
#include "Position.h"
#include "PositionCache.h"
#include "Selection.h"
#include "Style.h"
#include "UniConversion.h"
#include "ViewStyle.h"

using namespace Scintilla;
using namespace Scintilla::Internal;

namespace {

/*
	return whether this modification represents an operation that
	may reasonably be deferred (not done now OR [possibly] at all)
*/
constexpr bool CanDeferToLastStep(const DocModification &mh) noexcept {
	if (FlagSet(mh.modificationType, (ModificationFlags::BeforeInsert | ModificationFlags::BeforeDelete)))
		return true;	// CAN skip
	if (!FlagSet(mh.modificationType, (ModificationFlags::Undo | ModificationFlags::Redo)))
		return false;	// MUST do
	if (FlagSet(mh.modificationType, ModificationFlags::MultiStepUndoRedo))
		return true;	// CAN skip
	return false;		// PRESUMABLY must do
}

constexpr bool CanEliminate(const DocModification &mh) noexcept {
	return
		FlagSet(mh.modificationType, (ModificationFlags::BeforeInsert | ModificationFlags::BeforeDelete));
}

/*
	return whether this modification represents the FINAL step
	in a [possibly lengthy] multi-step Undo/Redo sequence
*/
constexpr bool IsLastStep(const DocModification &mh) noexcept {
	constexpr ModificationFlags finalMask = ModificationFlags::MultiStepUndoRedo
		| ModificationFlags::LastStepInUndoRedo
		| ModificationFlags::MultilineUndoRedo;
	return
		FlagSet(mh.modificationType, (ModificationFlags::Undo | ModificationFlags::Redo))
	    && ((mh.modificationType & finalMask) == finalMask);
}

constexpr bool IsAllSpacesOrTabs(std::string_view sv) noexcept {
	for (const char ch : sv) {
		// This is safe because IsSpaceOrTab() will return false for null terminators
		if (!IsSpaceOrTab(ch))
			return false;
	}
	return true;
}

}

Timer::Timer() noexcept :
		ticking(false), ticksToWait(0), tickerID{} {}

Idler::Idler() noexcept :
		state(false), idlerID(nullptr) {}

Editor::Editor() : durationWrapOneByte(0.000001, 0.00000001, 0.00001) {
	stylesValid = false;
	scaleRGBAImage = 100.0f;

	cursorMode = CursorShape::Normal;

	errorStatus = Status::Ok;
	mouseDownCaptures = true;
	mouseWheelCaptures = true;

	lastClickTime = 0;
	doubleClickCloseThreshold = Point(3, 3);
	dwellDelay = TimeForever;
	ticksToDwell = TimeForever;
	dwelling = false;
	ptMouseLast.x = 0;
	ptMouseLast.y = 0;
	dragDropEnabled = true;
	inDragDrop = DragDrop::none;
	dropWentOutside = false;
	posDrop = SelectionPosition(Sci::invalidPosition);
	hotSpotClickPos = Sci::invalidPosition;
	selectionUnit = TextUnit::character;

	lastXChosen = 0;
	lineAnchorPos = 0;
	originalAnchorPos = 0;
	wordSelectAnchorStartPos = 0;
	wordSelectAnchorEndPos = 0;
	wordSelectInitialCaretPos = -1;

	caretPolicies.x = { CaretPolicy::Slop | CaretPolicy::Even, 50 };
	caretPolicies.y = { CaretPolicy::Even, 0 };

	visiblePolicy = VisiblePolicySlop{};

	searchAnchor = 0;

	xCaretMargin = 50;
	horizontalScrollBarVisible = true;
	scrollWidth = 2000;
	verticalScrollBarVisible = true;
	endAtLastLine = true;
	caretSticky = CaretSticky::Off;
	marginOptions = MarginOption::None;
	mouseSelectionRectangularSwitch = false;
	multipleSelection = false;
	additionalSelectionTyping = false;
	multiPasteMode = MultiPaste::Once;
	rectangularSelectionModifier = KeyMod::Alt;
	virtualSpaceOptions = VirtualSpace::None;

	targetRange = SelectionSegment();
	searchFlags = FindOption::None;

	topLine = 0;
	posTopLine = 0;

	needUpdateUI = Update::None;
	ContainerNeedsUpdate(Update::Content);

	paintState = PaintState::notPainting;
	paintAbandonedByStyling = false;
	paintingAllText = false;
	willRedrawAll = false;
	idleStyling = IdleStyling::None;
	needIdleStyling = false;

	modEventMask = ModificationFlags::EventMaskAll;
	commandEvents = true;

	pdoc->AddWatcher(this, nullptr);

	recording = false;
	replaying = false;
	foldAutomatic = AutomaticFold::None;

	insideWrapScroll = false;

	convertPastes = true;

	SetRepresentations();
}

Editor::~Editor() {
	pdoc->RemoveWatcher(this, nullptr);
}

void Editor::Finalise() {
	SetIdle(false);
	CancelModes();
}

void Editor::DropGraphics() noexcept {
	marginView.DropGraphics();
	view.DropGraphics();
}

void Editor::InvalidateStyleData() noexcept {
	stylesValid = false;
	DropGraphics();
	view.llc.Invalidate(LineLayout::ValidLevel::invalid);
	view.posCache->Clear();
}

void Editor::InvalidateStyleRedraw() {
	NeedWrapping();
	InvalidateStyleData();
	Redraw();
}

void Editor::RefreshStyleData() {
	if (!stylesValid) {
		stylesValid = true;
		AutoSurface surface(this);
		if (surface) {
			vs.Refresh(*surface, pdoc->tabInChars);
		}
		SetScrollBars();
		SetRectangularRange();
	}
}

bool Editor::HasMarginWindow() const noexcept {
	return wMargin.Created();
}

Point Editor::GetVisibleOriginInMain() const {
	return Point(0, 0);
}

PointDocument Editor::DocumentPointFromView(Point ptView) const {
	PointDocument ptDocument(ptView);
	if (HasMarginWindow()) {
		const Point ptOrigin = GetVisibleOriginInMain();
		ptDocument.x += ptOrigin.x;
		ptDocument.y += ptOrigin.y;
	} else {
		ptDocument.x += xOffset;
		ptDocument.y += static_cast<double>(topLine * vs.lineHeight);
	}
	return ptDocument;
}

Sci::Line Editor::TopLineOfMain() const noexcept {
	if (HasMarginWindow())
		return 0;
	return topLine;
}

Point Editor::ClientSize() const {
	const PRectangle rcClient = GetClientRectangle();
	return Point(rcClient.Width(), rcClient.Height());
}

PRectangle Editor::GetClientRectangle() const {
	return wMain.GetClientPosition();
}

PRectangle Editor::GetClientDrawingRectangle() {
	return GetClientRectangle();
}

PRectangle Editor::GetTextRectangle() const {
	PRectangle rc = GetClientRectangle();
	rc.left += vs.textStart;
	rc.right -= vs.rightMarginWidth;
	return rc;
}

Sci::Line Editor::LinesOnScreen() const {
	const Point sizeClient = ClientSize();
	const int htClient = static_cast<int>(sizeClient.y);
	//Platform::DebugPrintf("lines on screen = %d\n", htClient / lineHeight + 1);
	return htClient / vs.lineHeight;
}

Sci::Line Editor::LinesToScroll() const {
	const Sci::Line retVal = LinesOnScreen() - 1;
	if (retVal < 1)
		return 1;
	return retVal;
}

Sci::Line Editor::MaxScrollPos() const {
	//Platform::DebugPrintf("Lines %d screen = %d maxScroll = %d\n",
	//LinesTotal(), LinesOnScreen(), LinesTotal() - LinesOnScreen() + 1);
	Sci::Line retVal = pcs->LinesDisplayed();
	if (endAtLastLine) {
		retVal -= LinesOnScreen();
	} else {
		retVal--;
	}
	if (retVal < 0) {
		return 0;
	}
	return retVal;
}

SelectionPosition Editor::ClampPositionIntoDocument(SelectionPosition sp) const {
	if (sp.Position() < 0) {
		return SelectionPosition(0);
	} else if (sp.Position() > pdoc->Length()) {
		return SelectionPosition(pdoc->Length());
	} else {
		// If not at end of line then set offset to 0
		if (!pdoc->IsLineEndPosition(sp.Position()))
			sp.SetVirtualSpace(0);
		return sp;
	}
}

Point Editor::LocationFromPosition(SelectionPosition pos, PointEnd pe) {
	const PRectangle rcClient = GetTextRectangle();
	RefreshStyleData();
	AutoSurface surface(this);
	return view.LocationFromPosition(surface, *this, pos, topLine, vs, pe, rcClient);
}

Point Editor::LocationFromPosition(Sci::Position pos, PointEnd pe) {
	return LocationFromPosition(SelectionPosition(pos), pe);
}

int Editor::XFromPosition(SelectionPosition sp) {
	const Point pt = LocationFromPosition(sp);
	return static_cast<int>(pt.x) - vs.textStart + xOffset;
}

SelectionPosition Editor::SPositionFromLocation(Point pt, bool canReturnInvalid, bool charPosition, bool virtualSpace) {
	RefreshStyleData();
	AutoSurface surface(this);

	PRectangle rcClient = GetTextRectangle();
	// May be in scroll view coordinates so translate back to main view
	const Point ptOrigin = GetVisibleOriginInMain();
	rcClient.Move(-ptOrigin.x, -ptOrigin.y);

	if (canReturnInvalid) {
		if (!rcClient.Contains(pt))
			return SelectionPosition(Sci::invalidPosition);
		if (pt.x < vs.textStart)
			return SelectionPosition(Sci::invalidPosition);
		if (pt.y < 0)
			return SelectionPosition(Sci::invalidPosition);
	}
	const PointDocument ptdoc = DocumentPointFromView(pt);
	return view.SPositionFromLocation(surface, *this, ptdoc, canReturnInvalid,
		charPosition, virtualSpace, vs, rcClient);
}

Sci::Position Editor::PositionFromLocation(Point pt, bool canReturnInvalid, bool charPosition) {
	return SPositionFromLocation(pt, canReturnInvalid, charPosition, false).Position();
}

/**
* Find the document position corresponding to an x coordinate on a particular document line.
* Ensure the result is on a UTF-8 character boundary.
* This method is used for rectangular selections and does not work on wrapped lines.
*/
SelectionPosition Editor::SPositionFromLineX(Sci::Line lineDoc, int x) {
	RefreshStyleData();
	if (lineDoc >= pdoc->LinesTotal())
		return SelectionPosition(pdoc->Length());
	//Platform::DebugPrintf("Position of (%d,%d) line = %d top=%d\n", pt.x, pt.y, line, topLine);
	AutoSurface surface(this);
	return view.SPositionFromLineX(surface, *this, lineDoc, x, vs);
}

Sci::Position Editor::PositionFromLineX(Sci::Line lineDoc, int x) {
	return SPositionFromLineX(lineDoc, x).Position();
}

Sci::Line Editor::LineFromLocation(Point pt) const noexcept {
	return pcs->DocFromDisplay(static_cast<int>(pt.y) / vs.lineHeight + topLine);
}

void Editor::SetTopLine(Sci::Line topLineNew) {
	if ((topLine != topLineNew) && (topLineNew >= 0)) {
		topLine = topLineNew;
		ContainerNeedsUpdate(Update::VScroll);
	}
	posTopLine = pdoc->LineStart(pcs->DocFromDisplay(topLine));
}

/**
 * If painting then abandon the painting because a wider redraw is needed.
 * @return true if calling code should stop drawing.
 */
bool Editor::AbandonPaint() {
	if ((paintState == PaintState::painting) && !paintingAllText) {
		paintState = PaintState::abandoned;
	}
	return paintState == PaintState::abandoned;
}

void Editor::RedrawRect(PRectangle rc) {
	//Platform::DebugPrintf("Redraw %0d,%0d - %0d,%0d\n", rc.left, rc.top, rc.right, rc.bottom);

	// Clip the redraw rectangle into the client area
	const PRectangle rcClient = GetClientRectangle();
	if (rc.top < rcClient.top)
		rc.top = rcClient.top;
	if (rc.bottom > rcClient.bottom)
		rc.bottom = rcClient.bottom;
	if (rc.left < rcClient.left)
		rc.left = rcClient.left;
	if (rc.right > rcClient.right)
		rc.right = rcClient.right;

	if ((rc.bottom > rc.top) && (rc.right > rc.left)) {
		wMain.InvalidateRectangle(rc);
	}
}

void Editor::DiscardOverdraw() {
	// Overridden on platforms that may draw outside visible area.
}

void Editor::Redraw() {
	if (redrawPendingText) {
		return;
	}
	//Platform::DebugPrintf("Redraw all\n");
	const PRectangle rcClient = GetClientRectangle();
	wMain.InvalidateRectangle(rcClient);
	if (HasMarginWindow()) {
		wMargin.InvalidateAll();
	} else if (paintState == PaintState::notPainting) {
		redrawPendingText = true;
	}
}

void Editor::RedrawSelMargin(Sci::Line line, bool allAfter) {
	const bool markersInText = vs.maskInLine || vs.maskDrawInText;
	if (!HasMarginWindow() || markersInText) {	// May affect text area so may need to abandon and retry
		if (AbandonPaint()) {
			return;
		}
	}
	if (HasMarginWindow() && markersInText) {
		Redraw();
		return;
	}
	if (redrawPendingMargin) {
		return;
	}
	PRectangle rcMarkers = GetClientRectangle();
	if (!markersInText) {
		// Normal case: just draw the margin
		rcMarkers.right = rcMarkers.left + vs.fixedColumnWidth;
	}
	const PRectangle rcMarkersFull = rcMarkers;
	if (line != -1) {
		PRectangle rcLine = RectangleFromRange(Range(pdoc->LineStart(line)), 0);

		// Inflate line rectangle if there are image markers with height larger than line height
		if (vs.largestMarkerHeight > vs.lineHeight) {
			const int delta = (vs.largestMarkerHeight - vs.lineHeight + 1) / 2;
			rcLine.top -= delta;
			rcLine.bottom += delta;
			if (rcLine.top < rcMarkers.top)
				rcLine.top = rcMarkers.top;
			if (rcLine.bottom > rcMarkers.bottom)
				rcLine.bottom = rcMarkers.bottom;
		}

		rcMarkers.top = rcLine.top;
		if (!allAfter)
			rcMarkers.bottom = rcLine.bottom;
		if (rcMarkers.Empty())
			return;
	}
	if (HasMarginWindow()) {
		const Point ptOrigin = GetVisibleOriginInMain();
		rcMarkers.Move(-ptOrigin.x, -ptOrigin.y);
		wMargin.InvalidateRectangle(rcMarkers);
	} else {
		wMain.InvalidateRectangle(rcMarkers);
		if (rcMarkers == rcMarkersFull) {
			redrawPendingMargin = true;
		}
	}
}

PRectangle Editor::RectangleFromRange(Range r, int overlap) {
	const Sci::Line docLineFirst = pdoc->SciLineFromPosition(r.First());
	const Sci::Line minLine = pcs->DisplayFromDoc(docLineFirst);
	Sci::Line docLineLast = docLineFirst;	// Common case where range is wholly in one document line
	if (r.Last() >= pdoc->LineStart(docLineFirst + 1)) {
		// Range covers multiple lines so need last line
		docLineLast = pdoc->SciLineFromPosition(r.Last());
	}
	const Sci::Line maxLine = pcs->DisplayLastFromDoc(docLineLast);
	const PRectangle rcClientDrawing = GetClientDrawingRectangle();
	PRectangle rc;
	const int leftTextOverlap = ((xOffset == 0) && (vs.leftMarginWidth > 0)) ? 1 : 0;
	rc.left = static_cast<XYPOSITION>(vs.textStart - leftTextOverlap);
	rc.top = static_cast<XYPOSITION>((minLine - TopLineOfMain()) * vs.lineHeight - overlap);
	if (rc.top < rcClientDrawing.top)
		rc.top = rcClientDrawing.top;
	// Extend to right of prepared area if any to prevent artifacts from caret line highlight
	rc.right = rcClientDrawing.right;
	rc.bottom = static_cast<XYPOSITION>((maxLine - TopLineOfMain() + 1) * vs.lineHeight + overlap);

	return rc;
}

void Editor::InvalidateRange(Sci::Position start, Sci::Position end) {
	if (redrawPendingText) {
		return;
	}
	RedrawRect(RectangleFromRange(Range(start, end), view.LinesOverlap() ? vs.lineOverlap : 0));
}

Sci::Position Editor::CurrentPosition() const noexcept {
	return sel.MainCaret();
}

bool Editor::SelectionEmpty() const noexcept {
	return sel.Empty();
}

SelectionPosition Editor::SelectionStart() noexcept {
	return sel.RangeMain().Start();
}

SelectionPosition Editor::SelectionEnd() noexcept {
	return sel.RangeMain().End();
}

void Editor::SetRectangularRange() {
	if (sel.IsRectangular()) {
		const int xAnchor = XFromPosition(sel.Rectangular().anchor);
		int xCaret = XFromPosition(sel.Rectangular().caret);
		if (sel.selType == Selection::SelTypes::thin) {
			xCaret = xAnchor;
		}
		const Sci::Line lineAnchorRect =
			pdoc->SciLineFromPosition(sel.Rectangular().anchor.Position());
		const Sci::Line lineCaret =
			pdoc->SciLineFromPosition(sel.Rectangular().caret.Position());
		const int increment = (lineCaret > lineAnchorRect) ? 1 : -1;
		AutoSurface surface(this);
		for (Sci::Line line=lineAnchorRect; line != lineCaret+increment; line += increment) {
			SelectionRange range(
				view.SPositionFromLineX(surface, *this, line, xCaret, vs),
				view.SPositionFromLineX(surface, *this, line, xAnchor, vs));
			if (!FlagSet(virtualSpaceOptions, VirtualSpace::RectangularSelection))
				range.ClearVirtualSpace();
			if (line == lineAnchorRect)
				sel.SetSelection(range);
			else
				sel.AddSelectionWithoutTrim(range);
		}
	}
}

void Editor::ThinRectangularRange() {
	if (sel.IsRectangular()) {
		sel.selType = Selection::SelTypes::thin;
		if (sel.Rectangular().caret < sel.Rectangular().anchor) {
			sel.Rectangular() = SelectionRange(sel.Range(sel.Count()-1).caret, sel.Range(0).anchor);
		} else {
			sel.Rectangular() = SelectionRange(sel.Range(sel.Count()-1).anchor, sel.Range(0).caret);
		}
		SetRectangularRange();
	}
}

void Editor::InvalidateSelection(SelectionRange newMain, bool invalidateWholeSelection) {
	if (sel.Count() > 1 || !(sel.RangeMain().anchor == newMain.anchor) || sel.IsRectangular()) {
		invalidateWholeSelection = true;
	}
	Sci::Position firstAffected = std::min(sel.RangeMain().Start().Position(), newMain.Start().Position());
	// +1 for lastAffected ensures caret repainted
	Sci::Position lastAffected = std::max(newMain.caret.Position()+1, newMain.anchor.Position());
	lastAffected = std::max(lastAffected, sel.RangeMain().End().Position());
	if (invalidateWholeSelection) {
		for (size_t r=0; r<sel.Count(); r++) {
			firstAffected = std::min(firstAffected, sel.Range(r).caret.Position());
			firstAffected = std::min(firstAffected, sel.Range(r).anchor.Position());
			lastAffected = std::max(lastAffected, sel.Range(r).caret.Position()+1);
			lastAffected = std::max(lastAffected, sel.Range(r).anchor.Position());
		}
	}
	ContainerNeedsUpdate(Update::Selection);
	InvalidateRange(firstAffected, lastAffected);
}

void Editor::InvalidateWholeSelection() {
	InvalidateSelection(sel.RangeMain(), true);
}

/* For Line selection - the anchor and caret are always
   at the beginning and end of the region lines. */
SelectionRange Editor::LineSelectionRange(SelectionPosition currentPos_, SelectionPosition anchor_) const noexcept {
	if (currentPos_ > anchor_) {
		anchor_ = SelectionPosition(pdoc->LineStartPosition(anchor_.Position()));
		currentPos_ = SelectionPosition(pdoc->LineEndPosition(currentPos_.Position()));
	} else {
		currentPos_ = SelectionPosition(pdoc->LineStartPosition(currentPos_.Position()));
		anchor_ = SelectionPosition(pdoc->LineEndPosition(anchor_.Position()));
	}
	return SelectionRange(currentPos_, anchor_);
}

bool Editor::RangeContainsProtected(Sci::Position start, Sci::Position end) const noexcept {
	if (vs.ProtectionActive()) {
		if (start > end) {
			std::swap(start, end);
		}
		for (Sci::Position pos = start; pos < end; pos++) {
			if (vs.styles[pdoc->StyleIndexAt(pos)].IsProtected())
				return true;
		}
	}
	return false;
}

bool Editor::RangeContainsProtected(const SelectionRange &range) const noexcept {
	return RangeContainsProtected(range.Start().Position(), range.End().Position());
}

bool Editor::SelectionContainsProtected() const noexcept {
	for (size_t r=0; r<sel.Count(); r++) {
		if (RangeContainsProtected(sel.Range(r))) {
			return true;
		}
	}
	return false;
}

/**
 * Asks document to find a good position and then moves out of any invisible positions.
 */
Sci::Position Editor::MovePositionOutsideChar(Sci::Position pos, Sci::Position moveDir, bool checkLineEnd) const {
	return MovePositionOutsideChar(SelectionPosition(pos), moveDir, checkLineEnd).Position();
}

SelectionPosition Editor::MovePositionOutsideChar(SelectionPosition pos, Sci::Position moveDir, bool checkLineEnd) const {
	const Sci::Position posMoved = pdoc->MovePositionOutsideChar(pos.Position(), moveDir, checkLineEnd);
	if (posMoved != pos.Position())
		pos.SetPosition(posMoved);
	if (vs.ProtectionActive()) {
		if (moveDir > 0) {
			if ((pos.Position() > 0) && vs.styles[pdoc->StyleIndexAt(pos.Position() - 1)].IsProtected()) {
				while ((pos.Position() < pdoc->Length()) &&
				        (vs.styles[pdoc->StyleIndexAt(pos.Position())].IsProtected()))
					pos.Add(1);
			}
		} else if (moveDir < 0) {
			if (vs.styles[pdoc->StyleIndexAt(pos.Position())].IsProtected()) {
				while ((pos.Position() > 0) &&
				        (vs.styles[pdoc->StyleIndexAt(pos.Position() - 1)].IsProtected()))
					pos.Add(-1);
			}
		}
	}
	return pos;
}

void Editor::MovedCaret(SelectionPosition newPos, SelectionPosition previousPos,
	bool ensureVisible, CaretPolicies policies) {
	const Sci::Line currentLine = pdoc->SciLineFromPosition(newPos.Position());
	if (ensureVisible) {
		// In case in need of wrapping to ensure DisplayFromDoc works.
		if (currentLine >= wrapPending.start) {
			if (WrapLines(WrapScope::wsAll)) {
				Redraw();
			}
		}
		const XYScrollPosition newXY = XYScrollToMakeVisible(
			SelectionRange(posDrag.IsValid() ? posDrag : newPos), XYScrollOptions::all, policies);
		if (previousPos.IsValid() && (newXY.xOffset == xOffset)) {
			// simple vertical scroll then invalidate
			ScrollTo(newXY.topLine);
			InvalidateSelection(SelectionRange(previousPos), true);
		} else {
			SetXYScroll(newXY);
		}
	}

	ShowCaretAtCurrentPosition();
	NotifyCaretMove();

	ClaimSelection();
	SetHoverIndicatorPosition(sel.MainCaret());
	QueueIdleWork(WorkItems::updateUI);

	if (marginView.highlightDelimiter.NeedsDrawing(currentLine)) {
		RedrawSelMargin();
	}
}

void Editor::MovePositionTo(SelectionPosition newPos, Selection::SelTypes selt, bool ensureVisible) {
	const SelectionPosition spCaret = ((sel.Count() == 1) && sel.Empty()) ?
		sel.Last() : SelectionPosition(Sci::invalidPosition);

	const Sci::Position delta = newPos.Position() - sel.MainCaret();
	newPos = ClampPositionIntoDocument(newPos);
	newPos = MovePositionOutsideChar(newPos, delta);
	if (!multipleSelection && sel.IsRectangular() && (selt == Selection::SelTypes::stream)) {
		// Can't turn into multiple selection so clear additional selections
		InvalidateSelection(SelectionRange(newPos), true);
		sel.DropAdditionalRanges();
	}
	if (!sel.IsRectangular() && (selt == Selection::SelTypes::rectangle)) {
		// Switching to rectangular
		InvalidateSelection(sel.RangeMain(), false);
		SelectionRange rangeMain = sel.RangeMain();
		sel.Clear();
		sel.Rectangular() = rangeMain;
	}
	if (selt != Selection::SelTypes::none) {
		sel.selType = selt;
	}
	if (selt != Selection::SelTypes::none || sel.MoveExtends()) {
		SetSelection(newPos);
	} else {
		SetEmptySelection(newPos);
	}

	MovedCaret(newPos, spCaret, ensureVisible, caretPolicies);
}

void Editor::MovePositionTo(Sci::Position newPos, Selection::SelTypes selt, bool ensureVisible) {
	MovePositionTo(SelectionPosition(newPos), selt, ensureVisible);
}

SelectionPosition Editor::MovePositionSoVisible(SelectionPosition pos, int moveDir) {
	pos = ClampPositionIntoDocument(pos);
	pos = MovePositionOutsideChar(pos, moveDir);
	const Sci::Line lineDoc = pdoc->SciLineFromPosition(pos.Position());
	if (pcs->GetVisible(lineDoc)) {
		return pos;
	}
	Sci::Line lineDisplay = pcs->DisplayFromDoc(lineDoc);
	if (moveDir > 0) {
		// lineDisplay is already line before fold as lines in fold use display line of line after fold
		lineDisplay = std::clamp<Sci::Line>(lineDisplay, 0, pcs->LinesDisplayed());
		return SelectionPosition(
			pdoc->LineStart(pcs->DocFromDisplay(lineDisplay)));
	} else {
		lineDisplay = std::clamp<Sci::Line>(lineDisplay - 1, 0, pcs->LinesDisplayed());
		return SelectionPosition(
			pdoc->LineEnd(pcs->DocFromDisplay(lineDisplay)));
	}
}

SelectionPosition Editor::MovePositionSoVisible(Sci::Position pos, int moveDir) {
	return MovePositionSoVisible(SelectionPosition(pos), moveDir);
}

Point Editor::PointMainCaret() {
	return LocationFromPosition(sel.RangeMain().caret);
}

/**
 * Choose the x position that the caret will try to stick to
 * as it moves up and down.
 */
void Editor::SetLastXChosen() {
	const Point pt = PointMainCaret();
	lastXChosen = static_cast<int>(pt.x) + xOffset;
}

void Editor::RememberSelectionForUndo(int index) {
	EnsureModelState();
	if (modelState) {
		modelState->RememberSelectionForUndo(index, sel);
		needRedoRemembered = true;
		// Remember selection at end of processing current message
	}
}

void Editor::RememberSelectionOntoStack(int index) {
	EnsureModelState();
	if (modelState) {
		// Is undo currently inside a group?
		if (!pdoc->AfterUndoSequenceStart()) {
			// Don't remember selections inside a grouped sequence as can only
			// unto or redo to the start and end of the group.
			modelState->RememberSelectionOntoStack(index, topLine);
		}
	}
}

void Editor::RememberCurrentSelectionForRedoOntoStack() {
	if (needRedoRemembered && (pdoc->UndoSequenceDepth() == 0)) {
		EnsureModelState();
		if (modelState) {
			modelState->RememberSelectionForRedoOntoStack(pdoc->UndoCurrent(), sel, topLine);
			needRedoRemembered = false;
		}
	}
}

void Editor::ScrollTo(Sci::Line line, bool moveThumb) {
	const Sci::Line topLineNew = std::clamp<Sci::Line>(line, 0, MaxScrollPos());
	if (topLineNew != topLine) {
		// Try to optimise small scrolls
#ifndef UNDER_CE
		const Sci::Line linesToMove = topLine - topLineNew;
		const bool performBlit = (std::abs(linesToMove) <= 10) && (paintState == PaintState::notPainting);
		willRedrawAll = !performBlit;
#endif
		SetTopLine(topLineNew);
		// Optimize by styling the view as this will invalidate any needed area
		// which could abort the initial paint if discovered later.
		StyleAreaBounded(GetClientRectangle(), true);
#ifndef UNDER_CE
		// Perform redraw rather than scroll if many lines would be redrawn anyway.
		if (performBlit) {
			ScrollText(linesToMove);
		} else {
			Redraw();
		}
		willRedrawAll = false;
#else
		Redraw();
#endif
		if (moveThumb) {
			SetVerticalScrollPos();
		}
	}
}

void Editor::ScrollText(Sci::Line /* linesToMove */) {
	//Platform::DebugPrintf("Editor::ScrollText %d\n", linesToMove);
	Redraw();
}

void Editor::HorizontalScrollTo(int xPos) {
	//Platform::DebugPrintf("HorizontalScroll %d\n", xPos);
	if (xPos < 0)
		xPos = 0;
	if (!Wrapping() && (xOffset != xPos)) {
		xOffset = xPos;
		ContainerNeedsUpdate(Update::HScroll);
		SetHorizontalScrollPos();
		RedrawRect(GetClientRectangle());
	}
}


void Editor::MoveSelectedLines(int lineDelta) {

	if (sel.IsRectangular()) {
		// Convert to stream selection
		const SelectionRange rangeRectangular = sel.Rectangular();
		sel.Clear();
		sel.SetSelection(rangeRectangular);
	}

	// if selection doesn't start at the beginning of the line, set the new start
	Sci::Position selectionStart = SelectionStart().Position();
	const Sci::Line startLine = pdoc->SciLineFromPosition(selectionStart);
	const Sci::Position beginningOfStartLine = pdoc->LineStart(startLine);
	selectionStart = beginningOfStartLine;

	// if selection doesn't end at the beginning of a line greater than that of the start,
	// then set it at the beginning of the next one
	Sci::Position selectionEnd = SelectionEnd().Position();
	Sci::Line endLine = pdoc->SciLineFromPosition(selectionEnd);
	const Sci::Position beginningOfEndLine = pdoc->LineStart(endLine);
	bool appendEol = false;
	if (selectionEnd > beginningOfEndLine
		|| selectionStart == selectionEnd) {
		selectionEnd = pdoc->LineStart(endLine + 1);
		appendEol = (selectionEnd == pdoc->Length() && pdoc->SciLineFromPosition(selectionEnd) == endLine);
		endLine = pdoc->SciLineFromPosition(selectionEnd);
	}

	// if there's nowhere for the selection to move
	// (i.e. at the beginning going up or at the end going down),
	// stop it right there!
	const bool docEndLineEmpty = pdoc->LineStart(endLine) == pdoc->Length();
	if ((selectionStart == 0 && lineDelta < 0)
		|| (selectionEnd == pdoc->Length() && lineDelta > 0
			&& !docEndLineEmpty) // allow moving when end line of document is empty
		|| ((selectionStart == selectionEnd)
			&& !(lineDelta < 0 && docEndLineEmpty && selectionEnd == pdoc->Length()))) { // allow moving-up last empty line
		return;
	}

	UndoGroup ug(pdoc);

	if (lineDelta > 0 && selectionEnd == pdoc->LineStart(pdoc->LinesTotal() - 1)) {
		SetSelection(pdoc->MovePositionOutsideChar(selectionEnd - 1, -1), selectionEnd);
		ClearSelection();
		selectionEnd = CurrentPosition();
	}
	SetSelection(selectionStart, selectionEnd);

	const std::string selectedText = RangeText(selectionStart, selectionEnd);

	const Point currentLocation = LocationFromPosition(CurrentPosition());
	const Sci::Line currentLine = LineFromLocation(currentLocation);

	if (appendEol)
		SetSelection(pdoc->MovePositionOutsideChar(selectionStart - 1, -1), selectionEnd);
	ClearSelection();

	const std::string_view eol = pdoc->EOLString();
	if (currentLine + lineDelta >= pdoc->LinesTotal())
		pdoc->InsertString(pdoc->Length(), eol);
	MoveCaretToLine(currentLine + lineDelta);

	Sci::Position selectionLength = pdoc->InsertString(CurrentPosition(), selectedText);
	if (appendEol) {
		const Sci::Position lengthInserted = pdoc->InsertString(CurrentPosition() + selectionLength, eol);
		selectionLength += lengthInserted;
	}
	SetSelection(CurrentPosition(), CurrentPosition() + selectionLength);
}

void Editor::MoveSelectedLinesUp() {
	MoveSelectedLines(-1);
}

void Editor::MoveSelectedLinesDown() {
	MoveSelectedLines(1);
}


Sci::Line Editor::DisplayFromPosition(Sci::Position pos) {
	AutoSurface surface(this);
	return view.DisplayFromPosition(surface, *this, pos, vs);
}

/**
 * Ensure the caret is reasonably visible in context.
 *
Caret policy in Scintilla

If slop is set, we can define a slop value.
This value defines an unwanted zone (UZ) where the caret is... unwanted.
This zone is defined as a number of pixels near the vertical margins,
and as a number of lines near the horizontal margins.
By keeping the caret away from the edges, it is seen within its context,
so it is likely that the identifier that the caret is on can be completely seen,
and that the current line is seen with some of the lines following it which are
often dependent on that line.

If strict is set, the policy is enforced... strictly.
The caret is centred on the display if slop is not set,
and cannot go in the UZ if slop is set.

If jumps is set, the display is moved more energetically
so the caret can move in the same direction longer before the policy is applied again.
'3UZ' notation is used to indicate three time the size of the UZ as a distance to the margin.

If even is not set, instead of having symmetrical UZs,
the left and bottom UZs are extended up to right and top UZs respectively.
This way, we favour the displaying of useful information: the beginning of lines,
where most code reside, and the lines after the caret, eg. the body of a function.

     |        |       |      |                                            |
slop | strict | jumps | even | Caret can go to the margin                 | When reaching limit (caret going out of
     |        |       |      |                                            | visibility or going into the UZ) display is...
-----+--------+-------+------+--------------------------------------------+--------------------------------------------------------------
  0  |   0    |   0   |   0  | Yes                                        | moved to put caret on top/on right
  0  |   0    |   0   |   1  | Yes                                        | moved by one position
  0  |   0    |   1   |   0  | Yes                                        | moved to put caret on top/on right
  0  |   0    |   1   |   1  | Yes                                        | centred on the caret
  0  |   1    |   -   |   0  | Caret is always on top/on right of display | -
  0  |   1    |   -   |   1  | No, caret is always centred                | -
  1  |   0    |   0   |   0  | Yes                                        | moved to put caret out of the asymmetrical UZ
  1  |   0    |   0   |   1  | Yes                                        | moved to put caret out of the UZ
  1  |   0    |   1   |   0  | Yes                                        | moved to put caret at 3UZ of the top or right margin
  1  |   0    |   1   |   1  | Yes                                        | moved to put caret at 3UZ of the margin
  1  |   1    |   -   |   0  | Caret is always at UZ of top/right margin  | -
  1  |   1    |   0   |   1  | No, kept out of UZ                         | moved by one position
  1  |   1    |   1   |   1  | No, kept out of UZ                         | moved to put caret at 3UZ of the margin
*/

Editor::XYScrollPosition Editor::XYScrollToMakeVisible(const SelectionRange &range,
	const XYScrollOptions options, CaretPolicies policies) {
	const PRectangle rcClient = GetTextRectangle();
	const Point ptOrigin = GetVisibleOriginInMain();
	const Point pt = LocationFromPosition(range.caret) + ptOrigin;
	const Point ptAnchor = LocationFromPosition(range.anchor) + ptOrigin;
	const Point ptBottomCaret(pt.x, pt.y + vs.lineHeight - 1);

	XYScrollPosition newXY(xOffset, topLine);
	if (rcClient.Empty()) {
		return newXY;
	}

	// Vertical positioning
	if (FlagSet(options, XYScrollOptions::vertical) &&
		(pt.y < rcClient.top || ptBottomCaret.y >= rcClient.bottom || FlagSet(policies.y.policy, CaretPolicy::Strict))) {
		const Sci::Line lineCaret = DisplayFromPosition(range.caret.Position());
		const Sci::Line linesOnScreen = LinesOnScreen();
		const Sci::Line halfScreen = std::max(linesOnScreen - 1, static_cast<Sci::Line>(2)) / 2;
		const bool bSlop = FlagSet(policies.y.policy, CaretPolicy::Slop);
		const bool bStrict = FlagSet(policies.y.policy, CaretPolicy::Strict);
		const bool bJump = FlagSet(policies.y.policy, CaretPolicy::Jumps);
		const bool bEven = FlagSet(policies.y.policy, CaretPolicy::Even);

		// It should be possible to scroll the window to show the caret,
		// but this fails to remove the caret on GTK+
		if (bSlop) {	// A margin is defined
			Sci::Line yMoveT = 0;
			Sci::Line yMoveB = 0;
			if (bStrict) {
				Sci::Line yMarginT = 0;
				Sci::Line yMarginB = 0;
				if (!FlagSet(options, XYScrollOptions::useMargin)) {
					// In drag mode, avoid moves
					// otherwise, a double click will select several lines.
					yMarginT = yMarginB = 0;
				} else {
					// yMarginT must equal to caretYSlop, with a minimum of 1 and
					// a maximum of slightly less than half the height of the text area.
					yMarginT = std::clamp<Sci::Line>(policies.y.slop, 1, halfScreen);
					if (bEven) {
						yMarginB = yMarginT;
					} else {
						yMarginB = linesOnScreen - yMarginT - 1;
					}
				}
				yMoveT = yMarginT;
				if (bEven) {
					if (bJump) {
						yMoveT = std::clamp<Sci::Line>(policies.y.slop * 3, 1, halfScreen);
					}
					yMoveB = yMoveT;
				} else {
					yMoveB = linesOnScreen - yMoveT - 1;
				}
				if (lineCaret < topLine + yMarginT) {
					// Caret goes too high
					newXY.topLine = lineCaret - yMoveT;
				} else if (lineCaret > topLine + linesOnScreen - 1 - yMarginB) {
					// Caret goes too low
					newXY.topLine = lineCaret - linesOnScreen + 1 + yMoveB;
				}
			} else {	// Not strict
				yMoveT = bJump ? policies.y.slop * 3 : policies.y.slop;
				yMoveT = std::clamp<Sci::Line>(yMoveT, 1, halfScreen);
				if (bEven) {
					yMoveB = yMoveT;
				} else {
					yMoveB = linesOnScreen - yMoveT - 1;
				}
				if (lineCaret < topLine) {
					// Caret goes too high
					newXY.topLine = lineCaret - yMoveT;
				} else if (lineCaret > topLine + linesOnScreen - 1) {
					// Caret goes too low
					newXY.topLine = lineCaret - linesOnScreen + 1 + yMoveB;
				}
			}
		} else {	// No slop
			if (!bStrict && !bJump) {
				// Minimal move
				if (lineCaret < topLine) {
					// Caret goes too high
					newXY.topLine = lineCaret;
				} else if (lineCaret > topLine + linesOnScreen - 1) {
					// Caret goes too low
					if (bEven) {
						newXY.topLine = lineCaret - linesOnScreen + 1;
					} else {
						newXY.topLine = lineCaret;
					}
				}
			} else {	// Strict or going out of display
				if (bEven) {
					// Always centre caret
					newXY.topLine = lineCaret - halfScreen;
				} else {
					// Always put caret on top of display
					newXY.topLine = lineCaret;
				}
			}
		}
		if (!(range.caret == range.anchor)) {
			const Sci::Line lineAnchor = DisplayFromPosition(range.anchor.Position());
			if (lineAnchor < lineCaret) {
				// Shift up to show anchor or as much of range as possible
				newXY.topLine = std::min(newXY.topLine, lineAnchor);
				newXY.topLine = std::max(newXY.topLine, lineCaret - LinesOnScreen());
			} else {
				// Shift down to show anchor or as much of range as possible
				newXY.topLine = std::max(newXY.topLine, lineAnchor - LinesOnScreen());
				newXY.topLine = std::min(newXY.topLine, lineCaret);
			}
		}
		newXY.topLine = std::clamp<Sci::Line>(newXY.topLine, 0, MaxScrollPos());
	}

	// Horizontal positioning
	if (FlagSet(options, XYScrollOptions::horizontal) && !Wrapping()) {
		const int halfScreen = std::max(static_cast<int>(rcClient.Width()) - 4, 4) / 2;
		const bool bSlop = FlagSet(policies.x.policy, CaretPolicy::Slop);
		const bool bStrict = FlagSet(policies.x.policy, CaretPolicy::Strict);
		const bool bJump = FlagSet(policies.x.policy, CaretPolicy::Jumps);
		const bool bEven = FlagSet(policies.x.policy, CaretPolicy::Even);

		if (bSlop) {	// A margin is defined
			int xMoveL = 0;
			int xMoveR = 0;
			if (bStrict) {
				int xMarginL = 0;
				int xMarginR = 0;
				if (!FlagSet(options, XYScrollOptions::useMargin)) {
					// In drag mode, avoid moves unless very near of the margin
					// otherwise, a simple click will select text.
					xMarginL = xMarginR = 2;
				} else {
					// xMargin must equal to caretXSlop, with a minimum of 2 and
					// a maximum of slightly less than half the width of the text area.
					xMarginR = std::clamp(policies.x.slop, 2, halfScreen);
					if (bEven) {
						xMarginL = xMarginR;
					} else {
						xMarginL = static_cast<int>(rcClient.Width()) - xMarginR - 4;
					}
				}
				if (bJump && bEven) {
					// Jump is used only in even mode
					xMoveL = xMoveR = std::clamp(policies.x.slop * 3, 1, halfScreen);
				} else {
					xMoveL = xMoveR = 0;	// Not used, avoid a warning
				}
				if (pt.x < rcClient.left + xMarginL) {
					// Caret is on the left of the display
					if (bJump && bEven) {
						newXY.xOffset -= xMoveL;
					} else {
						// Move just enough to allow to display the caret
						newXY.xOffset -= static_cast<int>((rcClient.left + xMarginL) - pt.x);
					}
				} else if (pt.x >= rcClient.right - xMarginR) {
					// Caret is on the right of the display
					if (bJump && bEven) {
						newXY.xOffset += xMoveR;
					} else {
						// Move just enough to allow to display the caret
						newXY.xOffset += static_cast<int>(pt.x - (rcClient.right - xMarginR) + 1);
					}
				}
			} else {	// Not strict
				xMoveR = bJump ? policies.x.slop * 3 : policies.x.slop;
				xMoveR = std::clamp(xMoveR, 1, halfScreen);
				if (bEven) {
					xMoveL = xMoveR;
				} else {
					xMoveL = static_cast<int>(rcClient.Width()) - xMoveR - 4;
				}
				if (pt.x < rcClient.left) {
					// Caret is on the left of the display
					newXY.xOffset -= xMoveL;
				} else if (pt.x >= rcClient.right) {
					// Caret is on the right of the display
					newXY.xOffset += xMoveR;
				}
			}
		} else {	// No slop
			if (bStrict ||
			        (bJump && (pt.x < rcClient.left || pt.x >= rcClient.right))) {
				// Strict or going out of display
				if (bEven) {
					// Centre caret
					newXY.xOffset += static_cast<int>(pt.x - rcClient.left - halfScreen);
				} else {
					// Put caret on right
					newXY.xOffset += static_cast<int>(pt.x - rcClient.right + 1);
				}
			} else {
				// Move just enough to allow to display the caret
				if (pt.x < rcClient.left) {
					// Caret is on the left of the display
					if (bEven) {
						newXY.xOffset -= static_cast<int>(rcClient.left - pt.x);
					} else {
						newXY.xOffset += static_cast<int>(pt.x - rcClient.right) + 1;
					}
				} else if (pt.x >= rcClient.right) {
					// Caret is on the right of the display
					newXY.xOffset += static_cast<int>(pt.x - rcClient.right) + 1;
				}
			}
		}
		// In case of a jump (find result) largely out of display, adjust the offset to display the caret
		if (pt.x + xOffset < rcClient.left + newXY.xOffset) {
			newXY.xOffset = static_cast<int>(pt.x + xOffset - rcClient.left) - 2;
		} else if (pt.x + xOffset >= rcClient.right + newXY.xOffset) {
			newXY.xOffset = static_cast<int>(pt.x + xOffset - rcClient.right) + 2;
			if (vs.IsBlockCaretStyle() || view.imeCaretBlockOverride) {
				// Ensure we can see a good portion of the block caret
				newXY.xOffset += static_cast<int>(vs.aveCharWidth);
			}
		}
		if (!(range.caret == range.anchor)) {
			if (ptAnchor.x < pt.x) {
				// Shift to left to show anchor or as much of range as possible
				const int maxOffset = static_cast<int>(ptAnchor.x + xOffset - rcClient.left) - 1;
				const int minOffset = static_cast<int>(pt.x + xOffset - rcClient.right) + 1;
				newXY.xOffset = std::min(newXY.xOffset, maxOffset);
				newXY.xOffset = std::max(newXY.xOffset, minOffset);
			} else {
				// Shift to right to show anchor or as much of range as possible
				const int minOffset = static_cast<int>(ptAnchor.x + xOffset - rcClient.right) + 1;
				const int maxOffset = static_cast<int>(pt.x + xOffset - rcClient.left) - 1;
				newXY.xOffset = std::max(newXY.xOffset, minOffset);
				newXY.xOffset = std::min(newXY.xOffset, maxOffset);
			}
		}
		if (newXY.xOffset < 0) {
			newXY.xOffset = 0;
		}
	}

	return newXY;
}

void Editor::SetXYScroll(XYScrollPosition newXY) {
	if ((newXY.topLine != topLine) || (newXY.xOffset != xOffset)) {
		if (newXY.topLine != topLine) {
			SetTopLine(newXY.topLine);
			SetVerticalScrollPos();
		}
		if (newXY.xOffset != xOffset) {
			xOffset = newXY.xOffset;
			ContainerNeedsUpdate(Update::HScroll);
			if (newXY.xOffset > 0) {
				const PRectangle rcText = GetTextRectangle();
				if (horizontalScrollBarVisible &&
					rcText.Width() + xOffset > scrollWidth) {
					scrollWidth = xOffset + static_cast<int>(rcText.Width());
					SetScrollBars();
				}
			}
			SetHorizontalScrollPos();
		}
		Redraw();
		UpdateSystemCaret();
	}
}

void Editor::ScrollRange(SelectionRange range) {
	SetXYScroll(XYScrollToMakeVisible(range, XYScrollOptions::all, caretPolicies));
}

void Editor::EnsureCaretVisible(bool useMargin, bool vert, bool horiz) {
	SetXYScroll(XYScrollToMakeVisible(SelectionRange(posDrag.IsValid() ? posDrag : sel.RangeMain().caret),
		(useMargin?XYScrollOptions::useMargin:XYScrollOptions::none)|
		(vert?XYScrollOptions::vertical:XYScrollOptions::none)|
		(horiz?XYScrollOptions::horizontal:XYScrollOptions::none),
		caretPolicies));
}





void Editor::NotifyCaretMove() {
}

void Editor::UpdateSystemCaret() {
}

void Editor::PaintSelMargin(Surface *surfaceWindow, const PRectangle &rc) {
	if (vs.fixedColumnWidth == 0)
		return;

	RefreshStyleData();
	RefreshPixMaps(surfaceWindow);

	// On GTK+ with Ubuntu overlay scroll bars, the surface may have been finished
	// at this point. The Initialised call checks for this case and sets the status
	// to be bad which avoids crashes in following calls.
	if (!surfaceWindow->Initialised()) {
		return;
	}

	PRectangle rcMargin = GetClientRectangle();
	const Point ptOrigin = GetVisibleOriginInMain();
	rcMargin.Move(0, -ptOrigin.y);
	rcMargin.left = 0;
	rcMargin.right = static_cast<XYPOSITION>(vs.fixedColumnWidth);

	if (!rc.Intersects(rcMargin))
		return;

	Surface *surface;
	if (view.bufferedDraw) {
		surface = marginView.pixmapSelMargin.get();
	} else {
		surface = surfaceWindow;
	}
	surface->SetMode(CurrentSurfaceMode());

	// Clip vertically to paint area to avoid drawing line numbers
	if (rcMargin.bottom > rc.bottom)
		rcMargin.bottom = rc.bottom;
	if (rcMargin.top < rc.top)
		rcMargin.top = rc.top;

	marginView.PaintMargin(surface, topLine, rc, rcMargin, *this, vs);

	if (view.bufferedDraw) {
		marginView.pixmapSelMargin->FlushDrawing();
		surfaceWindow->Copy(rcMargin, Point(rcMargin.left, rcMargin.top), *marginView.pixmapSelMargin);
	}
}

void Editor::RefreshPixMaps(Surface *surfaceWindow) {
	view.RefreshPixMaps(surfaceWindow, vs);
	marginView.RefreshPixMaps(surfaceWindow, vs);
	if (view.bufferedDraw && !(view.pixmapLine && marginView.pixmapSelMargin)) {
		const PRectangle rcClient = GetClientRectangle();
		view.pixmapLine = surfaceWindow->AllocatePixMap(static_cast<int>(rcClient.Width()), vs.lineHeight);
		marginView.pixmapSelMargin = surfaceWindow->AllocatePixMap(vs.fixedColumnWidth,
			static_cast<int>(rcClient.Height()));
	}
}

void Editor::Paint(Surface *surfaceWindow, PRectangle rcArea) {
	redrawPendingText = false;
	redrawPendingMargin = false;

	//Platform::DebugPrintf("Paint:%1d (%3d,%3d) ... (%3d,%3d)\n",
	//	paintingAllText, rcArea.left, rcArea.top, rcArea.right, rcArea.bottom);

	RefreshStyleData();
	if (paintState == PaintState::abandoned)
		return;	// Scroll bars may have changed so need redraw

	paintAbandonedByStyling = false;

	StyleAreaBounded(rcArea, false);

	const PRectangle rcClient = GetClientRectangle();
	//Platform::DebugPrintf("Client: (%3d,%3d) ... (%3d,%3d)   %d\n",
	//	rcClient.left, rcClient.top, rcClient.right, rcClient.bottom);

	if (NotifyUpdateUI()) {
		RefreshStyleData();
	}

	// Wrap the visible lines if needed.
	if (WrapLines(WrapScope::wsVisible)) {
		// The wrapping process has changed the height of some lines so
		// abandon this paint for a complete repaint.
		if (AbandonPaint()) {
			return;
		}
	}

	RefreshPixMaps(surfaceWindow);

	if (!marginView.pixmapSelPattern->Initialised()) {
		// When Direct2D is used, pixmap creation may fail with D2DERR_RECREATE_TARGET so
		// abandon this paint to avoid further failures.
		// Main drawing surface and pixmaps should be recreated by next paint.
		return;
	}

	if (!view.bufferedDraw)
		surfaceWindow->SetClip(rcArea);

	if (paintState != PaintState::abandoned) {
		if (vs.marginInside) {
			PaintSelMargin(surfaceWindow, rcArea);
			PRectangle rcRightMargin = rcClient;
			rcRightMargin.left = rcRightMargin.right - vs.rightMarginWidth;
			if (rcArea.Intersects(rcRightMargin)) {
				surfaceWindow->FillRectangle(rcRightMargin, vs.styles[StyleDefault].back);
			}
		} else { // Else separate view so separate paint event but leftMargin included to allow overlap
			PRectangle rcLeftMargin = rcArea;
			rcLeftMargin.left = 0;
			rcLeftMargin.right = rcLeftMargin.left + vs.leftMarginWidth;
			if (rcArea.Intersects(rcLeftMargin)) {
				surfaceWindow->FillRectangle(rcLeftMargin, vs.styles[StyleDefault].back);
			}
		}
	}

	if (paintState == PaintState::abandoned) {
		// Either styling or NotifyUpdateUI noticed that painting is needed
		// outside the current painting rectangle
		//Platform::DebugPrintf("Abandoning paint\n");
		if (Wrapping()) {
			if (paintAbandonedByStyling) {
				// Styling has spilled over a line end, such as occurs by starting a multiline
				// comment. The width of subsequent text may have changed, so rewrap.
				NeedWrapping(pcs->DocFromDisplay(topLine));
			}
		}
		if (!view.bufferedDraw)
			surfaceWindow->PopClip();
		return;
	}

	view.PaintText(surfaceWindow, *this, vs, rcArea, rcClient);

	if (horizontalScrollBarVisible && trackLineWidth && (view.lineWidthMaxSeen > scrollWidth)) {
		scrollWidth = view.lineWidthMaxSeen;
		if (!FineTickerRunning(TickReason::widen)) {
			FineTickerStart(TickReason::widen, 50, 5);
		}
	}

	if (!view.bufferedDraw)
		surfaceWindow->PopClip();

	NotifyPainted();
}

// Print magnification, colour mode, wrap, and FormatRange: EditorPrinting.cxx.


void Editor::SetVerticalScrollPos() {
	if (!insideWrapScroll) {
		scrollToAfterWrap.reset();
	}
}

/// Empty method is overridden on GTK+ to show / hide scrollbars
void Editor::ReconfigureScrollBars() {}

void Editor::ChangeScrollBars() {
	RefreshStyleData();

	const Sci::Line nMax = MaxScrollPos();
	const Sci::Line nPage = LinesOnScreen();
	const bool modified = ModifyScrollBars(nMax + nPage - 1, nPage);
	if (modified) {
		DwellEnd(true);
	}

	// TODO: ensure always showing as many lines as possible
	// May not be, if, for example, window made larger
	if (topLine > MaxScrollPos()) {
		SetTopLine(std::clamp<Sci::Line>(topLine, 0, MaxScrollPos()));
		SetVerticalScrollPos();
		Redraw();
	}
	if (modified) {
		if (!AbandonPaint())
			Redraw();
	}
	//Platform::DebugPrintf("end max = %d page = %d\n", nMax, nPage);
}

void Editor::SetScrollBars() {
	// Overridden on GTK to defer to idle
	ChangeScrollBars();
}

void Editor::ChangeSize() {
	DropGraphics();
	SetScrollBars();
	if (Wrapping()) {
		PRectangle rcTextArea = GetClientRectangle();
		rcTextArea.left = static_cast<XYPOSITION>(vs.textStart);
		rcTextArea.right -= vs.rightMarginWidth;
		if (wrapWidth != rcTextArea.Width()) {
			NeedWrapping();
			Redraw();
		}
	}
}

Sci::Position Editor::RealizeVirtualSpace(Sci::Position position, Sci::Position virtualSpace) {
	if (virtualSpace > 0) {
		const Sci::Line line = pdoc->SciLineFromPosition(position);
		const Sci::Position indent = pdoc->GetLineIndentPosition(line);
		if (indent == position) {
			return pdoc->SetLineIndentation(line, pdoc->GetLineIndentation(line) + virtualSpace);
		}
		const std::string spaceText(virtualSpace, ' ');
		const Sci::Position lengthInserted = pdoc->InsertString(position, spaceText);
		position += lengthInserted;
	}
	return position;
}

SelectionPosition Editor::RealizeVirtualSpace(const SelectionPosition &position) {
	// Return the new position with no virtual space
	return SelectionPosition(RealizeVirtualSpace(position.Position(), position.VirtualSpace()));
}

void Editor::AddChar(char ch) {
	const char s[1] {ch};
	InsertCharacter(std::string_view(s, 1), CharacterSource::DirectInput);
}

void Editor::FilterSelections() {
	if (!additionalSelectionTyping && (sel.Count() > 1)) {
		InvalidateWholeSelection();
		sel.DropAdditionalRanges();
	}
}

/// InsertCharacter inserts UTF-8 bytes into the document.
void Editor::InsertCharacter(std::string_view sv, CharacterSource charSource) {
	if (sv.empty()) {
		return;
	}
	FilterSelections();
	bool wrapOccurred = false;
	{
		UndoGroup ug(pdoc, (sel.Count() > 1) || !sel.Empty() || inOverstrike);

		// Vector elements point into selection in order to change selection.
		std::vector<SelectionRange *> selPtrs;
		for (size_t r = 0; r < sel.Count(); r++) {
			selPtrs.push_back(&sel.Range(r));
		}
		// Order selections by position in document.
		std::sort(selPtrs.begin(), selPtrs.end(),
			[](const SelectionRange *a, const SelectionRange *b) noexcept {return *a < *b;});

		// Loop in reverse to avoid disturbing positions of selections yet to be processed.
		for (std::vector<SelectionRange *>::reverse_iterator rit = selPtrs.rbegin();
			rit != selPtrs.rend(); ++rit) {
			SelectionRange *currentSel = *rit;
			if (!RangeContainsProtected(*currentSel)) {
				Sci::Position positionInsert = currentSel->Start().Position();
				if (!currentSel->Empty()) {
					ClearSelectionRange(*currentSel);
				} else if (inOverstrike) {
					if (positionInsert < pdoc->Length()) {
						if (!pdoc->IsPositionInLineEnd(positionInsert)) {
							pdoc->DelChar(positionInsert);
							currentSel->ClearVirtualSpace();
						}
					}
				}
				positionInsert = RealizeVirtualSpace(positionInsert, currentSel->caret.VirtualSpace());
				const Sci::Position lengthInserted = pdoc->InsertString(positionInsert, sv);
				if (lengthInserted > 0) {
					*currentSel = SelectionRange(positionInsert + lengthInserted);
				}
				currentSel->ClearVirtualSpace();
				// If in wrap mode rewrap current line so EnsureCaretVisible has accurate information
				if (Wrapping()) {
					AutoSurface surface(this);
					if (surface) {
						if (WrapOneLine(surface, pdoc->SciLineFromPosition(positionInsert))) {
							wrapOccurred = true;
						}
					}
				}
			}
		}
		ThinRectangularRange();
	}
	if (wrapOccurred) {
		SetScrollBars();
		SetVerticalScrollPos();
		Redraw();
	}
	// If in wrap mode rewrap current line so EnsureCaretVisible has accurate information
	EnsureCaretVisible();
	// Avoid blinking during rapid typing:
	ShowCaretAtCurrentPosition();
	if ((caretSticky == CaretSticky::Off) ||
		((caretSticky == CaretSticky::WhiteSpace) && !IsAllSpacesOrTabs(sv))) {
		SetLastXChosen();
	}

	int ch = static_cast<unsigned char>(sv[0]);
	if ((ch < 0xC0) || (1 == sv.length())) {
		// ASCII, single-byte values, and naked trail bytes 0x80..0xBF
		// represent themselves (one-byte characters under the invalid-UTF-8 policy).
	} else {
		unsigned int utf32[1] = { 0 };
		UTF32FromUTF8(sv, utf32, std::size(utf32));
		ch = utf32[0];
	}
	NotifyChar(ch, charSource);

	// Record committed and direct input as replace-selection; skip tentative IME.
	if (charSource != CharacterSource::TentativeInput) {
		EmitRecordedAction(RecordedReplaceSelection{std::string(sv)});
	}
}

void Editor::ClearSelectionRange(SelectionRange &range) {
	if (!range.Empty()) {
		if (range.Length()) {
			pdoc->DeleteChars(range.Start().Position(), range.Length());
			range.ClearVirtualSpace();
		} else {
			// Range is all virtual so collapse to start of virtual space
			range.MinimizeVirtualSpace();
		}
	}
}

void Editor::ClearBeforeTentativeStart() {
	// Make positions for the first composition string.
	FilterSelections();
	UndoGroup ug(pdoc, (sel.Count() > 1) || !sel.Empty() || inOverstrike);
	for (size_t r = 0; r<sel.Count(); r++) {
		if (!RangeContainsProtected(sel.Range(r))) {
			ClearSelectionRange(sel.Range(r));
			RealizeVirtualSpace(sel.Range(r).caret.Position(), sel.Range(r).caret.VirtualSpace());
			sel.Range(r).ClearVirtualSpace();
		}
	}
}

void Editor::InsertPaste(std::string_view text) {
	if (multiPasteMode == MultiPaste::Once) {
		SelectionPosition selStart = sel.Start();
		selStart = RealizeVirtualSpace(selStart);
		const Sci::Position lengthInserted = pdoc->InsertString(selStart.Position(), text);
		if (lengthInserted > 0) {
			SetEmptySelection(selStart.Position() + lengthInserted);
		}
	} else {
		// MultiPaste::Each
		for (size_t r=0; r<sel.Count(); r++) {
			if (!RangeContainsProtected(sel.Range(r))) {
				Sci::Position positionInsert = sel.Range(r).Start().Position();
				ClearSelectionRange(sel.Range(r));
				positionInsert = RealizeVirtualSpace(positionInsert, sel.Range(r).caret.VirtualSpace());
				const Sci::Position lengthInserted = pdoc->InsertString(positionInsert, text);
				if (lengthInserted > 0) {
					sel.Range(r) = SelectionRange(positionInsert + lengthInserted);
				}
				sel.Range(r).ClearVirtualSpace();
			}
		}
	}
}

void Editor::InsertPaste(const char *text, Sci::Position len) {
	InsertPaste(std::string_view(text, len));
}

void Editor::InsertPasteShape(std::string_view text, PasteShape shape) {
	std::string convertedText;
	if (convertPastes) {
		// Convert line endings of the paste into our local line-endings mode
		convertedText = Document::TransformLineEnds(text, pdoc->eolMode);
		text = convertedText;
	}
	if (shape == PasteShape::rectangular) {
		PasteRectangular(sel.Start(), text);
	} else {
		if (shape == PasteShape::line) {
			const Sci::Position insertPos = pdoc->LineStartPosition(sel.MainCaret());
			Sci::Position lengthInserted = pdoc->InsertString(insertPos, text);
			// add the newline if necessary
			if ((!text.empty()) && (!AnyOf(text.back(), '\n', '\r'))) {
				const std::string_view endline = pdoc->EOLString();
				lengthInserted += pdoc->InsertString(insertPos + lengthInserted, endline);
			}
			if (sel.MainCaret() == insertPos) {
				SetEmptySelection(sel.MainCaret() + lengthInserted);
			}
		} else {
			InsertPaste(text);
		}
	}
}

void Editor::InsertPasteShape(const char *text, Sci::Position len, PasteShape shape) {
	InsertPasteShape(std::string_view(text, len), shape);
}

void Editor::ClearSelection(bool retainMultipleSelections) {
	if (!sel.IsRectangular() && !retainMultipleSelections)
		FilterSelections();
	UndoGroup ug(pdoc);
	for (size_t r=0; r<sel.Count(); r++) {
		if (!sel.Range(r).Empty()) {
			if (!RangeContainsProtected(sel.Range(r))) {
				pdoc->DeleteChars(sel.Range(r).Start().Position(),
					sel.Range(r).Length());
				sel.Range(r) = SelectionRange(sel.Range(r).Start());
			}
		}
	}
	ThinRectangularRange();
	sel.RemoveDuplicates();
	ClaimSelection();
	SetHoverIndicatorPosition(sel.MainCaret());
}


void Editor::PasteRectangular(SelectionPosition pos, std::string_view text) {
	if (pdoc->IsReadOnly() || SelectionContainsProtected()) {
		return;
	}
	sel.Clear();
	sel.RangeMain() = SelectionRange(pos);
	Sci::Line line = pdoc->SciLineFromPosition(sel.MainCaret());
	UndoGroup ug(pdoc);
	sel.RangeMain().caret = RealizeVirtualSpace(sel.RangeMain().caret);
	const int xInsert = XFromPosition(sel.RangeMain().caret);
	bool prevCr = false;
	constexpr Sci::Position maxBatchSpaces = 8192;
	XYPOSITION maxSpaceWidth = 0;
	for (const Style &style : vs.styles) {
		maxSpaceWidth = std::max(maxSpaceWidth, style.spaceWidth);
	}
	while ((!text.empty()) && IsEOLCharacter(text.back())) {
		text.remove_suffix(1);
	}
	for (size_t i = 0; i < text.length(); i++) {
		if (IsEOLCharacter(text[i])) {
			if ((text[i] == '\r') || (!prevCr))
				line++;
			if (line >= pdoc->LinesTotal()) {
				const std::string_view eol = pdoc->EOLString();
				pdoc->InsertString(pdoc->LengthNoExcept(), eol);
			}
			// Pad the end of lines with spaces if required
			sel.RangeMain().caret.SetPosition(PositionFromLineX(line, xInsert));
			const int xCurrent = XFromPosition(sel.RangeMain().caret);
			if ((xCurrent < xInsert) && (i + 1 < text.length())) {
				if (pdoc->IsLineEndPosition(sel.MainCaret())) {
					int xAfterBatch = xCurrent;
					while ((xAfterBatch < xInsert) && (maxSpaceWidth > 0)) {
						const int missing = xInsert - xAfterBatch;
						const Sci::Position spacesEstimate = static_cast<Sci::Position>(missing / maxSpaceWidth);
						if (spacesEstimate <= 2) {
							break;
						}
						const Sci::Position batchSpaces = std::min(spacesEstimate - 1, maxBatchSpaces);
						const std::string pad(static_cast<size_t>(batchSpaces), ' ');
						const Sci::Position lengthInserted = pdoc->InsertString(sel.MainCaret(), pad);
						sel.RangeMain().caret.Add(lengthInserted);
						xAfterBatch = XFromPosition(sel.RangeMain().caret);
					}
				}

				while (XFromPosition(sel.RangeMain().caret) < xInsert) {
					assert(pdoc);
					const Sci::Position lengthInserted = pdoc->InsertString(sel.MainCaret(), " ", 1);
					sel.RangeMain().caret.Add(lengthInserted);
				}
			}
			prevCr = text[i] == '\r';
		} else {
			const Sci::Position lengthInserted = pdoc->InsertString(sel.MainCaret(), text.substr(i, 1));
			sel.RangeMain().caret.Add(lengthInserted);
			prevCr = false;
		}
	}
	SetEmptySelection(pos);
}

void Editor::PasteRectangular(SelectionPosition pos, const char *ptr, Sci::Position len) {
	PasteRectangular(pos, std::string_view(ptr, len));
}

void Editor::RestoreSelection(Sci::Position newPos, UndoRedo history) {
	EnsureModelState();
	if (FlagSet(undoSelectionHistoryOption, UndoSelectionHistoryOption::Enabled) && modelState) {
		// Undo wants the element after the current as it just undid it
		const int index = pdoc->UndoCurrent() + (history == UndoRedo::undo ? 1 : 0);
		const SelectionWithScroll selAndLine = modelState->SelectionFromStack(index, history);
		if (!selAndLine.selection.empty()) {
			if (FlagSet(undoSelectionHistoryOption, UndoSelectionHistoryOption::Scroll)) {
				ScrollTo(selAndLine.topLine);
			}
			sel = Selection(selAndLine.selection);
			if (sel.IsRectangular()) {
				const size_t mainForRectangular = sel.Main();
				// Reconstitute ranges from rectangular range
				SetRectangularRange();
				// Restore main if possible.
				if (mainForRectangular < sel.Count()) {
					sel.SetMain(mainForRectangular);
				}
			}
			newPos = -1; // Used selection from stack so don't use position returned from undo/redo.
			Redraw();
		}
	}
	if (newPos >= 0)
		SetEmptySelection(newPos);
	EnsureCaretVisible();
}

void Editor::DelCharBack(bool allowLineStartDeletion) {
	RefreshStyleData();
	if (!sel.IsRectangular())
		FilterSelections();
	if (sel.IsRectangular())
		allowLineStartDeletion = false;
	UndoGroup ug(pdoc, (sel.Count() > 1) || !sel.Empty());
	if (sel.Empty()) {
		for (size_t r=0; r<sel.Count(); r++) {
			if (!RangeContainsProtected(sel.Range(r).caret.Position() - 1, sel.Range(r).caret.Position())) {
				if (sel.Range(r).caret.VirtualSpace()) {
					sel.Range(r).caret.SetVirtualSpace(sel.Range(r).caret.VirtualSpace() - 1);
					sel.Range(r).anchor.SetVirtualSpace(sel.Range(r).caret.VirtualSpace());
				} else {
					const Sci::Line lineCurrentPos =
						pdoc->SciLineFromPosition(sel.Range(r).caret.Position());
					if (allowLineStartDeletion || (pdoc->LineStart(lineCurrentPos) != sel.Range(r).caret.Position())) {
						if (pdoc->GetColumn(sel.Range(r).caret.Position()) <= pdoc->GetLineIndentation(lineCurrentPos) &&
								pdoc->GetColumn(sel.Range(r).caret.Position()) > 0 && pdoc->backspaceUnindents) {
							UndoGroup ugInner(pdoc, !ug.Needed());
							const int indentation = pdoc->GetLineIndentation(lineCurrentPos);
							const int indentationStep = pdoc->IndentSize();
							int indentationChange = indentation % indentationStep;
							if (indentationChange == 0)
								indentationChange = indentationStep;
							const Sci::Position posSelect = pdoc->SetLineIndentation(lineCurrentPos, indentation - indentationChange);
							// SetEmptySelection
							sel.Range(r) = SelectionRange(posSelect);
						} else {
							pdoc->DelCharBack(sel.Range(r).caret.Position());
						}
					}
				}
			} else {
				sel.Range(r).ClearVirtualSpace();
			}
		}
		ThinRectangularRange();
	} else {
		ClearSelection();
	}
	sel.RemoveDuplicates();
	ContainerNeedsUpdate(Update::Selection);
	// Avoid blinking during rapid typing:
	ShowCaretAtCurrentPosition();
}

void Editor::NotifyFocus(bool focus) {
	NotificationData scn = {};
	scn.code = focus ? Notification::FocusIn : Notification::FocusOut;
	NotifyParent(scn);
}

/// Ask an external lexer to style through endStyleNeeded. Styling starts at the
/// beginning of the line containing the current end-styled position.
void Editor::NotifyStyleToNeeded(Sci::Position endStyleNeeded) {
	NotificationData scn = {};
	scn.code = Notification::StyleNeeded;
	scn.position = endStyleNeeded;
	NotifyParent(scn);
}

void Editor::NotifyStyleNeeded(Document *, void *, Sci::Position endStyleNeeded) {
	NotifyStyleToNeeded(endStyleNeeded);
}

void Editor::NotifyGroupCompleted(Document *, void *) noexcept {
	// RememberCurrentSelectionForRedoOntoStack may throw (for memory exhaustion)
	// but this method may not as it is called in UndoGroup destructor so ignore
	// exception.
	try {
		RememberCurrentSelectionForRedoOntoStack();
	} catch (...) {
		// Ignore any exception
	}
}

/// Report an entered text character before it is styled. ch is its Unicode code
/// point, or its byte value for one-byte invalid UTF-8. characterSource identifies
/// direct, tentative, or completed IME input.
void Editor::NotifyChar(int ch, CharacterSource charSource) {
	NotificationData scn = {};
	scn.code = Notification::CharAdded;
	scn.ch = ch;
	scn.characterSource = charSource;
	NotifyParent(scn);
}

/// Report transitions into and out of the document save point so the host can
/// update its dirty indicator and command state.
void Editor::NotifySavePoint(bool isSavePoint) {
	NotificationData scn = {};
	if (isSavePoint) {
		scn.code = Notification::SavePointReached;
		if (changeHistoryOption != ChangeHistoryOption::Disabled) {
			Redraw();
		}
	} else {
		scn.code = Notification::SavePointLeft;
	}
	NotifyParent(scn);
}

/// Report an attempted text change rejected because the document is read-only.
void Editor::NotifyModifyAttempt() {
	NotificationData scn = {};
	scn.code = Notification::ModifyAttemptRO;
	NotifyParent(scn);
}

/// DoubleClick reports the document position, its line, and the held modifiers.
void Editor::NotifyDoubleClick(Point pt, KeyMod modifiers) {
	NotificationData scn = {};
	scn.code = Notification::DoubleClick;
	scn.line = LineFromLocation(pt);
	scn.position = PositionFromLocation(pt, true);
	scn.modifiers = modifiers;
	NotifyParent(scn);
}

/// Hotspot click events report the document position and held modifiers. Release
/// reports only the Ctrl modifier because that is all the mouse path preserves.
void Editor::NotifyHotSpotDoubleClicked(Sci::Position position, KeyMod modifiers) {
	NotificationData scn = {};
	scn.code = Notification::HotSpotDoubleClick;
	scn.position = position;
	scn.modifiers = modifiers;
	NotifyParent(scn);
}

void Editor::NotifyHotSpotClicked(Sci::Position position, KeyMod modifiers) {
	NotificationData scn = {};
	scn.code = Notification::HotSpotClick;
	scn.position = position;
	scn.modifiers = modifiers;
	NotifyParent(scn);
}

void Editor::NotifyHotSpotReleaseClick(Sci::Position position, KeyMod modifiers) {
	NotificationData scn = {};
	scn.code = Notification::HotSpotReleaseClick;
	scn.position = position;
	scn.modifiers = modifiers;
	NotifyParent(scn);
}

/// updated is the accumulated set of content, selection, and scroll changes
/// since the previous UpdateUI notification. A flag may be present conservatively.
bool Editor::NotifyUpdateUI() {
	if (needUpdateUI != Update::None) {
		NotificationData scn = {};
		scn.code = Notification::UpdateUI;
		scn.updated = needUpdateUI;
		NotifyParent(scn);
		needUpdateUI = Update::None;
		return true;
	}
	return false;
}

void Editor::NotifyPainted() {
	NotificationData scn = {};
	scn.code = Notification::Painted;
	NotifyParent(scn);
}

/// Indicator click and release report the document position and held modifiers.
/// Release is sent only after a notified press.
void Editor::NotifyIndicatorClick(bool click, Sci::Position position, KeyMod modifiers) {
	const int mask = pdoc->decorations->AllOnFor(position);
	if ((click && mask) || pdoc->decorations->ClickNotified()) {
		NotificationData scn = {};
		pdoc->decorations->SetClickNotified(click);
		scn.code = click ? Notification::IndicatorClick : Notification::IndicatorRelease;
		scn.modifiers = modifiers;
		scn.position = position;
		NotifyParent(scn);
	}
}

/// Sensitive margin clicks report the margin number, the start position of the
/// clicked line, and held modifiers. Automatic fold clicks are handled directly.
bool Editor::NotifyMarginClick(Point pt, KeyMod modifiers) {
	const int marginClicked = vs.MarginFromLocation(pt);
	if ((marginClicked >= 0) && vs.ms[marginClicked].sensitive) {
		const Sci::Position position = pdoc->LineStart(LineFromLocation(pt));
		if ((vs.ms[marginClicked].mask & MaskFolders) && (FlagSet(foldAutomatic, AutomaticFold::Click))) {
			const bool ctrl = FlagSet(modifiers, KeyMod::Ctrl);
			const bool shift = FlagSet(modifiers, KeyMod::Shift);
			const Sci::Line lineClick = pdoc->SciLineFromPosition(position);
			if (shift && ctrl) {
				FoldAll(FoldAction::Toggle);
			} else {
				const FoldLevel levelClick = pdoc->GetFoldLevel(lineClick);
				if (LevelIsHeader(levelClick)) {
					if (shift) {
						// Ensure all children visible
						FoldExpand(lineClick, FoldAction::Expand, levelClick);
					} else if (ctrl) {
						FoldExpand(lineClick, FoldAction::Toggle, levelClick);
					} else {
						// Toggle this line
						FoldLine(lineClick, FoldAction::Toggle);
					}
				}
			}
			return true;
		}
		NotificationData scn = {};
		scn.code = Notification::MarginClick;
		scn.modifiers = modifiers;
		scn.position = position;
		scn.margin = marginClicked;
		NotifyParent(scn);
		return true;
	}
	return false;
}

bool Editor::NotifyMarginRightClick(Point pt, KeyMod modifiers) {
	const int marginRightClicked = vs.MarginFromLocation(pt);
	if ((marginRightClicked >= 0) && vs.ms[marginRightClicked].sensitive) {
		const Sci::Position position = pdoc->LineStart(LineFromLocation(pt));
		NotificationData scn = {};
		scn.code = Notification::MarginRightClick;
		scn.modifiers = modifiers;
		scn.position = position;
		scn.margin = marginRightClicked;
		NotifyParent(scn);
		return true;
	}
	return false;
}

/// Ask the host to reveal the document range [pos, pos + len), normally after a
/// change affects text hidden by folding.
void Editor::NotifyNeedShown(Sci::Position pos, Sci::Position len) {
	NotificationData scn = {};
	scn.code = Notification::NeedShown;
	scn.position = pos;
	scn.length = len;
	NotifyParent(scn);
}

/// DwellStart and DwellEnd report the document position and editor-relative x/y
/// coordinates where the dwell began or ended.
void Editor::NotifyDwelling(Point pt, bool state) {
	NotificationData scn = {};
	scn.code = state ? Notification::DwellStart : Notification::DwellEnd;
	scn.position = PositionFromLocation(pt, true);
	scn.x = static_cast<int>(pt.x + vs.ExternalMarginWidth());
	scn.y = static_cast<int>(pt.y);
	NotifyParent(scn);
}

/// Report a zoom change after the display scale has changed; there is no payload.
void Editor::NotifyZoom() {
	NotificationData scn = {};
	scn.code = Notification::Zoom;
	NotifyParent(scn);
}

/// Notifications from document
void Editor::NotifyModifyAttempt(Document *, void *) {
	//Platform::DebugPrintf("** Modify Attempt\n");
	NotifyModifyAttempt();
}

void Editor::NotifySavePoint(Document *, void *, bool atSavePoint) {
	//Platform::DebugPrintf("** Save Point %s\n", atSavePoint ? "On" : "Off");
	NotifySavePoint(atSavePoint);
}

namespace {

/// Move a position so it is still after the same character as before the insertion.
constexpr Sci::Position MovePositionForInsertion(Sci::Position position, Sci::Position startInsertion, Sci::Position length) noexcept {
	if (position > startInsertion) {
		return position + length;
	}
	return position;
}

/// Move a position so it is still after the same character as before the deletion if that
/// character is still present else after the previous surviving character.
constexpr Sci::Position MovePositionForDeletion(Sci::Position position, Sci::Position startDeletion, Sci::Position length) noexcept {
	if (position > startDeletion) {
		const Sci::Position endDeletion = startDeletion + length;
		if (position > endDeletion) {
			return position - length;
		}
		return startDeletion;
	}
	return position;
}

}

void Editor::NotifyModified(Document *, DocModification mh, void *) {
	ContainerNeedsUpdate(Update::Content);
	if (paintState == PaintState::painting) {
		CheckForChangeOutsidePaint(Range(mh.position, mh.position + mh.length));
	}
	if (FlagSet(mh.modificationType, ModificationFlags::ChangeLineState)) {
		if (paintState == PaintState::painting) {
			CheckForChangeOutsidePaint(
			    Range(pdoc->LineStart(mh.line),
					pdoc->LineStart(mh.line + 1)));
		} else {
			// Could check that change is before last visible line.
			Redraw();
		}
	}
	if (FlagSet(mh.modificationType, ModificationFlags::ChangeTabStops)) {
		Redraw();
	}
	if (FlagSet(mh.modificationType, ModificationFlags::LexerState)) {
		if (paintState == PaintState::painting) {
			CheckForChangeOutsidePaint(
			    Range(mh.position, mh.position + mh.length));
		} else {
			Redraw();
		}
	}
	if (FlagSet(mh.modificationType, ModificationFlags::ChangeStyle | ModificationFlags::ChangeIndicator)) {
		if (FlagSet(mh.modificationType, ModificationFlags::ChangeStyle)) {
			pdoc->IncrementStyleClock();
		}
		if (paintState == PaintState::notPainting) {
			const Sci::Line lineDocTop = pcs->DocFromDisplay(topLine);
			if (mh.position < pdoc->LineStart(lineDocTop)) {
				// Styling performed before this view
				Redraw();
			} else {
				InvalidateRange(mh.position, mh.position + mh.length);
			}
		}
		if (FlagSet(mh.modificationType, ModificationFlags::ChangeStyle)) {
			view.llc.Invalidate(LineLayout::ValidLevel::checkTextAndStyle);
		}
	} else {
		if (FlagSet(undoSelectionHistoryOption, UndoSelectionHistoryOption::Enabled) &&
			FlagSet(mh.modificationType, ModificationFlags::User)) {
			if (FlagSet(mh.modificationType, ModificationFlags::BeforeInsert | ModificationFlags::BeforeDelete)) {
				RememberSelectionForUndo(pdoc->UndoCurrent());
			}
			if (FlagSet(mh.modificationType, ModificationFlags::InsertText | ModificationFlags::DeleteText)) {
				RememberSelectionOntoStack(pdoc->UndoCurrent());
			}
		}
		// Move selection and brace highlights
		if (FlagSet(mh.modificationType, ModificationFlags::InsertText)) {
			sel.MovePositions(true, mh.position, mh.length);
			braces[0] = MovePositionForInsertion(braces[0], mh.position, mh.length);
			braces[1] = MovePositionForInsertion(braces[1], mh.position, mh.length);
		} else if (FlagSet(mh.modificationType, ModificationFlags::DeleteText)) {
			sel.MovePositions(false, mh.position, mh.length);
			braces[0] = MovePositionForDeletion(braces[0], mh.position, mh.length);
			braces[1] = MovePositionForDeletion(braces[1], mh.position, mh.length);
		}
		if (FlagSet(mh.modificationType, ModificationFlags::BeforeInsert | ModificationFlags::BeforeDelete) && pcs->HiddenLines()) {
			// Some lines are hidden so may need shown.
			const Sci::Line lineOfPos = pdoc->SciLineFromPosition(mh.position);
			Sci::Position endNeedShown = mh.position;
			if (FlagSet(mh.modificationType, ModificationFlags::BeforeInsert)) {
				if (pdoc->ContainsLineEnd(mh.text, mh.length) && (mh.position != pdoc->LineStart(lineOfPos)))
					endNeedShown = pdoc->LineStart(lineOfPos+1);
			} else {
				// If the deletion includes any EOL then we extend the need shown area.
				endNeedShown = mh.position + mh.length;
				Sci::Line lineLast = pdoc->SciLineFromPosition(mh.position+mh.length);
				for (Sci::Line line = lineOfPos + 1; line <= lineLast; line++) {
					const Sci::Line lineMaxSubord = pdoc->GetLastChild(line, {}, -1);
					if (lineLast < lineMaxSubord) {
						lineLast = lineMaxSubord;
						endNeedShown = pdoc->LineEnd(lineLast);
					}
				}
			}
			NeedShown(mh.position, endNeedShown - mh.position);
		}
		if (mh.linesAdded != 0) {
			// Update contraction state for inserted and removed lines
			// lineOfPos should be calculated in context of state before modification, shouldn't it
			Sci::Line lineOfPos = pdoc->SciLineFromPosition(mh.position);
			if (mh.position > pdoc->LineStart(lineOfPos))
				lineOfPos++;	// Affecting subsequent lines
			if (mh.linesAdded > 0) {
				pcs->InsertLines(lineOfPos, mh.linesAdded);
			} else {
				pcs->DeleteLines(lineOfPos, -mh.linesAdded);
			}
			view.LinesAddedOrRemoved(lineOfPos, mh.linesAdded);
		}
		if (FlagSet(mh.modificationType, ModificationFlags::ChangeAnnotation)) {
			const Sci::Line lineDoc = pdoc->SciLineFromPosition(mh.position);
			if (vs.annotationVisible != AnnotationVisible::Hidden) {
				if (pcs->SetHeight(lineDoc, pcs->GetHeight(lineDoc) + static_cast<int>(mh.annotationLinesAdded))) {
					SetScrollBars();
				}
				Redraw();
			}
		}
		if (FlagSet(mh.modificationType, ModificationFlags::ChangeEOLAnnotation)) {
			if (vs.eolAnnotationVisible != EOLAnnotationVisible::Hidden) {
				Redraw();
			}
		}
		CheckModificationForWrap(mh);
		if (mh.linesAdded != 0) {
			// Avoid scrolling of display if change before current display
			if (mh.position < posTopLine && !CanDeferToLastStep(mh)) {
				const Sci::Line newTop = std::clamp<Sci::Line>(topLine + mh.linesAdded, 0, MaxScrollPos());
				if (newTop != topLine) {
					SetTopLine(newTop);
					SetVerticalScrollPos();
				}
			}

			if (paintState == PaintState::notPainting && !CanDeferToLastStep(mh)) {
				if (SynchronousStylingToVisible()) {
					QueueIdleWork(WorkItems::style, pdoc->Length());
				}
				Redraw();
			}
		} else {
			if (paintState == PaintState::notPainting && mh.length && !CanEliminate(mh)) {
				if (SynchronousStylingToVisible()) {
					QueueIdleWork(WorkItems::style, mh.position + mh.length);
				}
				InvalidateRange(mh.position, mh.position + mh.length);
				if (FlagSet(changeHistoryOption, ChangeHistoryOption::Markers)) {
					RedrawSelMargin(pdoc->SciLineFromPosition(mh.position));
				}
			}
		}
	}

	if (mh.linesAdded != 0 && !CanDeferToLastStep(mh)) {
		SetScrollBars();
	}

	if (FlagSet(mh.modificationType, (ModificationFlags::ChangeMarker | ModificationFlags::ChangeMargin))) {
		if ((!willRedrawAll) && ((paintState == PaintState::notPainting) || !PaintContainsMargin())) {
			if (FlagSet(mh.modificationType, ModificationFlags::ChangeFold)) {
				// Fold changes can affect the drawing of following lines so redraw whole margin
				RedrawSelMargin(marginView.highlightDelimiter.isEnabled ? -1 : mh.line - 1, true);
			} else {
				RedrawSelMargin(mh.line);
			}
		}
	}
	if ((FlagSet(mh.modificationType, ModificationFlags::ChangeFold)) && (FlagSet(foldAutomatic, AutomaticFold::Change))) {
		FoldChanged(mh.line, mh.foldLevelNow, mh.foldLevelPrev);
	}

	// NOW pay the piper WRT "deferred" visual updates
	if (IsLastStep(mh)) {
		SetScrollBars();
		Redraw();
	}

	if (FlagSet(mh.modificationType, ModificationFlags::Undo | ModificationFlags::Redo)
		&& FlagSet(mh.modificationType, ModificationFlags::LastStepInUndoRedo)
		&& !pdoc->TentativeActive()) {
		// Update selection and scroll
		RestoreSelection(mh.newPos,
			FlagSet(mh.modificationType, ModificationFlags::Undo) ? UndoRedo::undo : UndoRedo::redo);
	}

	// If client wants to see this modification
	if (FlagSet(mh.modificationType, modEventMask)) {
		if (commandEvents) {
			if ((mh.modificationType & (ModificationFlags::ChangeStyle | ModificationFlags::ChangeIndicator)) == ModificationFlags::None) {
				// Real modification made to text of document.
				NotifyChange();	// Send EN_CHANGE
			}
		}

		// Modified reports what changed and whether it is before or after the change.
		// The host must not modify the document while handling this notification.
		NotificationData scn = {};
		scn.code = Notification::Modified;
		scn.position = mh.position;
		scn.modificationType = mh.modificationType;
		scn.text = mh.text;
		scn.length = mh.length;
		scn.linesAdded = mh.linesAdded;
		scn.line = mh.line;
		scn.foldLevelNow = mh.foldLevelNow;
		scn.foldLevelPrev = mh.foldLevelPrev;
		scn.token = static_cast<int>(mh.token);
		scn.annotationLinesAdded = mh.annotationLinesAdded;
		NotifyParent(scn);
	}
}

void Editor::NotifyDeleted(Document *, void *) noexcept {
	/* Do nothing */
}

/// Something has changed that the container should know about
void Editor::ContainerNeedsUpdate(Update flags) noexcept {
	needUpdateUI = needUpdateUI | flags;
}

/**
 * Force scroll and keep position relative to top of window.
 *
 * If stuttered = true and not already at first/last row, move to first/last row of window.
 * If stuttered = true and already at first/last row, scroll as normal.
 */
void Editor::PageMove(int direction, Selection::SelTypes selt, bool stuttered) {
	Sci::Line topLineNew;
	SelectionPosition newPos;

	const Sci::Line currentLine = pdoc->SciLineFromPosition(sel.MainCaret());
	const Sci::Line topStutterLine = topLine + caretPolicies.y.slop;
	const Sci::Line bottomStutterLine =
	    pdoc->SciLineFromPosition(PositionFromLocation(
	                Point::FromInts(lastXChosen - xOffset, direction * vs.lineHeight * static_cast<int>(LinesToScroll()))))
	    - caretPolicies.y.slop - 1;

	if (stuttered && (direction < 0 && currentLine > topStutterLine)) {
		topLineNew = topLine;
		newPos = SPositionFromLocation(Point::FromInts(lastXChosen - xOffset, vs.lineHeight * caretPolicies.y.slop),
			false, false, UserVirtualSpace());

	} else if (stuttered && (direction > 0 && currentLine < bottomStutterLine)) {
		topLineNew = topLine;
		newPos = SPositionFromLocation(Point::FromInts(lastXChosen - xOffset, vs.lineHeight * static_cast<int>(LinesToScroll() - caretPolicies.y.slop)),
			false, false, UserVirtualSpace());

	} else {
		const Point pt = LocationFromPosition(sel.MainCaret());

		topLineNew = std::clamp<Sci::Line>(
		            topLine + direction * LinesToScroll(), 0, MaxScrollPos());
		newPos = SPositionFromLocation(
			Point::FromInts(lastXChosen - xOffset, static_cast<int>(pt.y) +
				direction * (vs.lineHeight * static_cast<int>(LinesToScroll()))),
			false, false, UserVirtualSpace());
	}

	if (topLineNew != topLine) {
		SetTopLine(topLineNew);
		MovePositionTo(newPos, selt);
		SetVerticalScrollPos();
		Redraw();
	} else {
		MovePositionTo(newPos, selt);
	}
}

void Editor::ChangeCaseOfSelection(CaseMapping caseMapping) {
	UndoGroup ug(pdoc);
	for (size_t r=0; r<sel.Count(); r++) {
		SelectionRange current = sel.Range(r);
		SelectionRange currentNoVS = current;
		currentNoVS.ClearVirtualSpace();
		const size_t rangeBytes = currentNoVS.Length();
		if (rangeBytes > 0 && !RangeContainsProtected(currentNoVS)) {
			std::string sText = RangeText(currentNoVS.Start().Position(), currentNoVS.End().Position());

			std::string sMapped = CaseMapString(sText, caseMapping);

			if (sMapped != sText) {
				size_t firstDifference = 0;
				while (sMapped[firstDifference] == sText[firstDifference])
					firstDifference++;
				size_t lastDifferenceText = sText.size() - 1;
				size_t lastDifferenceMapped = sMapped.size() - 1;
				while (sMapped[lastDifferenceMapped] == sText[lastDifferenceText]) {
					lastDifferenceText--;
					lastDifferenceMapped--;
				}
				const size_t endDifferenceText = sText.size() - 1 - lastDifferenceText;
				pdoc->DeleteChars(
					currentNoVS.Start().Position() + firstDifference,
					rangeBytes - firstDifference - endDifferenceText);
				const Sci::Position lengthChange = lastDifferenceMapped - firstDifference + 1;
				const Sci::Position lengthInserted = pdoc->InsertString(
					currentNoVS.Start().Position() + firstDifference,
					sMapped.c_str() + firstDifference,
					lengthChange);
				// Automatic movement changes selection so reset to exactly the same as it was.
				const Sci::Position diffSizes = sMapped.size() - sText.size() + lengthInserted - lengthChange;
				if (diffSizes != 0) {
					if (current.anchor > current.caret)
						current.anchor.Add(diffSizes);
					else
						current.caret.Add(diffSizes);
				}
				sel.Range(r) = current;
			}
		}
	}
}

void Editor::LineDelete() {
	const Sci::Line line = pdoc->SciLineFromPosition(sel.MainCaret());
	const Sci::Position start = pdoc->LineStart(line);
	const Sci::Position end = pdoc->LineStart(line + 1);
	pdoc->DeleteChars(start, end - start);
}

void Editor::LineTranspose() {
	const Sci::Line line = pdoc->SciLineFromPosition(sel.MainCaret());
	if (line > 0) {
		UndoGroup ug(pdoc);

		const Sci::Position startPrevious = pdoc->LineStart(line - 1);
		const std::string linePrevious = RangeText(startPrevious, pdoc->LineEnd(line - 1));

		Sci::Position startCurrent = pdoc->LineStart(line);
		const std::string lineCurrent = RangeText(startCurrent, pdoc->LineEnd(line));

		pdoc->DeleteChars(startCurrent, lineCurrent.length());
		pdoc->DeleteChars(startPrevious, linePrevious.length());
		startCurrent -= linePrevious.length();

		startCurrent += pdoc->InsertString(startPrevious, lineCurrent);
		pdoc->InsertString(startCurrent, linePrevious);
		// Move caret to start of current line
		MovePositionTo(SelectionPosition(startCurrent));
	}
}

void Editor::LineReverse() {
	const Sci::Line lineStart =
		pdoc->SciLineFromPosition(sel.RangeMain().Start().Position());
	const Sci::Line lineEnd =
		pdoc->SciLineFromPosition(sel.RangeMain().End().Position()-1);
	const Sci::Line lineDiff = lineEnd - lineStart;
	if (lineDiff <= 0)
		return;
	UndoGroup ug(pdoc);
	for (Sci::Line i=(lineDiff+1)/2-1; i>=0; --i) {
		const Sci::Line lineNum2 = lineEnd - i;
		const Sci::Line lineNum1 = lineStart + i;
		Sci::Position lineStart2 = pdoc->LineStart(lineNum2);
		const Sci::Position lineStart1 = pdoc->LineStart(lineNum1);
		const std::string line2 = RangeText(lineStart2, pdoc->LineEnd(lineNum2));
		const std::string line1 = RangeText(lineStart1, pdoc->LineEnd(lineNum1));
		const Sci::Position lineLen2 = line2.length();
		const Sci::Position lineLen1 = line1.length();
		pdoc->DeleteChars(lineStart2, lineLen2);
		pdoc->DeleteChars(lineStart1, lineLen1);
		lineStart2 -= lineLen1;
		pdoc->InsertString(lineStart2, line1);
		pdoc->InsertString(lineStart1, line2);
	}
	// Wholly select all affected lines
	sel.RangeMain() = SelectionRange(pdoc->LineStart(lineStart),
		pdoc->LineStart(lineEnd+1));
}

void Editor::Duplicate(bool forLine) {
	if (sel.Empty()) {
		forLine = true;
	}
	UndoGroup ug(pdoc);
	std::string_view eol;
	if (forLine) {
		eol = pdoc->EOLString();
	}
	for (size_t r=0; r<sel.Count(); r++) {
		SelectionPosition start = sel.Range(r).Start();
		SelectionPosition end = sel.Range(r).End();
		if (forLine) {
			const Sci::Line line = pdoc->SciLineFromPosition(sel.Range(r).caret.Position());
			start = SelectionPosition(pdoc->LineStart(line));
			end = SelectionPosition(pdoc->LineEnd(line));
		}
		std::string text = RangeText(start.Position(), end.Position());
		Sci::Position lengthInserted = 0;
		if (forLine)
			lengthInserted = pdoc->InsertString(end.Position(), eol);
		pdoc->InsertString(end.Position() + lengthInserted, text);
	}
	if (sel.Count() && sel.IsRectangular()) {
		SelectionPosition last = sel.Last();
		if (forLine) {
			const Sci::Line line = pdoc->SciLineFromPosition(last.Position());
			last = SelectionPosition(last.Position() +
				pdoc->LineStart(line+1) - pdoc->LineStart(line));
		}
		if (sel.Rectangular().anchor > sel.Rectangular().caret)
			sel.Rectangular().anchor = last;
		else
			sel.Rectangular().caret = last;
		SetRectangularRange();
	}
}

void Editor::CancelModes() {
	sel.SetMoveExtends(false);
}

void Editor::NewLine() {
	InvalidateWholeSelection();
	if (sel.IsRectangular() || !additionalSelectionTyping) {
		// Remove non-main ranges
		sel.DropAdditionalRanges();
	}

	UndoGroup ug(pdoc, !sel.Empty() || (sel.Count() > 1));

	// Clear each range
	if (!sel.Empty()) {
		ClearSelection();
	}

	// Insert each line end
	size_t countInsertions = 0;
	const std::string_view eol = pdoc->EOLString();
	for (size_t r = 0; r < sel.Count(); r++) {
		sel.Range(r).ClearVirtualSpace();
		const Sci::Position positionInsert = sel.Range(r).caret.Position();
		const Sci::Position insertLength = pdoc->InsertString(positionInsert, eol);
		if (insertLength > 0) {
			sel.Range(r) = SelectionRange(positionInsert + insertLength);
			countInsertions++;
		}
	}

	// Perform notifications after all the changes as the application may change the
	// selections in response to the characters.
	// Newline is not a RecordedCommand; each EOL byte is stored as ReplaceSelection
	// so replay inserts the same line ending bytes the document used.
	for (size_t i = 0; i < countInsertions; i++) {
		for (const char ch : eol) {
			NotifyChar(ch, CharacterSource::DirectInput);
			EmitRecordedAction(RecordedReplaceSelection{std::string(1, ch)});
		}
	}

	SetLastXChosen();
	SetScrollBars();
	EnsureCaretVisible();
	// Avoid blinking during rapid typing:
	ShowCaretAtCurrentPosition();
}

SelectionPosition Editor::PositionUpOrDown(SelectionPosition spStart, int direction, int lastX) {
	const Point pt = LocationFromPosition(spStart);
	int skipLines = 0;

	if (vs.annotationVisible != AnnotationVisible::Hidden) {
		const Sci::Line lineDoc = pdoc->SciLineFromPosition(spStart.Position());
		const Point ptStartLine = LocationFromPosition(pdoc->LineStart(lineDoc));
		const int subLine = static_cast<int>(pt.y - ptStartLine.y) / vs.lineHeight;

		if (direction < 0 && subLine == 0) {
			const Sci::Line lineDisplay = pcs->DisplayFromDoc(lineDoc);
			if (lineDisplay > 0) {
				skipLines = pdoc->AnnotationLines(pcs->DocFromDisplay(lineDisplay - 1));
			}
		} else if (direction > 0 && subLine >= (pcs->GetHeight(lineDoc) - 1 - pdoc->AnnotationLines(lineDoc))) {
			skipLines = pdoc->AnnotationLines(lineDoc);
		}
	}

	const Sci::Line newY = static_cast<Sci::Line>(pt.y) + (1 + skipLines) * direction * vs.lineHeight;
	if (lastX < 0) {
		lastX = static_cast<int>(pt.x) + xOffset;
	}
	SelectionPosition posNew = SPositionFromLocation(
		Point::FromInts(lastX - xOffset, static_cast<int>(newY)), false, false, UserVirtualSpace());

	if (direction < 0) {
		// Line wrapping may lead to a location on the same line, so
		// seek back if that is the case.
		Point ptNew = LocationFromPosition(posNew.Position());
		while ((posNew.Position() > 0) && (pt.y == ptNew.y)) {
			posNew.Add(-1);
			posNew.SetVirtualSpace(0);
			ptNew = LocationFromPosition(posNew.Position());
		}
	} else if (direction > 0 && posNew.Position() != pdoc->Length()) {
		// There is an equivalent case when moving down which skips
		// over a line.
		Point ptNew = LocationFromPosition(posNew.Position());
		while ((posNew.Position() > spStart.Position()) && (ptNew.y > static_cast<XYPOSITION>(newY))) {
			posNew.Add(-1);
			posNew.SetVirtualSpace(0);
			ptNew = LocationFromPosition(posNew.Position());
		}
	}
	return posNew;
}

void Editor::CursorUpOrDown(int direction, Selection::SelTypes selt) {
	if ((selt == Selection::SelTypes::none) && sel.MoveExtends()) {
		selt = !sel.IsRectangular() ? Selection::SelTypes::stream : Selection::SelTypes::rectangle;
	}
	SelectionPosition caretToUse = sel.RangeMain().caret;
	if (sel.IsRectangular()) {
		if (selt ==  Selection::SelTypes::none) {
			caretToUse = (direction > 0) ? sel.Limits().end : sel.Limits().start;
		} else {
			caretToUse = sel.Rectangular().caret;
		}
	}
	if (selt == Selection::SelTypes::rectangle) {
		const SelectionRange rangeBase = sel.IsRectangular() ? sel.Rectangular() : sel.RangeMain();
		if (!sel.IsRectangular()) {
			InvalidateWholeSelection();
			sel.DropAdditionalRanges();
		}
		const SelectionPosition posNew = MovePositionSoVisible(
			PositionUpOrDown(caretToUse, direction, lastXChosen), direction);
		sel.selType = Selection::SelTypes::rectangle;
		sel.Rectangular() = SelectionRange(posNew, rangeBase.anchor);
		SetRectangularRange();
		MovedCaret(posNew, caretToUse, true, caretPolicies);
	} else if (sel.selType == Selection::SelTypes::lines && sel.MoveExtends()) {
		// Calculate new caret position and call SetSelection(), which will ensure whole lines are selected.
		const SelectionPosition posNew = MovePositionSoVisible(
			PositionUpOrDown(caretToUse, direction, -1), direction);
		SetSelection(posNew, sel.RangeMain().anchor);
	} else {
		InvalidateWholeSelection();
		if (!additionalSelectionTyping || (sel.IsRectangular())) {
			sel.DropAdditionalRanges();
		}
		sel.selType = Selection::SelTypes::stream;
		for (size_t r = 0; r < sel.Count(); r++) {
			const int lastX = (r == sel.Main()) ? lastXChosen : -1;
			const SelectionPosition spCaretNow = sel.Range(r).caret;
			const SelectionPosition posNew = MovePositionSoVisible(
				PositionUpOrDown(spCaretNow, direction, lastX), direction);
			sel.Range(r) = selt == Selection::SelTypes::stream ?
				SelectionRange(posNew, sel.Range(r).anchor) : SelectionRange(posNew);
		}
		sel.RemoveDuplicates();
		MovedCaret(sel.RangeMain().caret, caretToUse, true, caretPolicies);
	}
}

void Editor::ParaUpOrDown(int direction, Selection::SelTypes selt) {
	Sci::Line lineDoc;
	const Sci::Position savedPos = sel.MainCaret();
	do {
		MovePositionTo(SelectionPosition(direction > 0 ? pdoc->ParaDown(sel.MainCaret()) : pdoc->ParaUp(sel.MainCaret())), selt);
		lineDoc = pdoc->SciLineFromPosition(sel.MainCaret());
		if (direction > 0) {
			if (sel.MainCaret() >= pdoc->Length() && !pcs->GetVisible(lineDoc)) {
				if (selt == Selection::SelTypes::none) {
					MovePositionTo(SelectionPosition(pdoc->LineEndPosition(savedPos)));
				}
				break;
			}
		}
	} while (!pcs->GetVisible(lineDoc));
}

Range Editor::RangeDisplayLine(Sci::Line lineVisible) {
	RefreshStyleData();
	AutoSurface surface(this);
	return view.RangeDisplayLine(surface, *this, lineVisible, vs);
}

Sci::Position Editor::StartEndDisplayLine(Sci::Position pos, bool start) {
	RefreshStyleData();
	AutoSurface surface(this);
	const Sci::Position posRet = view.StartEndDisplayLine(surface, *this, pos, start, vs);
	if (posRet == Sci::invalidPosition) {
		return pos;
	}
	return posRet;
}

namespace {

constexpr EditorCommand WithExtends(EditorCommand command) noexcept {
	switch (command) {
	case EditorCommand::CharLeft: return EditorCommand::CharLeftExtend;
	case EditorCommand::CharRight: return EditorCommand::CharRightExtend;

	case EditorCommand::WordLeft: return EditorCommand::WordLeftExtend;
	case EditorCommand::WordRight: return EditorCommand::WordRightExtend;
	case EditorCommand::WordLeftEnd: return EditorCommand::WordLeftEndExtend;
	case EditorCommand::WordRightEnd: return EditorCommand::WordRightEndExtend;
	case EditorCommand::WordPartLeft: return EditorCommand::WordPartLeftExtend;
	case EditorCommand::WordPartRight: return EditorCommand::WordPartRightExtend;

	case EditorCommand::Home: return EditorCommand::HomeExtend;
	case EditorCommand::HomeDisplay: return EditorCommand::HomeDisplayExtend;
	case EditorCommand::HomeWrap: return EditorCommand::HomeWrapExtend;
	case EditorCommand::VCHome: return EditorCommand::VCHomeExtend;
	case EditorCommand::VCHomeDisplay: return EditorCommand::VCHomeDisplayExtend;
	case EditorCommand::VCHomeWrap: return EditorCommand::VCHomeWrapExtend;

	case EditorCommand::LineEnd: return EditorCommand::LineEndExtend;
	case EditorCommand::LineEndDisplay: return EditorCommand::LineEndDisplayExtend;
	case EditorCommand::LineEndWrap: return EditorCommand::LineEndWrapExtend;

	default:	return command;
	}
}

constexpr int NaturalDirection(EditorCommand command) noexcept {
	switch (command) {
	case EditorCommand::CharLeft:
	case EditorCommand::CharLeftExtend:
	case EditorCommand::CharLeftRectExtend:
	case EditorCommand::WordLeft:
	case EditorCommand::WordLeftExtend:
	case EditorCommand::WordLeftEnd:
	case EditorCommand::WordLeftEndExtend:
	case EditorCommand::WordPartLeft:
	case EditorCommand::WordPartLeftExtend:
	case EditorCommand::Home:
	case EditorCommand::HomeExtend:
	case EditorCommand::HomeDisplay:
	case EditorCommand::HomeDisplayExtend:
	case EditorCommand::HomeWrap:
	case EditorCommand::HomeWrapExtend:
		// VC_HOME* mostly goes back
	case EditorCommand::VCHome:
	case EditorCommand::VCHomeExtend:
	case EditorCommand::VCHomeDisplay:
	case EditorCommand::VCHomeDisplayExtend:
	case EditorCommand::VCHomeWrap:
	case EditorCommand::VCHomeWrapExtend:
		return -1;

	default:
		return 1;
	}
}

constexpr bool IsRectExtend(EditorCommand command, bool isRectMoveExtends) noexcept {
	switch (command) {
	case EditorCommand::CharLeftRectExtend:
	case EditorCommand::CharRightRectExtend:
	case EditorCommand::HomeRectExtend:
	case EditorCommand::VCHomeRectExtend:
	case EditorCommand::LineEndRectExtend:
		return true;
	default:
		if (isRectMoveExtends) {
			// Handle SetSelectionMode(SelectionMode::Rectangle) and subsequent movements.
			switch (command) {
			case EditorCommand::CharLeftExtend:
			case EditorCommand::CharRightExtend:
			case EditorCommand::HomeExtend:
			case EditorCommand::VCHomeExtend:
			case EditorCommand::LineEndExtend:
				return true;
			default:
				return false;
			}
		}
		return false;
	}
}

}

Sci::Position Editor::HomeWrapPosition(Sci::Position position) {
	const Sci::Position viewLineStart = StartEndDisplayLine(position, true);
	const Sci::Position homePos = MovePositionSoVisible(viewLineStart, -1).Position();
	if (position <= homePos)
		return pdoc->LineStartPosition(position);
	return homePos;
}

Sci::Position Editor::VCHomeDisplayPosition(Sci::Position position) {
	const Sci::Position homePos = pdoc->VCHomePosition(position);
	const Sci::Position viewLineStart = StartEndDisplayLine(position, true);
	if (viewLineStart > homePos) {
		return viewLineStart;
	}
	return homePos;
}

Sci::Position Editor::VCHomeWrapPosition(Sci::Position position) {
	const Sci::Position homePos = pdoc->VCHomePosition(position);
	const Sci::Position viewLineStart = StartEndDisplayLine(position, true);
	if ((viewLineStart < position) && (viewLineStart > homePos)) {
		return viewLineStart;
	}
	return homePos;
}

Sci::Position Editor::LineEndWrapPosition(Sci::Position position) {
	const Sci::Position endPos = StartEndDisplayLine(position, false);
	const Sci::Position realEndPos = pdoc->LineEndPosition(position);
	if (endPos > realEndPos      // if moved past visible EOLs
		|| position >= endPos) // if at end of display line already
		return realEndPos;
	return endPos;
}

SelectionPosition Editor::PositionMove(EditorCommand command, SelectionPosition spCaret) {
	switch (command) {
	case EditorCommand::CharLeft:
	case EditorCommand::CharLeftExtend:
		if (spCaret.VirtualSpace()) {
			spCaret.AddVirtualSpace(-1);
		} else if (!FlagSet(virtualSpaceOptions, VirtualSpace::NoWrapLineStart) || pdoc->GetColumn(spCaret.Position()) > 0) {
			spCaret.Add(-1);
		}
		return spCaret;
	case EditorCommand::CharRight:
	case EditorCommand::CharRightExtend:
		if (FlagSet(virtualSpaceOptions, VirtualSpace::UserAccessible) && pdoc->IsLineEndPosition(spCaret.Position())) {
			spCaret.AddVirtualSpace(1);
		} else {
			spCaret.Add(1);
		}
		return spCaret;
	case EditorCommand::WordLeft:
	case EditorCommand::WordLeftExtend:
		return SelectionPosition(pdoc->NextWordStart(spCaret.Position(), -1));
	case EditorCommand::WordRight:
	case EditorCommand::WordRightExtend:
		return SelectionPosition(pdoc->NextWordStart(spCaret.Position(), 1));
	case EditorCommand::WordLeftEnd:
	case EditorCommand::WordLeftEndExtend:
		return SelectionPosition(pdoc->NextWordEnd(spCaret.Position(), -1));
	case EditorCommand::WordRightEnd:
	case EditorCommand::WordRightEndExtend:
		return SelectionPosition(pdoc->NextWordEnd(spCaret.Position(), 1));
	case EditorCommand::WordPartLeft:
	case EditorCommand::WordPartLeftExtend:
		return SelectionPosition(pdoc->WordPartLeft(spCaret.Position()));
	case EditorCommand::WordPartRight:
	case EditorCommand::WordPartRightExtend:
		return SelectionPosition(pdoc->WordPartRight(spCaret.Position()));
	case EditorCommand::Home:
	case EditorCommand::HomeExtend:
		return SelectionPosition(pdoc->LineStartPosition(spCaret.Position()));
	case EditorCommand::HomeDisplay:
	case EditorCommand::HomeDisplayExtend:
		return SelectionPosition(StartEndDisplayLine(spCaret.Position(), true));
	case EditorCommand::HomeWrap:
	case EditorCommand::HomeWrapExtend:
		return SelectionPosition(HomeWrapPosition(spCaret.Position()));
	case EditorCommand::VCHome:
	case EditorCommand::VCHomeExtend:
		// VCHome alternates between beginning of line and beginning of text so may move back or forwards
		return SelectionPosition(pdoc->VCHomePosition(spCaret.Position()));
	case EditorCommand::VCHomeDisplay:
	case EditorCommand::VCHomeDisplayExtend:
		return SelectionPosition(VCHomeDisplayPosition(spCaret.Position()));
	case EditorCommand::VCHomeWrap:
	case EditorCommand::VCHomeWrapExtend:
		return SelectionPosition(VCHomeWrapPosition(spCaret.Position()));
	case EditorCommand::LineEnd:
	case EditorCommand::LineEndExtend:
		return SelectionPosition(pdoc->LineEndPosition(spCaret.Position()));
	case EditorCommand::LineEndDisplay:
	case EditorCommand::LineEndDisplayExtend:
		return SelectionPosition(StartEndDisplayLine(spCaret.Position(), false));
	case EditorCommand::LineEndWrap:
	case EditorCommand::LineEndWrapExtend:
		return SelectionPosition(LineEndWrapPosition(spCaret.Position()));

	default:
		break;
	}
	// Above switch should be exhaustive so this will never be reached.
	PLATFORM_ASSERT(false);
	return spCaret;
}

SelectionRange Editor::SelectionMove(EditorCommand command, size_t r) {
	const SelectionPosition spCaretStart = sel.Range(r).caret;
	const SelectionPosition spCaretMoved = PositionMove(command, spCaretStart);

	const int directionMove = (spCaretMoved < spCaretStart) ? -1 : 1;
	const SelectionPosition spCaret = MovePositionSoVisible(spCaretMoved, directionMove);

	// Handle move versus extend, and special behaviour for non-empty left/right
	switch (command) {
	case EditorCommand::CharLeft:
	case EditorCommand::CharRight:
		if (sel.Range(r).Empty()) {
			return SelectionRange(spCaret);
		}
		if (command == EditorCommand::CharLeft) {
			return SelectionRange(sel.Range(r).Start());
		}
		return SelectionRange(sel.Range(r).End());

	case EditorCommand::WordLeft:
	case EditorCommand::WordRight:
	case EditorCommand::WordLeftEnd:
	case EditorCommand::WordRightEnd:
	case EditorCommand::WordPartLeft:
	case EditorCommand::WordPartRight:
	case EditorCommand::Home:
	case EditorCommand::HomeDisplay:
	case EditorCommand::HomeWrap:
	case EditorCommand::VCHome:
	case EditorCommand::VCHomeDisplay:
	case EditorCommand::VCHomeWrap:
	case EditorCommand::LineEnd:
	case EditorCommand::LineEndDisplay:
	case EditorCommand::LineEndWrap:
		return SelectionRange(spCaret);

	default:
		break;
	}

	// All remaining cases are *Extend
	const SelectionRange rangeNew = SelectionRange(spCaret, sel.Range(r).anchor);
	sel.TrimOtherSelections(r, rangeNew);
	return rangeNew;
}

int Editor::HorizontalMove(EditorCommand command) {
	if (sel.selType == Selection::SelTypes::lines) {
		return 0; // horizontal moves with line selection have no effect
	}
	if (sel.MoveExtends()) {
		command = WithExtends(command);
	}

	if (!multipleSelection && !sel.IsRectangular()) {
		// Simplify selection down to 1
		sel.SetSelection(sel.RangeMain());
	}

	// Invalidate each of the current selections
	InvalidateWholeSelection();

	if (IsRectExtend(command, sel.IsRectangular() && sel.MoveExtends())) {
		const SelectionRange rangeBase = sel.IsRectangular() ? sel.Rectangular() : sel.RangeMain();
		if (!sel.IsRectangular()) {
			sel.DropAdditionalRanges();
		}
		// Will change to rectangular if not currently rectangular
		SelectionPosition spCaret = rangeBase.caret;
		switch (command) {
		case EditorCommand::CharLeftRectExtend:
		case EditorCommand::CharLeftExtend: // only when sel.IsRectangular() && sel.MoveExtends()
			if (pdoc->IsLineEndPosition(spCaret.Position()) && spCaret.VirtualSpace()) {
				spCaret.SetVirtualSpace(spCaret.VirtualSpace() - 1);
			} else if (!FlagSet(virtualSpaceOptions, VirtualSpace::NoWrapLineStart) || pdoc->GetColumn(spCaret.Position()) > 0) {
				spCaret = SelectionPosition(spCaret.Position() - 1);
			}
			break;
		case EditorCommand::CharRightRectExtend:
		case EditorCommand::CharRightExtend: // only when sel.IsRectangular() && sel.MoveExtends()
			if (FlagSet(virtualSpaceOptions, VirtualSpace::RectangularSelection) && pdoc->IsLineEndPosition(sel.MainCaret())) {
				spCaret.SetVirtualSpace(spCaret.VirtualSpace() + 1);
			} else {
				spCaret = SelectionPosition(spCaret.Position() + 1);
			}
			break;
		case EditorCommand::HomeRectExtend:
		case EditorCommand::HomeExtend: // only when sel.IsRectangular() && sel.MoveExtends()
			spCaret = SelectionPosition(pdoc->LineStartPosition(spCaret.Position()));
			break;
		case EditorCommand::VCHomeRectExtend:
		case EditorCommand::VCHomeExtend: // only when sel.IsRectangular() && sel.MoveExtends()
			spCaret = SelectionPosition(pdoc->VCHomePosition(spCaret.Position()));
			break;
		case EditorCommand::LineEndRectExtend:
		case EditorCommand::LineEndExtend: // only when sel.IsRectangular() && sel.MoveExtends()
			spCaret = SelectionPosition(pdoc->LineEndPosition(spCaret.Position()));
			break;
		default:
			break;
		}
		const int directionMove = (spCaret < rangeBase.caret) ? -1 : 1;
		spCaret = MovePositionSoVisible(spCaret, directionMove);
		sel.selType = Selection::SelTypes::rectangle;
		sel.Rectangular() = SelectionRange(spCaret, rangeBase.anchor);
		SetRectangularRange();
	} else if (sel.IsRectangular()) {
		// Not a rectangular extension so switch to stream.
		SelectionPosition selAtLimit = (NaturalDirection(command) > 0) ? sel.Limits().end : sel.Limits().start;
		switch (command) {
		case EditorCommand::Home:
			selAtLimit = SelectionPosition(pdoc->LineStartPosition(selAtLimit.Position()));
			break;
		case EditorCommand::VCHome:
			selAtLimit = SelectionPosition(pdoc->VCHomePosition(selAtLimit.Position()));
			break;
		case EditorCommand::LineEnd:
			selAtLimit = SelectionPosition(pdoc->LineEndPosition(selAtLimit.Position()));
			break;
		default:
			break;
		}
		sel.selType = Selection::SelTypes::stream;
		sel.SetSelection(SelectionRange(selAtLimit));
	} else {
		if (!additionalSelectionTyping) {
			InvalidateWholeSelection();
			sel.DropAdditionalRanges();
		}
		for (size_t r = 0; r < sel.Count(); r++) {
			sel.Range(r) = SelectionMove(command, r);
		}
	}

	sel.RemoveDuplicates();

	MovedCaret(sel.RangeMain().caret, SelectionPosition(Sci::invalidPosition), true, caretPolicies);

	// Invalidate the new state of the selection
	InvalidateWholeSelection();

	SetLastXChosen();
	// Need the line moving and so forth from MovePositionTo
	return 0;
}

int Editor::DelWordOrLine(EditorCommand command) {
	// Virtual space may be realised for EditorCommand::DelWordRight or EditorCommand::DelWordRightEnd
	// which means 2 actions so wrap in an undo group.

	// Rightwards and leftwards deletions differ in treatment of virtual space.
	// Clear virtual space for leftwards, realise for rightwards.
	const bool leftwards = AnyOf(command, EditorCommand::DelWordLeft, EditorCommand::DelLineLeft);

	if (!additionalSelectionTyping) {
		InvalidateWholeSelection();
		sel.DropAdditionalRanges();
	}

	UndoGroup ug0(pdoc, (sel.Count() > 1) || !leftwards);

	for (size_t r = 0; r < sel.Count(); r++) {
		if (leftwards) {
			// Delete to the left so first clear the virtual space.
			sel.Range(r).ClearVirtualSpace();
		} else {
			// Delete to the right so first realise the virtual space.
			sel.Range(r) = SelectionRange(
				RealizeVirtualSpace(sel.Range(r).caret));
		}

		Range rangeDelete;
		switch (command) {
		case EditorCommand::DelWordLeft:
			rangeDelete = Range(
				pdoc->NextWordStart(sel.Range(r).caret.Position(), -1),
				sel.Range(r).caret.Position());
			break;
		case EditorCommand::DelWordRight:
			rangeDelete = Range(
				sel.Range(r).caret.Position(),
				pdoc->NextWordStart(sel.Range(r).caret.Position(), 1));
			break;
		case EditorCommand::DelWordRightEnd:
			rangeDelete = Range(
				sel.Range(r).caret.Position(),
				pdoc->NextWordEnd(sel.Range(r).caret.Position(), 1));
			break;
		case EditorCommand::DelLineLeft:
			rangeDelete = Range(
				pdoc->LineStartPosition(sel.Range(r).caret.Position()),
				sel.Range(r).caret.Position());
			break;
		case EditorCommand::DelLineRight:
			rangeDelete = Range(
				sel.Range(r).caret.Position(),
				pdoc->LineEndPosition(sel.Range(r).caret.Position()));
			break;
		default:
			break;
		}
		if (!RangeContainsProtected(rangeDelete.start, rangeDelete.end)) {
			pdoc->DeleteChars(rangeDelete.start, rangeDelete.end - rangeDelete.start);
		}
	}

	// May need something stronger here: can selections overlap at this point?
	sel.RemoveDuplicates();

	MovedCaret(sel.RangeMain().caret, SelectionPosition(Sci::invalidPosition), true, caretPolicies);

	// Invalidate the new state of the selection
	InvalidateWholeSelection();

	SetLastXChosen();
	return 0;
}

int Editor::KeyDefault(Keys, KeyMod) {
	return 0;
}

int Editor::KeyDownWithModifiers(Keys key, KeyMod modifiers, bool *consumed) {
	DwellEnd(false);
	const EditorCommand command = kmap.Find(key, modifiers);
	if (command != EditorCommand::None) {
		if (consumed)
			*consumed = true;
		// Command capture is at ExecuteCommand as typed RecordedCommand.
		return ExecuteCommand(command);
	}
	if (consumed)
		*consumed = false;
	return KeyDefault(key, modifiers);
}

void Editor::Indent(bool forwards, bool lineIndent) {
	UndoGroup ug(pdoc);
	// Avoid problems with recalculating rectangular range multiple times by temporarily
	// treating rectangular selection as multiple stream selection.
	const Selection::SelTypes selType = sel.selType;
	if (sel.IsRectangular()) {
		sel.selType = Selection::SelTypes::stream;
	}
	for (size_t r=0; r<sel.Count(); r++) {
		const Sci::Line lineOfAnchor =
			pdoc->SciLineFromPosition(sel.Range(r).anchor.Position());
		Sci::Position caretPosition = sel.Range(r).caret.Position();
		const Sci::Line lineCurrentPos = pdoc->SciLineFromPosition(caretPosition);
		if (lineOfAnchor == lineCurrentPos && !lineIndent) {
			const int indentationStep = pdoc->IndentSize();
			if (forwards) {
				pdoc->DeleteChars(sel.Range(r).Start().Position(), sel.Range(r).Length());
				caretPosition = sel.Range(r).caret.Position();
				const int indentation = pdoc->GetLineIndentation(lineCurrentPos);
				const Sci::Position column = pdoc->GetColumn(caretPosition);
				if (column <= indentation && pdoc->tabIndents) {
					// Inside initial whitespace
					const Sci::Position posSelect = pdoc->SetLineIndentation(
						lineCurrentPos, indentation + indentationStep - (indentation % indentationStep));
					sel.Range(r) = SelectionRange(posSelect);
				} else {
					if (pdoc->useTabs) {
						const Sci::Position lengthInserted = pdoc->InsertString(caretPosition, "\t");
						sel.Range(r) = SelectionRange(caretPosition + lengthInserted);
					} else {
						const Sci::Position numSpaces = pdoc->tabInChars - (column % pdoc->tabInChars);
						const std::string spaceText(numSpaces, ' ');
						const Sci::Position lengthInserted = pdoc->InsertString(caretPosition, spaceText);
						sel.Range(r) = SelectionRange(caretPosition + lengthInserted);
					}
				}
			} else {
				const int indentation = pdoc->GetLineIndentation(lineCurrentPos);
				const Sci::Position column = pdoc->GetColumn(caretPosition);
				if (column <= indentation && pdoc->tabIndents) {
					const Sci::Position posSelect = pdoc->SetLineIndentation(lineCurrentPos, indentation - indentationStep);
					sel.Range(r) = SelectionRange(posSelect);
				} else {
					const Sci::Position newColumn = std::max<Sci::Position>(0,
						((column - 1) / pdoc->tabInChars) * pdoc->tabInChars);
					Sci::Position newPos = caretPosition;
					while (pdoc->GetColumn(newPos) > newColumn)
						newPos--;
					sel.Range(r) = SelectionRange(newPos);
				}
			}
		} else {	// Multiline or LineIndent
			const Sci::Position anchorPosOnLine = sel.Range(r).anchor.Position() -
				pdoc->LineStart(lineOfAnchor);
			const Sci::Position currentPosPosOnLine = caretPosition -
				pdoc->LineStart(lineCurrentPos);
			// Multiple lines selected so indent / dedent
			const Sci::Line lineTopSel = std::min(lineOfAnchor, lineCurrentPos);
			Sci::Line lineBottomSel = std::max(lineOfAnchor, lineCurrentPos);
			if (pdoc->LineStart(lineBottomSel) == sel.Range(r).anchor.Position() || pdoc->LineStart(lineBottomSel) == caretPosition)
				lineBottomSel--;  	// If not selecting any characters on a line, do not indent
			pdoc->Indent(forwards, lineBottomSel, lineTopSel);
			if (lineOfAnchor < lineCurrentPos) {
				if (currentPosPosOnLine == 0)
					sel.Range(r) = SelectionRange(pdoc->LineStart(lineCurrentPos),
						pdoc->LineStart(lineOfAnchor));
				else
					sel.Range(r) = SelectionRange(pdoc->LineStart(lineCurrentPos + 1),
						pdoc->LineStart(lineOfAnchor));
			} else {
				if (anchorPosOnLine == 0)
					sel.Range(r) = SelectionRange(pdoc->LineStart(lineCurrentPos),
						pdoc->LineStart(lineOfAnchor));
				else
					sel.Range(r) = SelectionRange(pdoc->LineStart(lineCurrentPos),
						pdoc->LineStart(lineOfAnchor + 1));
			}
		}
	}
	sel.selType = selType;	// Restore rectangular mode
	ThinRectangularRange();
	ContainerNeedsUpdate(Update::Selection);
}

/// UTF-8 Unicode case mapping (not locale-sensitive). Invalid UTF-8 bytes are left unchanged.
std::string Editor::CaseMapString(const std::string &s, CaseMapping caseMapping) {
	if (s.empty() || caseMapping == CaseMapping::same) {
		return s;
	}
	const CaseConversion conversion = (caseMapping == CaseMapping::upper)
		? CaseConversion::upper
		: CaseConversion::lower;
	return CaseConvertString(s, conversion);
}

namespace {

bool Close(Point pt1, Point pt2, Point threshold) noexcept {
	const Point ptDifference = pt2 - pt1;
	if (std::abs(ptDifference.x) > threshold.x)
		return false;
	if (std::abs(ptDifference.y) > threshold.y)
		return false;
	return true;
}

constexpr bool AllowVirtualSpace(VirtualSpace virtualSpaceOptions, bool rectangular) noexcept {
	return FlagSet(virtualSpaceOptions, (rectangular ? VirtualSpace::RectangularSelection : VirtualSpace::UserAccessible));
}

}

std::string Editor::RangeText(Sci::Position start, Sci::Position end) const {
	if (start < end) {
		const Sci::Position len = end - start;
		std::string ret(len, '\0');
		pdoc->GetCharRange(ret.data(), start, len);
		return ret;
	}
	return {};
}

bool Editor::CopyLineRange(SelectionText *ss, bool allowProtected) {
	const Sci::Line currentLine = pdoc->SciLineFromPosition(sel.MainCaret());
	const Sci::Position start = pdoc->LineStart(currentLine);
	const Sci::Position end = pdoc->LineEnd(currentLine);

	if (allowProtected || !RangeContainsProtected(start, end)) {
		std::string text = RangeText(start, end);
		text.append(pdoc->EOLString());
		ss->Copy(text, false, true);
		return true;
	}
	return false;
}

void Editor::SetDragPosition(SelectionPosition newPos) {
	if (newPos.Position() >= 0) {
		newPos = MovePositionOutsideChar(newPos, 1);
		posDrop = newPos;
	}
	if (!(posDrag == newPos)) {
		const CaretPolicies dragCaretPolicies = {
			CaretPolicySlop(CaretPolicy::Slop | CaretPolicy::Strict | CaretPolicy::Even, 50),
			CaretPolicySlop(CaretPolicy::Slop | CaretPolicy::Strict | CaretPolicy::Even, 2)
		};
		MovedCaret(newPos, posDrag, true, dragCaretPolicies);

		caret.on = true;
		FineTickerCancel(TickReason::caret);
		if ((caret.active) && (caret.period > 0) && (newPos.Position() < 0))
			FineTickerStart(TickReason::caret, caret.period, caret.period/10);
		InvalidateCaret();
		posDrag = newPos;
		InvalidateCaret();
	}
}

void Editor::DisplayCursor(Window::Cursor c) {
	if (cursorMode == CursorShape::Normal)
		wMain.SetCursor(c);
	else
		wMain.SetCursor(static_cast<Window::Cursor>(cursorMode));
}

bool Editor::DragThreshold(Point ptStart, Point ptNow) {
	const Point ptDiff = ptStart - ptNow;
	const XYPOSITION distanceSquared = ptDiff.x * ptDiff.x + ptDiff.y * ptDiff.y;
	return distanceSquared > 16.0f;
}

void Editor::StartDrag() {
	// Always handled by subclasses
}

void Editor::DropAt(SelectionPosition position, std::string_view value, bool moving, bool rectangular) {
	//Platform::DebugPrintf("DropAt %d %d\n", inDragDrop, position);
	if (inDragDrop == DragDrop::dragging)
		dropWentOutside = false;

	const bool positionWasInSelection = PositionInSelection(position.Position());

	const bool positionOnEdgeOfSelection =
	    (position == SelectionStart()) || (position == SelectionEnd());

	if ((inDragDrop != DragDrop::dragging) || !(positionWasInSelection) ||
	        (positionOnEdgeOfSelection && !moving)) {

		const SelectionPosition selStart = SelectionStart();
		const SelectionPosition selEnd = SelectionEnd();

		UndoGroup ug(pdoc);

		SelectionPosition positionAfterDeletion = position;
		if ((inDragDrop == DragDrop::dragging) && moving) {
			// Remove dragged out text
			if (rectangular || sel.selType == Selection::SelTypes::lines) {
				for (size_t r=0; r<sel.Count(); r++) {
					if (position >= sel.Range(r).Start()) {
						if (position > sel.Range(r).End()) {
							positionAfterDeletion.Add(-sel.Range(r).Length());
						} else {
							positionAfterDeletion.Add(-SelectionRange(position, sel.Range(r).Start()).Length());
						}
					}
				}
			} else {
				if (position > selStart) {
					positionAfterDeletion.Add(-SelectionRange(selEnd, selStart).Length());
				}
			}
			ClearSelection();
		}
		position = positionAfterDeletion;

		std::string convertedText = Document::TransformLineEnds(value, pdoc->eolMode);

		if (rectangular) {
			PasteRectangular(position, convertedText);
			// Should try to select new rectangle but it may not be a rectangle now so just select the drop position
			SetEmptySelection(position);
		} else {
			position = MovePositionOutsideChar(position, sel.MainCaret() - position.Position());
			position = RealizeVirtualSpace(position);
			const Sci::Position lengthInserted = pdoc->InsertString(
				position.Position(), convertedText);
			if (lengthInserted > 0) {
				SelectionPosition posAfterInsertion = position;
				posAfterInsertion.Add(lengthInserted);
				SetSelection(posAfterInsertion, position);
			}
		}
	} else if (inDragDrop == DragDrop::dragging) {
		SetEmptySelection(position);
	}
}

void Editor::DropAt(SelectionPosition position, const char *value, size_t lengthValue, bool moving, bool rectangular) {
	DropAt(position, std::string_view(value, lengthValue), moving, rectangular);
}

void Editor::DropAt(SelectionPosition position, const char *value, bool moving, bool rectangular) {
	DropAt(position, std::string_view(value), moving, rectangular);
}

/**
 * @return true if given position is inside the selection,
 */
bool Editor::PositionInSelection(Sci::Position pos) {
	pos = MovePositionOutsideChar(pos, sel.MainCaret() - pos);
	for (size_t r=0; r<sel.Count(); r++) {
		if (sel.Range(r).Contains(pos))
			return true;
	}
	return false;
}

bool Editor::PointInSelection(Point pt) {
	const SelectionPosition pos = SPositionFromLocation(pt, false, true);
	const Point ptPos = LocationFromPosition(pos);
	for (size_t r=0; r<sel.Count(); r++) {
		const SelectionRange &range = sel.Range(r);
		if (range.Contains(pos)) {
			bool hit = true;
			if (pos == range.Start()) {
				// see if just before selection
				if (pt.x < ptPos.x) {
					hit = false;
				}
			}
			if (pos == range.End()) {
				// see if just after selection
				if (pt.x > ptPos.x) {
					hit = false;
				}
			}
			if (hit)
				return true;
		}
	}
	return false;
}

bool Editor::PointInSelMargin(Point pt) const {
	// Really means: "Point in a margin"
	if (vs.fixedColumnWidth > 0) {	// There is a margin
		PRectangle rcSelMargin = GetClientRectangle();
		rcSelMargin.right = static_cast<XYPOSITION>(vs.textStart - vs.leftMarginWidth);
		rcSelMargin.left = static_cast<XYPOSITION>(vs.textStart - vs.fixedColumnWidth);
		const Point ptOrigin = GetVisibleOriginInMain();
		rcSelMargin.Move(0, -ptOrigin.y);
		return rcSelMargin.ContainsWholePixel(pt);
	}
	return false;
}

Window::Cursor Editor::GetMarginCursor(Point pt) const noexcept {
	int x = 0;
	for (const MarginStyle &m : vs.ms) {
		if ((pt.x >= x) && (pt.x < x + m.width))
			return static_cast<Window::Cursor>(m.cursor);
		x += m.width;
	}
	return Window::Cursor::reverseArrow;
}

void Editor::TrimAndSetSelection(Sci::Position currentPos_, Sci::Position anchor_) {
	sel.TrimSelection(SelectionRange(currentPos_, anchor_));
	SetSelection(currentPos_, anchor_);
}

void Editor::LineSelection(Sci::Position lineCurrentPos_, Sci::Position lineAnchorPos_, bool wholeLine) {
	Sci::Position selCurrentPos;
	Sci::Position selAnchorPos;
	if (wholeLine) {
		const Sci::Line lineCurrent_ = pdoc->SciLineFromPosition(lineCurrentPos_);
		const Sci::Line lineAnchor_ = pdoc->SciLineFromPosition(lineAnchorPos_);
		if (lineAnchorPos_ < lineCurrentPos_) {
			selCurrentPos = pdoc->LineStart(lineCurrent_ + 1);
			selAnchorPos = pdoc->LineStart(lineAnchor_);
		} else if (lineAnchorPos_ > lineCurrentPos_) {
			selCurrentPos = pdoc->LineStart(lineCurrent_);
			selAnchorPos = pdoc->LineStart(lineAnchor_ + 1);
		} else { // Same line, select it
			selCurrentPos = pdoc->LineStart(lineAnchor_ + 1);
			selAnchorPos = pdoc->LineStart(lineAnchor_);
		}
	} else {
		if (lineAnchorPos_ < lineCurrentPos_) {
			selCurrentPos = StartEndDisplayLine(lineCurrentPos_, false) + 1;
			selCurrentPos = pdoc->MovePositionOutsideChar(selCurrentPos, 1);
			selAnchorPos = StartEndDisplayLine(lineAnchorPos_, true);
		} else if (lineAnchorPos_ > lineCurrentPos_) {
			selCurrentPos = StartEndDisplayLine(lineCurrentPos_, true);
			selAnchorPos = StartEndDisplayLine(lineAnchorPos_, false) + 1;
			selAnchorPos = pdoc->MovePositionOutsideChar(selAnchorPos, 1);
		} else { // Same line, select it
			selCurrentPos = StartEndDisplayLine(lineAnchorPos_, false) + 1;
			selCurrentPos = pdoc->MovePositionOutsideChar(selCurrentPos, 1);
			selAnchorPos = StartEndDisplayLine(lineAnchorPos_, true);
		}
	}
	TrimAndSetSelection(selCurrentPos, selAnchorPos);
}

void Editor::WordSelection(Sci::Position pos) {
	if (pos < wordSelectAnchorStartPos) {
		// Extend backward to the word containing pos.
		// Skip ExtendWordSelect if the line is empty or if pos is after the last character.
		// This ensures that a series of empty lines isn't counted as a single "word".
		if (!pdoc->IsLineEndPosition(pos))
			pos = pdoc->ExtendWordSelect(pdoc->MovePositionOutsideChar(pos + 1, 1), -1);
		TrimAndSetSelection(pos, wordSelectAnchorEndPos);
	} else if (pos > wordSelectAnchorEndPos) {
		// Extend forward to the word containing the character to the left of pos.
		// Skip ExtendWordSelect if the line is empty or if pos is the first position on the line.
		// This ensures that a series of empty lines isn't counted as a single "word".
		if (pos > pdoc->LineStartPosition(pos))
			pos = pdoc->ExtendWordSelect(pdoc->MovePositionOutsideChar(pos - 1, -1), 1);
		TrimAndSetSelection(pos, wordSelectAnchorStartPos);
	} else {
		// Select only the anchored word
		if (pos >= originalAnchorPos)
			TrimAndSetSelection(wordSelectAnchorEndPos, wordSelectAnchorStartPos);
		else
			TrimAndSetSelection(wordSelectAnchorStartPos, wordSelectAnchorEndPos);
	}
}

void Editor::DwellEnd(bool mouseMoved) {
	if (mouseMoved)
		ticksToDwell = dwellDelay;
	else
		ticksToDwell = TimeForever;
	if (dwelling && (dwellDelay < TimeForever)) {
		dwelling = false;
		NotifyDwelling(ptMouseLast, dwelling);
	}
	FineTickerCancel(TickReason::dwell);
}

void Editor::MouseLeave() {
	SetHotSpotRange(nullptr);
	SetHoverIndicatorPosition(Sci::invalidPosition);
	if (!HaveMouseCapture()) {
		ptMouseLast = Point(-1, -1);
		DwellEnd(true);
	}
}

void Editor::ButtonDownWithModifiers(Point pt, unsigned int curTime, KeyMod modifiers) {
	SetHoverIndicatorPoint(pt);
	//Platform::DebugPrintf("ButtonDown %d %d = %d alt=%d %d\n", curTime, lastClickTime, curTime - lastClickTime, alt, inDragDrop);
	ptMouseLast = pt;
	const bool ctrl = FlagSet(modifiers, KeyMod::Ctrl);
	const bool shift = FlagSet(modifiers, KeyMod::Shift);
	const bool rectangularModifier = FlagSet(modifiers, rectangularSelectionModifier);
	const SelectionPosition clickPos = SPositionFromLocation(pt, false, false,
		AllowVirtualSpace(virtualSpaceOptions, rectangularModifier));
	const SelectionPosition newPos = MovePositionOutsideChar(clickPos, sel.MainCaret() - clickPos.Position());
	const SelectionPosition newCharPos = MovePositionOutsideChar(
		SPositionFromLocation(pt, false, true, false), -1);
	inDragDrop = DragDrop::none;
	sel.SetMoveExtends(false);

	if (NotifyMarginClick(pt, modifiers))
		return;

	NotifyIndicatorClick(true, newPos.Position(), modifiers);

	const bool multiClick = (curTime < (lastClickTime + Platform::DoubleClickTime())) && Close(pt, lastClick, doubleClickCloseThreshold);
	lastClickTime = curTime;
	lastClick = pt;

	const bool inSelMargin = PointInSelMargin(pt);
	// In margin ctrl+(double)click should always select everything
	if (ctrl && inSelMargin) {
		SelectAll();
		return;
	}
	if (shift && !inSelMargin) {
		SetSelection(newPos);
	}
	if (multiClick) {
		//Platform::DebugPrintf("Double click %d %d = %d\n", curTime, lastClickTime, curTime - lastClickTime);
		ChangeMouseCapture(true);
		if (!ctrl || !multipleSelection || (selectionUnit != TextUnit::character && selectionUnit != TextUnit::word))
			SetEmptySelection(newPos.Position());
		bool doubleClick = false;
		if (inSelMargin) {
			// Inside margin selection type should be either subLine or wholeLine.
			if (selectionUnit == TextUnit::subLine) {
				// If it is subLine, we're inside a *double* click and word wrap is enabled,
				// so we switch to wholeLine in order to select whole line.
				selectionUnit = TextUnit::wholeLine;
			} else if (selectionUnit != TextUnit::subLine && selectionUnit != TextUnit::wholeLine) {
				// If it is neither, reset selection type to line selection.
				selectionUnit = (Wrapping() && (FlagSet(marginOptions, MarginOption::SubLineSelect))) ? TextUnit::subLine : TextUnit::wholeLine;
			}
		} else {
			if (selectionUnit == TextUnit::character) {
				selectionUnit = TextUnit::word;
				doubleClick = true;
			} else if (selectionUnit == TextUnit::word) {
				// Since we ended up here, we're inside a *triple* click, which should always select
				// whole line regardless of word wrap being enabled or not.
				selectionUnit = TextUnit::wholeLine;
			} else {
				selectionUnit = TextUnit::character;
				originalAnchorPos = sel.MainCaret();
			}
		}

		if (selectionUnit == TextUnit::word) {
			Sci::Position charPos = originalAnchorPos;
			if (sel.MainCaret() == originalAnchorPos) {
				charPos = PositionFromLocation(pt, false, true);
				charPos = MovePositionOutsideChar(charPos, -1);
			}

			Sci::Position startWord;
			Sci::Position endWord;
			if ((sel.MainCaret() >= originalAnchorPos) && !pdoc->IsLineEndPosition(charPos)) {
				startWord = pdoc->ExtendWordSelect(pdoc->MovePositionOutsideChar(charPos + 1, 1), -1);
				endWord = pdoc->ExtendWordSelect(charPos, 1);
			} else {
				// Selecting backwards, or anchor beyond last character on line. In these cases,
				// we select the word containing the character to the *left* of the anchor.
				if (charPos > pdoc->LineStartPosition(charPos)) {
					startWord = pdoc->ExtendWordSelect(charPos, -1);
					endWord = pdoc->ExtendWordSelect(startWord, 1);
				} else {
					// Anchor at start of line; select nothing to begin with.
					startWord = charPos;
					endWord = charPos;
				}
			}

			wordSelectAnchorStartPos = startWord;
			wordSelectAnchorEndPos = endWord;
			wordSelectInitialCaretPos = sel.MainCaret();
			WordSelection(wordSelectInitialCaretPos);
		} else if (AnyOf(selectionUnit, TextUnit::subLine, TextUnit::wholeLine)) {
			lineAnchorPos = newPos.Position();
			LineSelection(lineAnchorPos, lineAnchorPos, selectionUnit == TextUnit::wholeLine);
			//Platform::DebugPrintf("Triple click: %d - %d\n", anchor, currentPos);
		} else {
			SetEmptySelection(sel.MainCaret());
		}
		//Platform::DebugPrintf("Double click: %d - %d\n", anchor, currentPos);
		if (doubleClick) {
			NotifyDoubleClick(pt, modifiers);
			if (PositionIsHotspot(newCharPos.Position()))
				NotifyHotSpotDoubleClicked(newCharPos.Position(), modifiers);
		}
	} else {	// Single click
		if (inSelMargin) {
			if (sel.IsRectangular() || (sel.Count() > 1)) {
				InvalidateWholeSelection();
				sel.Clear();
			}
			sel.selType = Selection::SelTypes::stream;
			if (!shift) {
				// Single click in margin: select wholeLine or only subLine if word wrap is enabled
				lineAnchorPos = newPos.Position();
				selectionUnit = (Wrapping() && (FlagSet(marginOptions, MarginOption::SubLineSelect))) ? TextUnit::subLine : TextUnit::wholeLine;
				LineSelection(lineAnchorPos, lineAnchorPos, selectionUnit == TextUnit::wholeLine);
			} else {
				// Single shift+click in margin: select from line anchor to clicked line
				if (sel.MainAnchor() > sel.MainCaret())
					lineAnchorPos = sel.MainAnchor() - 1;
				else
					lineAnchorPos = sel.MainAnchor();
				// Reset selection type if there is an empty selection.
				// This ensures that we don't end up stuck in previous selection mode, which is no longer valid.
				// Otherwise, if there's a non empty selection, reset selection type only if it differs from selSubLine and selWholeLine.
				// This ensures that we continue selecting in the same selection mode.
				if (sel.Empty() || (selectionUnit != TextUnit::subLine && selectionUnit != TextUnit::wholeLine))
					selectionUnit = (Wrapping() && (FlagSet(marginOptions, MarginOption::SubLineSelect))) ? TextUnit::subLine : TextUnit::wholeLine;
				LineSelection(newPos.Position(), lineAnchorPos, selectionUnit == TextUnit::wholeLine);
			}

			SetDragPosition(SelectionPosition(Sci::invalidPosition));
			ChangeMouseCapture(true);
		} else {
			if (PointIsHotspot(pt)) {
				NotifyHotSpotClicked(newCharPos.Position(), modifiers);
				hotSpotClickPos = newCharPos.Position();
			}
			if (!shift) {
				const ptrdiff_t selectionPart = SelectionFromPoint(pt);
				if (selectionPart >= 0) {
					if (multipleSelection && ctrl) {
						// Deselect
						if (sel.Count() > 1) {
							DropSelection(selectionPart);
							// Completed: don't want any more processing of this click
							return;
						}
						// Switch to just the click position
						SetSelection(newPos, newPos);
					}
					if (dragDropEnabled && !sel.Range(selectionPart).Empty()) {
						inDragDrop = DragDrop::initial;
					}
				}
			}
			ChangeMouseCapture(true);
			if (inDragDrop != DragDrop::initial) {
				SetDragPosition(SelectionPosition(Sci::invalidPosition));
				if (!shift) {
					if (ctrl && multipleSelection) {
						const SelectionRange range(newPos);
						sel.TentativeSelection(range);
						InvalidateSelection(range, true);
					} else {
						InvalidateSelection(SelectionRange(newPos), true);
						if (sel.Count() > 1)
							Redraw();
						if ((sel.Count() > 1) || (sel.selType != Selection::SelTypes::stream))
							sel.Clear();
						sel.selType = rectangularModifier ? Selection::SelTypes::rectangle : Selection::SelTypes::stream;
						SetSelection(newPos, newPos);
					}
				}
				SelectionPosition anchorCurrent = newPos;
				if (shift)
					anchorCurrent = sel.IsRectangular() ?
						sel.Rectangular().anchor : sel.RangeMain().anchor;
				sel.selType = rectangularModifier ? Selection::SelTypes::rectangle : Selection::SelTypes::stream;
				selectionUnit = TextUnit::character;
				originalAnchorPos = sel.MainCaret();
				sel.Rectangular() = SelectionRange(newPos, anchorCurrent);
				SetRectangularRange();
			}
		}
	}
	lastXChosen = static_cast<int>(pt.x) + xOffset;
	ShowCaretAtCurrentPosition();
}

void Editor::RightButtonDownWithModifiers(Point pt, unsigned int, KeyMod modifiers) {
	if (NotifyMarginRightClick(pt, modifiers))
		return;
}

bool Editor::PositionIsHotspot(Sci::Position position) const noexcept {
	return vs.styles[pdoc->StyleIndexAt(position)].hotspot;
}

bool Editor::PointIsHotspot(Point pt) {
	const Sci::Position pos = PositionFromLocation(pt, true, true);
	if (pos == Sci::invalidPosition)
		return false;
	return PositionIsHotspot(pos);
}

void Editor::SetHoverIndicatorPosition(Sci::Position position) {
	const Sci::Position hoverIndicatorPosPrev = hoverIndicatorPos;
	hoverIndicatorPos = Sci::invalidPosition;
	if (!vs.indicatorsDynamic)
		return;
	if (position != Sci::invalidPosition) {
		for (const IDecoration *deco : pdoc->decorations->View()) {
			if (vs.indicators[deco->Indicator()].IsDynamic()) {
				if (pdoc->decorations->ValueAt(deco->Indicator(), position)) {
					hoverIndicatorPos = position;
				}
			}
		}
	}
	if (hoverIndicatorPosPrev != hoverIndicatorPos) {
		Redraw();
	}
}

void Editor::SetHoverIndicatorPoint(Point pt) {
	if (!vs.indicatorsDynamic) {
		SetHoverIndicatorPosition(Sci::invalidPosition);
	} else {
		SetHoverIndicatorPosition(PositionFromLocation(pt, true, true));
	}
}

void Editor::SetHotSpotRange(const Point *pt) {
	if (pt) {
		const Sci::Position pos = PositionFromLocation(*pt, false, true);

		// If we don't limit this to word characters then the
		// range can encompass more than the run range and then
		// the underline will not be drawn properly.
		Range hsNew;
		hsNew.start = pdoc->ExtendStyleRange(pos, -1, hotspotSingleLine);
		hsNew.end = pdoc->ExtendStyleRange(pos, 1, hotspotSingleLine);

		// Only invalidate the range if the hotspot range has changed...
		if (!(hsNew == hotspot)) {
			if (hotspot.Valid()) {
				InvalidateRange(hotspot.start, hotspot.end);
			}
			hotspot = hsNew;
			InvalidateRange(hotspot.start, hotspot.end);
		}
	} else {
		if (hotspot.Valid()) {
			InvalidateRange(hotspot.start, hotspot.end);
		}
		hotspot = Range(Sci::invalidPosition);
	}
}

void Editor::ButtonMoveWithModifiers(Point pt, unsigned int, KeyMod modifiers) {
	if (ptMouseLast != pt) {
		DwellEnd(true);
	}

	SelectionPosition movePos = SPositionFromLocation(pt, false, false,
		AllowVirtualSpace(virtualSpaceOptions, sel.IsRectangular()));
	movePos = MovePositionOutsideChar(movePos, sel.MainCaret() - movePos.Position());

	if (dragDropEnabled && inDragDrop == DragDrop::initial) {
		if (DragThreshold(ptMouseLast, pt)) {
			ChangeMouseCapture(false);
			SetDragPosition(movePos);
			CopySelectionRange(&drag);
			StartDrag();
		}
		return;
	}

	ptMouseLast = pt;
	PRectangle rcClient = GetClientRectangle();
	const Point ptOrigin = GetVisibleOriginInMain();
	rcClient.Move(0, -ptOrigin.y);
	if ((dwellDelay < TimeForever) && rcClient.Contains(pt)) {
		FineTickerStart(TickReason::dwell, dwellDelay, dwellDelay/10);
	}
	//Platform::DebugPrintf("Move %d %d\n", pt.x, pt.y);
	if (HaveMouseCapture()) {

		// Slow down autoscrolling/selection
		autoScrollTimer.ticksToWait -= timer.tickSize;
		if (autoScrollTimer.ticksToWait > 0)
			return;
		autoScrollTimer.ticksToWait = autoScrollDelay;

		// Adjust selection
		if (posDrag.IsValid()) {
			SetDragPosition(movePos);
		} else {
			if (selectionUnit == TextUnit::character) {
				if (sel.selType == Selection::SelTypes::stream
					&& FlagSet(modifiers, rectangularSelectionModifier)
					&& mouseSelectionRectangularSwitch) {
					sel.selType = Selection::SelTypes::rectangle;
				}
				if (sel.IsRectangular()) {
					sel.Rectangular() = SelectionRange(movePos, sel.Rectangular().anchor);
					SetSelection(movePos, sel.RangeMain().anchor);
				} else if (sel.Count() > 1) {
					InvalidateSelection(sel.RangeMain(), false);
					const SelectionRange range(movePos, sel.RangeMain().anchor);
					sel.TentativeSelection(range);
					InvalidateSelection(range, true);
				} else {
					SetSelection(movePos, sel.RangeMain().anchor);
				}
			} else if (selectionUnit == TextUnit::word) {
				// Continue selecting by word
				if (movePos.Position() == wordSelectInitialCaretPos) {  // Didn't move
					// No need to do anything. Previously this case was lumped
					// in with "Moved forward", but that can be harmful in this
					// case: a handler for the NotifyDoubleClick re-adjusts
					// the selection for a fancier definition of "word" (for
					// example, in Perl it is useful to include the leading
					// '$', '%' or '@' on variables for word selection). In this
					// the ButtonMove() called via TickFor() for auto-scrolling
					// could result in the fancier word selection adjustment
					// being unmade.
				} else {
					wordSelectInitialCaretPos = -1;
					WordSelection(movePos.Position());
				}
			} else {
				// Continue selecting by line
				LineSelection(movePos.Position(), lineAnchorPos, selectionUnit == TextUnit::wholeLine);
			}
		}

		// Autoscroll
		const Sci::Line lineMove = DisplayFromPosition(movePos.Position());
		if (pt.y >= rcClient.bottom) {
			ScrollTo(lineMove - LinesOnScreen() + 1);
			Redraw();
		} else if (pt.y < rcClient.top) {
			ScrollTo(lineMove);
			Redraw();
		}
		EnsureCaretVisible(false, false, true);

		if (hotspot.Valid() && !PointIsHotspot(pt))
			SetHotSpotRange(nullptr);

		if (hotSpotClickPos != Sci::invalidPosition && PositionFromLocation(pt, true, true) != hotSpotClickPos) {
			if (inDragDrop == DragDrop::none) {
				DisplayCursor(Window::Cursor::text);
			}
			hotSpotClickPos = Sci::invalidPosition;
		}

	} else {
		if (vs.fixedColumnWidth > 0) {	// There is a margin
			if (PointInSelMargin(pt)) {
				DisplayCursor(GetMarginCursor(pt));
				SetHotSpotRange(nullptr);
				SetHoverIndicatorPosition(Sci::invalidPosition);
				return; 	// No need to test for selection
			}
		}
		// Display regular (drag) cursor over selection
		if (dragDropEnabled && PointInSelection(pt) && !SelectionEmpty()) {
			DisplayCursor(Window::Cursor::arrow);
			SetHoverIndicatorPosition(Sci::invalidPosition);
		} else {
			SetHoverIndicatorPoint(pt);
			if (PointIsHotspot(pt)) {
				DisplayCursor(Window::Cursor::hand);
				SetHotSpotRange(&pt);
			} else {
				if (hoverIndicatorPos != Sci::invalidPosition)
					DisplayCursor(Window::Cursor::hand);
				else
					DisplayCursor(Window::Cursor::text);
				SetHotSpotRange(nullptr);
			}
		}
	}
}

void Editor::ButtonUpWithModifiers(Point pt, unsigned int curTime, KeyMod modifiers) {
	//Platform::DebugPrintf("ButtonUp %d %d\n", HaveMouseCapture(), inDragDrop);
	SelectionPosition newPos = SPositionFromLocation(pt, false, false,
		AllowVirtualSpace(virtualSpaceOptions, sel.IsRectangular()));
	if (hoverIndicatorPos != Sci::invalidPosition)
		InvalidateRange(newPos.Position(), newPos.Position() + 1);
	newPos = MovePositionOutsideChar(newPos, sel.MainCaret() - newPos.Position());
	if (inDragDrop == DragDrop::initial) {
		inDragDrop = DragDrop::none;
		SetEmptySelection(newPos);
		selectionUnit = TextUnit::character;
		originalAnchorPos = sel.MainCaret();
	}
	if (hotSpotClickPos != Sci::invalidPosition && PointIsHotspot(pt)) {
		hotSpotClickPos = Sci::invalidPosition;
		SelectionPosition newCharPos = SPositionFromLocation(pt, false, true, false);
		newCharPos = MovePositionOutsideChar(newCharPos, -1);
		NotifyHotSpotReleaseClick(newCharPos.Position(), modifiers & KeyMod::Ctrl);
	}
	if (HaveMouseCapture()) {
		if (PointInSelMargin(pt)) {
			DisplayCursor(GetMarginCursor(pt));
		} else {
			DisplayCursor(Window::Cursor::text);
			SetHotSpotRange(nullptr);
		}
		ptMouseLast = pt;
		ChangeMouseCapture(false);
		NotifyIndicatorClick(false, newPos.Position(), modifiers);
		if (inDragDrop == DragDrop::dragging) {
			const SelectionPosition selStart = SelectionStart();
			const SelectionPosition selEnd = SelectionEnd();
			if (selStart < selEnd) {
				if (drag.Length()) {
					const Sci::Position length = drag.Length();
					if (FlagSet(modifiers, KeyMod::Ctrl)) {
						const Sci::Position lengthInserted = pdoc->InsertString(
							newPos.Position(), drag.Data(), length);
						if (lengthInserted > 0) {
							SetSelection(newPos.Position(), newPos.Position() + lengthInserted);
						}
					} else if (newPos < selStart) {
						pdoc->DeleteChars(selStart.Position(), drag.Length());
						const Sci::Position lengthInserted = pdoc->InsertString(
							newPos.Position(), drag.Data(), length);
						if (lengthInserted > 0) {
							SetSelection(newPos.Position(), newPos.Position() + lengthInserted);
						}
					} else if (newPos > selEnd) {
						pdoc->DeleteChars(selStart.Position(), drag.Length());
						newPos.Add(-static_cast<Sci::Position>(drag.Length()));
						const Sci::Position lengthInserted = pdoc->InsertString(
							newPos.Position(), drag.Data(), length);
						if (lengthInserted > 0) {
							SetSelection(newPos.Position(), newPos.Position() + lengthInserted);
						}
					} else {
						SetEmptySelection(newPos.Position());
					}
					drag.Clear();
				}
				selectionUnit = TextUnit::character;
			}
		} else {
			if (selectionUnit == TextUnit::character) {
				if (sel.Count() > 1) {
					sel.RangeMain() =
						SelectionRange(newPos, sel.Range(sel.Count() - 1).anchor);
					InvalidateWholeSelection();
				} else {
					SetSelection(newPos, sel.RangeMain().anchor);
				}
			}
			sel.CommitTentative();
		}
		SetRectangularRange();
		lastClickTime = curTime;
		lastClick = pt;
		lastXChosen = static_cast<int>(pt.x) + xOffset;
		if (sel.selType == Selection::SelTypes::stream) {
			SetLastXChosen();
		}
		inDragDrop = DragDrop::none;
		EnsureCaretVisible(false);
	}
}

bool Editor::Idle() {
	NotifyUpdateUI();

	bool needWrap = Wrapping() && wrapPending.NeedsWrap();

	if (needWrap) {
		// Wrap lines during idle.
		WrapLines(WrapScope::wsIdle);
		// No more wrapping
		needWrap = wrapPending.NeedsWrap();
	} else if (needIdleStyling) {
		IdleStyle();
	}

	// Add more idle things to do here, but make sure idleDone is
	// set correctly before the function returns. returning
	// false will stop calling this idle function until SetIdle() is
	// called again.

	const bool idleDone = !needWrap && !needIdleStyling; // && thatDone && theOtherThingDone...

	return !idleDone;
}

void Editor::TickFor(TickReason reason) {
	switch (reason) {
		case TickReason::caret:
			caret.on = !caret.on;
			if (caret.active) {
				InvalidateCaret();
			}
			break;
		case TickReason::scroll:
			// Auto scroll
			if (HaveMouseCapture()) {
				ButtonMoveWithModifiers(ptMouseLast, 0, KeyMod::Norm);
			} else {
				// Capture cancelled so cancel timer
				FineTickerCancel(TickReason::scroll);
			}
			break;
		case TickReason::widen:
			SetScrollBars();
			FineTickerCancel(TickReason::widen);
			break;
		case TickReason::dwell:
			if ((!HaveMouseCapture()) &&
				(ptMouseLast.y >= 0)) {
				dwelling = true;
				NotifyDwelling(ptMouseLast, dwelling);
			}
			FineTickerCancel(TickReason::dwell);
			break;
		default:
			// tickPlatform handled by subclass
			break;
	}
}

/// FineTickerStart is be overridden by subclasses that support fine ticking so
/// this method should never be called.
bool Editor::FineTickerRunning(TickReason) {
	assert(false);
	return false;
}

/// FineTickerStart is be overridden by subclasses that support fine ticking so
/// this method should never be called.
void Editor::FineTickerStart(TickReason, int, int) {
	assert(false);
}

/// FineTickerCancel is be overridden by subclasses that support fine ticking so
/// this method should never be called.
void Editor::FineTickerCancel(TickReason) {
	assert(false);
}

void Editor::ChangeMouseCapture(bool on) {
	SetMouseCapture(on);
	// While mouse captured want timer to scroll automatically
	if (on) {
		FineTickerStart(TickReason::scroll, 100, 10);
	} else {
		FineTickerCancel(TickReason::scroll);
	}
}

// SetFocus / HasFocus and other input surface methods: definitions in EditorInput.cxx.

void Editor::UpdateBaseElements() {
	// Overridden by subclasses
}

Sci::Position Editor::PositionAfterArea(PRectangle rcArea) const {
	// The start of the document line after the display line after the area
	// This often means that the line after a modification is restyled which helps
	// detect multiline comment additions and heals single line comments
	const Sci::Line lineAfter = TopLineOfMain() + static_cast<Sci::Line>(rcArea.bottom - 1) / vs.lineHeight + 1;
	if (lineAfter < pcs->LinesDisplayed()) {
		return pdoc->LineStart(pcs->DocFromDisplay(lineAfter) + 1);
	}
	return pdoc->Length();
}

// Style to a position within the view. If this causes a change at end of last line then
// affects later lines so style all the viewed text.



// Style for an area but bound the amount of styling to remain responsive


void Editor::IdleWork() {
	// Style the line after the modification as this allows modifications that change just the
	// line of the modification to heal instead of propagating to the rest of the window.
	if (FlagSet(workNeeded.items, WorkItems::style)) {
		StyleToPositionInView(pdoc->LineStart(pdoc->LineFromPosition(workNeeded.upTo) + 2));
	}
	NotifyUpdateUI();
	workNeeded.Reset();
}

void Editor::QueueIdleWork(WorkItems items, Sci::Position upTo) {
	workNeeded.Need(items, upTo);
}

bool Editor::PaintContains(PRectangle rc) {
	if (rc.Empty()) {
		return true;
	}
	return rcPaint.Contains(rc);
}

bool Editor::PaintContainsMargin() {
	if (HasMarginWindow()) {
		// With separate margin view, paint of text view
		// never contains margin.
		return false;
	}
	PRectangle rcSelMargin = GetClientRectangle();
	rcSelMargin.right = static_cast<XYPOSITION>(vs.textStart);
	return PaintContains(rcSelMargin);
}

void Editor::CheckForChangeOutsidePaint(Range r) {
	if (paintState == PaintState::painting && !paintingAllText) {
		//Platform::DebugPrintf("Checking range in paint %d-%d\n", r.start, r.end);
		if (!r.Valid())
			return;

		PRectangle rcRange = RectangleFromRange(r, 0);
		const PRectangle rcText = GetTextRectangle();
		if (rcRange.top < rcText.top) {
			rcRange.top = rcText.top;
		}
		if (rcRange.bottom > rcText.bottom) {
			rcRange.bottom = rcText.bottom;
		}

		if (!PaintContains(rcRange)) {
			AbandonPaint();
			paintAbandonedByStyling = true;
		}
	}
}

void Editor::SetAnnotationHeights(Sci::Line start, Sci::Line end) {
	if (vs.annotationVisible != AnnotationVisible::Hidden) {
		RefreshStyleData();
		bool changedHeight = false;
		for (Sci::Line line=start; line<end && line<pdoc->LinesTotal(); line++) {
			int linesWrapped = 1;
			if (Wrapping()) {
				AutoSurface surface(this);
				std::shared_ptr<LineLayout> ll = view.RetrieveLineLayout(line, *this);
				if (surface && ll) {
					view.LayoutLine(*this, surface, vs, ll.get(), wrapWidth);
					linesWrapped = ll->lines;
				}
			}
			if (pcs->SetHeight(line, pdoc->AnnotationLines(line) + linesWrapped))
				changedHeight = true;
		}
		if (changedHeight) {
			SetScrollBars();
			SetVerticalScrollPos();
			Redraw();
		}
	}
}

void Editor::SetDocPointer(Document *document) {
	//Platform::DebugPrintf("** %x setdoc to %x\n", pdoc, document);
	pdoc->RemoveWatcher(this, nullptr);
	pdoc->Release();
	if (!document) {
		pdoc = new Document(DocumentOption::Default);
	} else {
		pdoc = document;
	}
	pdoc->AddRef();
	modelState.reset();
	pcs = ContractionStateCreate(pdoc->IsLarge());

	// Ensure all positions within document
	sel.Clear();
	targetRange = SelectionSegment();

	braces[0] = Sci::invalidPosition;
	braces[1] = Sci::invalidPosition;

	vs.ReleaseAllExtendedStyles();

	SetRepresentations();

	scrollToAfterWrap.reset();

	// Reset the contraction state to fully shown.
	pcs->Clear();
	pcs->InsertLines(0, pdoc->LinesTotal() - 1);
	SetAnnotationHeights(0, pdoc->LinesTotal());
	view.llc.Deallocate();
	NeedWrapping();

	hotspot = Range(Sci::invalidPosition);
	hoverIndicatorPos = Sci::invalidPosition;

	view.ClearAllTabstops();

	pdoc->AddWatcher(this, nullptr);
	SetScrollBars();
	Redraw();
}

// GetTag: definition in EditorSearch.cxx.

std::unique_ptr<Surface> Editor::CreateMeasurementSurface() const {
	if (!wMain.GetID()) {
		return {};
	}
	std::unique_ptr<Surface> surf = Surface::Allocate();
	surf->Init(wMain.GetID());
	surf->SetMode(CurrentSurfaceMode());
	return surf;
}

std::unique_ptr<Surface> Editor::CreateDrawingSurface(SurfaceID sid) const {
	if (!wMain.GetID()) {
		return {};
	}
	std::unique_ptr<Surface> surf = Surface::Allocate();
	surf->Init(sid, wMain.GetID());
	surf->SetMode(CurrentSurfaceMode());
	return surf;
}

