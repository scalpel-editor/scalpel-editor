// Scintilla source code edit control
/** @file EditorFolding.cxx
 ** Fold levels, expand/collapse, line visibility, and display-line mapping.
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

// Display line for a document line when some lines are hidden or annotated.
// Invisible lines share the display line of the previous visible line. When lines wrap, one document line can span several display lines.
Sci::Line Editor::VisibleFromDocLine(Sci::Line docLine) const noexcept {
	return pcs->DisplayFromDoc(docLine);
}

// Document line for a display line (0 is the first line of the document).
// displayLine <= 0 yields 0; past the last display line yields the last document line.
Sci::Line Editor::DocLineFromVisible(Sci::Line displayLine) const noexcept {
	return pcs->DocFromDisplay(displayLine);
}

// Fold level is a 32-bit value: number part (0..NumberMask) plus WhiteFlag / HeaderFlag.
// Base is FoldLevel::Base so levels can be compared without underflow. Returns the previous level.
int Editor::SetFoldLevel(Sci::Line line, FoldLevel level) {
	const int prev = pdoc->SetLevel(line, static_cast<int>(level));
	if (prev != static_cast<int>(level))
		RedrawSelMargin();
	return prev;
}

// Fold level of line: number part plus WhiteFlag / HeaderFlag.
FoldLevel Editor::GetFoldLevel(Sci::Line line) const noexcept {
	return pdoc->GetFoldLevel(line);
}

// Last line that would show or hide when toggling the fold at line.
// When level is omitted, uses the fold level of line.
Sci::Line Editor::GetLastChild(Sci::Line line, std::optional<FoldLevel> level) const {
	return pdoc->GetLastChild(line, level);
}

// Nearest header line above line with a lower fold level, or -1.
Sci::Line Editor::GetFoldParent(Sci::Line line) const noexcept {
	return pdoc->GetFoldParent(line);
}

// Mark a line range visible or invisible. Does not change fold levels or flags.
void Editor::ShowLines(Sci::Line lineStart, Sci::Line lineEnd) {
	pcs->SetVisible(lineStart, lineEnd, true);
	SetScrollBars();
	Redraw();
}

// Mark a line range invisible. Does not change fold levels.
void Editor::HideLines(Sci::Line lineStart, Sci::Line lineEnd) {
	pcs->SetVisible(lineStart, lineEnd, false);
	SetScrollBars();
	Redraw();
}

// True when no line is hidden in the contraction map.
bool Editor::GetAllLinesVisible() const noexcept {
	return !pcs->HiddenLines();
}

// True when the fold header at lineDoc is expanded (children may be visible).
bool Editor::GetFoldExpanded(Sci::Line lineDoc) const noexcept {
	return pcs->GetExpanded(lineDoc);
}

// AutomaticFold::Show expands folds that hold the caret or insertion; Click handles margin clicks; Change reacts to fold-level edits.
void Editor::SetAutomaticFold(AutomaticFold automatic) {
	foldAutomatic = automatic;
}

// AutomaticFold flags currently enabled.
AutomaticFold Editor::GetAutomaticFold() const noexcept {
	return foldAutomatic;
}

// Draw fold lines before/after expanded or contracted headers, or show level/state debug in the margin.
void Editor::SetFoldFlags(FoldFlag flags) {
	foldFlags = flags;
	Redraw();
}

// Toggle fold state for a header line. Optional per-line display text appears to the right of contracted text when display style is not Hidden.
void Editor::ToggleFold(Sci::Line line) {
	FoldLine(line, FoldAction::Toggle);
}

// Set per-line fold display text then toggle the fold at line.
// text may be null to clear the per-line tag.
void Editor::ToggleFoldShowText(Sci::Line line, const char *text) {
	pcs->SetFoldDisplayText(line, text);
	FoldLine(line, FoldAction::Toggle);
}

// FoldDisplayTextStyle::Hidden (default), Standard, or Boxed.
void Editor::FoldDisplayTextSetStyle(FoldDisplayTextStyle style) {
	foldDisplayTextStyle = style;
	Redraw();
}

// How contracted fold tags are drawn: Hidden, Standard, or Boxed.
FoldDisplayTextStyle Editor::FoldDisplayTextGetStyle() const noexcept {
	return foldDisplayTextStyle;
}

// Default tag shown for contracted headers that have no per-line text. Empty or null clears it.
void Editor::SetDefaultFoldDisplayText(const char *text) {
	EditModel::SetDefaultFoldDisplayText(text);
	Redraw();
}

// Expand or contract this header and every nested header under it.
void Editor::FoldChildren(Sci::Line line, FoldAction action) {
	FoldExpand(line, action, pdoc->GetFoldLevel(line));
}

// Expand every child of line down to the given fold level.
void Editor::ExpandChildren(Sci::Line line, FoldLevel level) {
	FoldExpand(line, FoldAction::Expand, level);
}

// Ensure a document line is not under a contracted fold.
void Editor::EnsureVisible(Sci::Line line) {
	EnsureLineVisible(line, false);
}

// Same as EnsureVisible, then scroll so the line is inside the visible-policy slop.
void Editor::EnsureVisibleEnforcePolicy(Sci::Line line) {
	EnsureLineVisible(line, true);
}

/**
 * Recursively expand a fold, making lines visible except where they have an unexpanded parent.
 */
Sci::Line Editor::ExpandLine(Sci::Line line) {
	const Sci::Line lineMaxSubord = pdoc->GetLastChild(line);
	line++;
	Sci::Line lineStart = line;
	while (line <= lineMaxSubord) {
		const FoldLevel level = pdoc->GetFoldLevel(line);
		if (LevelIsHeader(level)) {
			pcs->SetVisible(lineStart, line, true);
			if (pcs->GetExpanded(line)) {
				line = ExpandLine(line);
			} else {
				line = pdoc->GetLastChild(line);
			}
			lineStart = line + 1;
		}
		line++;
	}
	if (lineStart <= lineMaxSubord) {
		pcs->SetVisible(lineStart, lineMaxSubord, true);
	}
	return lineMaxSubord;
}

// Set expanded state without walking children. Redraws the fold margin when it changes.
void Editor::SetFoldExpanded(Sci::Line lineDoc, bool expanded) {
	if (pcs->SetExpanded(lineDoc, expanded)) {
		RedrawSelMargin();
	}
}

// Expand, contract, or toggle the fold containing line.
// Toggle on a non-header walks up to the parent header. Contracting can move the caret
// into view when it lay inside the hidden range; expanding ensures the header is visible.
void Editor::FoldLine(Sci::Line line, FoldAction action) {
	if (line >= 0) {
		if (action == FoldAction::Toggle) {
			if (!LevelIsHeader(pdoc->GetFoldLevel(line))) {
				line = pdoc->GetFoldParent(line);
				if (line < 0)
					return;
			}
			action = (pcs->GetExpanded(line)) ? FoldAction::Contract : FoldAction::Expand;
		}

		if (action == FoldAction::Contract) {
			const Sci::Line lineMaxSubord = pdoc->GetLastChild(line);
			if (lineMaxSubord > line) {
				pcs->SetExpanded(line, false);
				pcs->SetVisible(line + 1, lineMaxSubord, false);

				const Sci::Line lineCurrent =
					pdoc->SciLineFromPosition(sel.MainCaret());
				if (lineCurrent > line && lineCurrent <= lineMaxSubord) {
					// This does not re-expand the fold
					EnsureCaretVisible();
				}
			}

		} else {
			if (!(pcs->GetVisible(line))) {
				EnsureLineVisible(line, false);
				MoveCaretToLine(line);
			}
			pcs->SetExpanded(line, true);
			ExpandLine(line);
		}

		SetScrollBars();
		Redraw();
	}
}

// Expand or contract line and set the same expanded flag on every nested header under it.
// Child lines are made visible or hidden as a block down to the last subordinate.
void Editor::FoldExpand(Sci::Line line, FoldAction action, FoldLevel level) {
	bool expanding = action == FoldAction::Expand;
	if (action == FoldAction::Toggle) {
		expanding = !pcs->GetExpanded(line);
	}
	// Ensure child lines lexed and fold information extracted before
	// flipping the state.
	pdoc->GetLastChild(line, LevelNumberPart(level));
	SetFoldExpanded(line, expanding);
	if (expanding && (pcs->HiddenLines() == 0))
		// Nothing to do
		return;
	const Sci::Line lineMaxSubord = pdoc->GetLastChild(line, LevelNumberPart(level));
	line++;
	pcs->SetVisible(line, lineMaxSubord, expanding);
	while (line <= lineMaxSubord) {
		const FoldLevel levelLine = pdoc->GetFoldLevel(line);
		if (LevelIsHeader(levelLine)) {
			SetFoldExpanded(line, expanding);
		}
		line++;
	}
	SetScrollBars();
	Redraw();
}

// Next contracted fold header at or after lineStart, or -1 when none remain.
Sci::Line Editor::ContractedFoldNext(Sci::Line lineStart) const noexcept {
	for (Sci::Line line = lineStart; line<pdoc->LinesTotal();) {
		if (!pcs->GetExpanded(line) && LevelIsHeader(pdoc->GetFoldLevel(line)))
			return line;
		line = pcs->ContractedNext(line+1);
		if (line < 0)
			return -1;
	}

	return -1;
}

/**
 * Recurse up from this line to find any folds that prevent this line from being visible
 * and unfold them all.
 */
void Editor::EnsureLineVisible(Sci::Line lineDoc, bool enforcePolicy) {

	// In case in need of wrapping to ensure DisplayFromDoc works.
	if (lineDoc >= wrapPending.start) {
		if (WrapLines(WrapScope::wsAll)) {
			Redraw();
		}
	}

	if (!pcs->GetVisible(lineDoc)) {
		// Back up to find a non-blank line
		Sci::Line lookLine = lineDoc;
		FoldLevel lookLineLevel = pdoc->GetFoldLevel(lookLine);
		while ((lookLine > 0) && LevelIsWhitespace(lookLineLevel)) {
			lookLineLevel = pdoc->GetFoldLevel(--lookLine);
		}
		Sci::Line lineParent = pdoc->GetFoldParent(lookLine);
		if (lineParent < 0) {
			// Backed up to a top level line, so try to find parent of initial line
			lineParent = pdoc->GetFoldParent(lineDoc);
		}
		if (lineParent >= 0) {
			if (lineDoc != lineParent)
				EnsureLineVisible(lineParent, enforcePolicy);
			if (!pcs->GetExpanded(lineParent)) {
				pcs->SetExpanded(lineParent, true);
				ExpandLine(lineParent);
			}
		}
		SetScrollBars();
		Redraw();
	}
	if (enforcePolicy) {
		const Sci::Line lineDisplay = pcs->DisplayFromDoc(lineDoc);
		if (FlagSet(visiblePolicy.policy, VisiblePolicy::Slop)) {
			if ((topLine > lineDisplay) || ((FlagSet(visiblePolicy.policy, VisiblePolicy::Strict)) && (topLine + visiblePolicy.slop > lineDisplay))) {
				SetTopLine(std::clamp<Sci::Line>(lineDisplay - visiblePolicy.slop, 0, MaxScrollPos()));
				SetVerticalScrollPos();
				Redraw();
			} else if ((lineDisplay > topLine + LinesOnScreen() - 1) ||
			        ((FlagSet(visiblePolicy.policy, VisiblePolicy::Strict)) && (lineDisplay > topLine + LinesOnScreen() - 1 - visiblePolicy.slop))) {
				SetTopLine(std::clamp<Sci::Line>(lineDisplay - LinesOnScreen() + 1 + visiblePolicy.slop, 0, MaxScrollPos()));
				SetVerticalScrollPos();
				Redraw();
			}
		} else {
			if ((topLine > lineDisplay) || (lineDisplay > topLine + LinesOnScreen() - 1) || (FlagSet(visiblePolicy.policy, VisiblePolicy::Strict))) {
				SetTopLine(std::clamp<Sci::Line>(lineDisplay - LinesOnScreen() / 2 + 1, 0, MaxScrollPos()));
				SetVerticalScrollPos();
				Redraw();
			}
		}
	}
}

// Expand or contract the whole document. Toggle discovers state from the first header.
// ContractEveryLevel contracts nested headers as well as top-level ones.
void Editor::FoldAll(FoldAction action) {
	const Sci::Line maxLine = pdoc->LinesTotal();
	const bool contractAll = FlagSet(action, FoldAction::ContractEveryLevel);
	action = static_cast<FoldAction>(static_cast<int>(action) & ~static_cast<int>(FoldAction::ContractEveryLevel));
	bool expanding = action == FoldAction::Expand;
	if (!expanding) {
		pdoc->EnsureStyledTo(pdoc->Length());
	}
	Sci::Line line = 0;
	if (action == FoldAction::Toggle) {
		// Discover current state
		for (; line < maxLine; line++) {
			if (LevelIsHeader(pdoc->GetFoldLevel(line))) {
				expanding = !pcs->GetExpanded(line);
				break;
			}
		}
	}
	if (expanding) {
		pcs->SetVisible(0, maxLine-1, true);
		pcs->ExpandAll();
	} else {
		for (; line < maxLine; line++) {
			const FoldLevel level = pdoc->GetFoldLevel(line);
			if (LevelIsHeader(level)) {
				if (FoldLevel::Base == LevelNumberPart(level)) {
					SetFoldExpanded(line, false);
					const Sci::Line lineMaxSubord = pdoc->GetLastChild(line);
					if (lineMaxSubord > line) {
						pcs->SetVisible(line + 1, lineMaxSubord, false);
						if (!contractAll) {
							line = lineMaxSubord;
						}
					}
				} else if (contractAll) {
					SetFoldExpanded(line, false);
				}
			}
		}
	}
	SetScrollBars();
	Redraw();
}

// React to a fold-level change at line so expansion and visibility stay consistent
// when headers appear, disappear, or merge with neighbouring blocks.
void Editor::FoldChanged(Sci::Line line, FoldLevel levelNow, FoldLevel levelPrev) {
	if (LevelIsHeader(levelNow)) {
		if (!LevelIsHeader(levelPrev)) {
			// Adding a fold point.
			if (pcs->SetExpanded(line, true)) {
				RedrawSelMargin();
			}
			FoldExpand(line, FoldAction::Expand, levelPrev);
		}
	} else if (LevelIsHeader(levelPrev)) {
		const Sci::Line prevLine = line - 1;
		const FoldLevel prevLineLevel = pdoc->GetFoldLevel(prevLine);

		// Combining two blocks where the first block is collapsed (e.g. by deleting the line(s) which separate(s) the two blocks)
		if ((LevelNumber(prevLineLevel) == LevelNumber(levelNow)) && !pcs->GetVisible(prevLine))
			FoldLine(pdoc->GetFoldParent(prevLine), FoldAction::Expand);

		if (!pcs->GetExpanded(line)) {
			// Removing the fold from one that has been contracted so should expand
			// otherwise lines are left invisible with no way to make them visible
			if (pcs->SetExpanded(line, true)) {
				RedrawSelMargin();
			}
			// Combining two blocks where the second one is collapsed (e.g. by adding characters in the line which separates the two blocks)
			FoldExpand(line, FoldAction::Expand, levelPrev);
		}
	}
	if (!LevelIsWhitespace(levelNow) &&
	        (LevelNumber(levelPrev) > LevelNumber(levelNow))) {
		if (pcs->HiddenLines()) {
			// See if should still be hidden
			const Sci::Line parentLine = pdoc->GetFoldParent(line);
			if ((parentLine < 0) || (pcs->GetExpanded(parentLine) && pcs->GetVisible(parentLine))) {
				pcs->SetVisible(line, line, true);
				SetScrollBars();
				Redraw();
			}
		}
	}

	// Combining two blocks where the first one is collapsed (e.g. by adding characters in the line which separates the two blocks)
	if (!LevelIsWhitespace(levelNow) && (LevelNumber(levelPrev) < LevelNumber(levelNow))) {
		if (pcs->HiddenLines()) {
			const Sci::Line parentLine = pdoc->GetFoldParent(line);
			if (!pcs->GetExpanded(parentLine) && pcs->GetVisible(line))
				FoldLine(parentLine, FoldAction::Expand);
		}
	}
}

// When AutomaticFold::Show is set, expand folds covering [pos, pos+len).
// Otherwise notify the host that the range needs to be shown.
void Editor::NeedShown(Sci::Position pos, Sci::Position len) {
	if (FlagSet(foldAutomatic, AutomaticFold::Show)) {
		const Sci::Line lineStart = pdoc->SciLineFromPosition(pos);
		const Sci::Line lineEnd = pdoc->SciLineFromPosition(pos+len);
		for (Sci::Line line = lineStart; line <= lineEnd; line++) {
			EnsureLineVisible(line, false);
		}
	} else {
		NotifyNeedShown(pos, len);
	}
}
