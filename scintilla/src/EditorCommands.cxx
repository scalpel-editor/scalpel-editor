// Scintilla source code edit control
/** @file EditorCommands.cxx
 ** Bindable editing actions: ExecuteCommand and related key-map operations.
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
#include "EditorRecording.h"
#include "ElapsedPeriod.h"

using namespace Scintilla;
using namespace Scintilla::Internal;

int Editor::ExecuteCommand(EditorCommand command) {
	// Capture bindable commands as typed RecordedCommand while recording.
	// Autocomplete/call-tip interception in ScintillaBase never reaches here
	// for keys it consumes, so those do not produce records.
	if (IsRecordableCommand(command)) {
		EmitRecordedAction(RecordedCommand{command});
	}

	switch (command) {
	case EditorCommand::LineDown:
		CursorUpOrDown(1, Selection::SelTypes::none);
		break;
	case EditorCommand::LineDownExtend:
		CursorUpOrDown(1, Selection::SelTypes::stream);
		break;
	case EditorCommand::LineDownRectExtend:
		CursorUpOrDown(1, Selection::SelTypes::rectangle);
		break;
	case EditorCommand::ParaDown:
		ParaUpOrDown(1, Selection::SelTypes::none);
		break;
	case EditorCommand::ParaDownExtend:
		ParaUpOrDown(1, Selection::SelTypes::stream);
		break;
	case EditorCommand::LineScrollDown:
		ScrollTo(topLine + 1);
		MoveCaretInsideView(false);
		break;
	case EditorCommand::LineUp:
		CursorUpOrDown(-1, Selection::SelTypes::none);
		break;
	case EditorCommand::LineUpExtend:
		CursorUpOrDown(-1, Selection::SelTypes::stream);
		break;
	case EditorCommand::LineUpRectExtend:
		CursorUpOrDown(-1, Selection::SelTypes::rectangle);
		break;
	case EditorCommand::ParaUp:
		ParaUpOrDown(-1, Selection::SelTypes::none);
		break;
	case EditorCommand::ParaUpExtend:
		ParaUpOrDown(-1, Selection::SelTypes::stream);
		break;
	case EditorCommand::LineScrollUp:
		ScrollTo(topLine - 1);
		MoveCaretInsideView(false);
		break;

	case EditorCommand::CharLeft:
	case EditorCommand::CharLeftExtend:
	case EditorCommand::CharLeftRectExtend:
	case EditorCommand::CharRight:
	case EditorCommand::CharRightExtend:
	case EditorCommand::CharRightRectExtend:
	case EditorCommand::WordLeft:
	case EditorCommand::WordLeftExtend:
	case EditorCommand::WordRight:
	case EditorCommand::WordRightExtend:
	case EditorCommand::WordLeftEnd:
	case EditorCommand::WordLeftEndExtend:
	case EditorCommand::WordRightEnd:
	case EditorCommand::WordRightEndExtend:
	case EditorCommand::WordPartLeft:
	case EditorCommand::WordPartLeftExtend:
	case EditorCommand::WordPartRight:
	case EditorCommand::WordPartRightExtend:
	case EditorCommand::Home:
	case EditorCommand::HomeExtend:
	case EditorCommand::HomeRectExtend:
	case EditorCommand::HomeDisplay:
	case EditorCommand::HomeDisplayExtend:
	case EditorCommand::HomeWrap:
	case EditorCommand::HomeWrapExtend:
	case EditorCommand::VCHome:
	case EditorCommand::VCHomeExtend:
	case EditorCommand::VCHomeRectExtend:
	case EditorCommand::VCHomeDisplay:
	case EditorCommand::VCHomeDisplayExtend:
	case EditorCommand::VCHomeWrap:
	case EditorCommand::VCHomeWrapExtend:
	case EditorCommand::LineEnd:
	case EditorCommand::LineEndExtend:
	case EditorCommand::LineEndRectExtend:
	case EditorCommand::LineEndDisplay:
	case EditorCommand::LineEndDisplayExtend:
	case EditorCommand::LineEndWrap:
	case EditorCommand::LineEndWrapExtend:
		return HorizontalMove(command);

	case EditorCommand::DocumentStart:
		MovePositionTo(0);
		SetLastXChosen();
		break;
	case EditorCommand::DocumentStartExtend:
		MovePositionTo(0, Selection::SelTypes::stream);
		SetLastXChosen();
		break;
	case EditorCommand::DocumentEnd:
		MovePositionTo(pdoc->Length());
		SetLastXChosen();
		break;
	case EditorCommand::DocumentEndExtend:
		MovePositionTo(pdoc->Length(), Selection::SelTypes::stream);
		SetLastXChosen();
		break;
	case EditorCommand::StutteredPageUp:
		PageMove(-1, Selection::SelTypes::none, true);
		break;
	case EditorCommand::StutteredPageUpExtend:
		PageMove(-1, Selection::SelTypes::stream, true);
		break;
	case EditorCommand::StutteredPageDown:
		PageMove(1, Selection::SelTypes::none, true);
		break;
	case EditorCommand::StutteredPageDownExtend:
		PageMove(1, Selection::SelTypes::stream, true);
		break;
	case EditorCommand::PageUp:
		PageMove(-1);
		break;
	case EditorCommand::PageUpExtend:
		PageMove(-1, Selection::SelTypes::stream);
		break;
	case EditorCommand::PageUpRectExtend:
		PageMove(-1, Selection::SelTypes::rectangle);
		break;
	case EditorCommand::PageDown:
		PageMove(1);
		break;
	case EditorCommand::PageDownExtend:
		PageMove(1, Selection::SelTypes::stream);
		break;
	case EditorCommand::PageDownRectExtend:
		PageMove(1, Selection::SelTypes::rectangle);
		break;
	case EditorCommand::EditToggleOvertype:
		SetOvertype(!inOverstrike);
		break;
	case EditorCommand::Cancel:            	// Cancel any modes - handled in subclass
		// Also unselect text
		CancelModes();
		if ((sel.Count() > 1) && !sel.IsRectangular()) {
			// Drop additional selections
			InvalidateWholeSelection();
			sel.DropAdditionalRanges();
		}
		break;
	case EditorCommand::DeleteBack:
		DelCharBack(true);
		if (AnyOf(caretSticky, CaretSticky::Off, CaretSticky::WhiteSpace)) {
			SetLastXChosen();
		}
		EnsureCaretVisible();
		break;
	case EditorCommand::DeleteBackNotLine:
		DelCharBack(false);
		if (AnyOf(caretSticky, CaretSticky::Off, CaretSticky::WhiteSpace)) {
			SetLastXChosen();
		}
		EnsureCaretVisible();
		break;
	case EditorCommand::Tab:
	case EditorCommand::LineIndent:
		Indent(true, command == EditorCommand::LineIndent);
		if (caretSticky == CaretSticky::Off) {
			SetLastXChosen();
		}
		EnsureCaretVisible();
		ShowCaretAtCurrentPosition();		// Avoid blinking
		break;
	case EditorCommand::BackTab:
	case EditorCommand::LineDedent:
		Indent(false, command == EditorCommand::LineDedent);
		if (AnyOf(caretSticky, CaretSticky::Off, CaretSticky::WhiteSpace)) {
			SetLastXChosen();
		}
		EnsureCaretVisible();
		ShowCaretAtCurrentPosition();		// Avoid blinking
		break;
	case EditorCommand::NewLine:
		NewLine();
		break;
	case EditorCommand::FormFeed:
		AddChar('\f');
		break;
	case EditorCommand::ZoomIn:
		if (vs.zoomLevel < 60) {
			vs.zoomLevel++;
			InvalidateStyleRedraw();
			NotifyZoom();
		}
		break;
	case EditorCommand::ZoomOut:
		if (vs.zoomLevel > -10) {
			vs.zoomLevel--;
			InvalidateStyleRedraw();
			NotifyZoom();
		}
		break;

	case EditorCommand::DelWordLeft:
	case EditorCommand::DelWordRight:
	case EditorCommand::DelWordRightEnd:
	case EditorCommand::DelLineLeft:
	case EditorCommand::DelLineRight:
		return DelWordOrLine(command);

	case EditorCommand::LineCopy: {
			const Sci::Line lineStart = pdoc->SciLineFromPosition(SelectionStart().Position());
			const Sci::Line lineEnd = pdoc->SciLineFromPosition(SelectionEnd().Position());
			CopyRangeToClipboard(pdoc->LineStart(lineStart),
				pdoc->LineStart(lineEnd + 1));
		}
		break;
	case EditorCommand::LineCut: {
			const Sci::Line lineStart = pdoc->SciLineFromPosition(SelectionStart().Position());
			const Sci::Line lineEnd = pdoc->SciLineFromPosition(SelectionEnd().Position());
			const Sci::Position start = pdoc->LineStart(lineStart);
			const Sci::Position end = pdoc->LineStart(lineEnd + 1);
			SetSelection(start, end);
			Cut();
			SetLastXChosen();
		}
		break;
	case EditorCommand::LineDelete:
		LineDelete();
		break;
	case EditorCommand::LineTranspose:
		LineTranspose();
		break;
	case EditorCommand::LineReverse:
		LineReverse();
		break;
	case EditorCommand::LineDuplicate:
		Duplicate(true);
		break;
	case EditorCommand::SelectionDuplicate:
		Duplicate(false);
		break;
	case EditorCommand::LowerCase:
		ChangeCaseOfSelection(CaseMapping::lower);
		break;
	case EditorCommand::UpperCase:
		ChangeCaseOfSelection(CaseMapping::upper);
		break;
	case EditorCommand::ScrollToStart:
		ScrollTo(0);
		break;
	case EditorCommand::ScrollToEnd:
		ScrollTo(MaxScrollPos());
		break;

	case EditorCommand::Cut:
		Cut();
		SetLastXChosen();
		break;
	case EditorCommand::Copy:
		Copy();
		break;
	case EditorCommand::CopyAllowLine:
		CopyAllowLine();
		break;
	case EditorCommand::CutAllowLine:
		CutAllowLine();
		SetLastXChosen();
		break;
	case EditorCommand::Paste:
		Paste();
		if (AnyOf(caretSticky, CaretSticky::Off, CaretSticky::WhiteSpace)) {
			SetLastXChosen();
		}
		EnsureCaretVisible();
		break;
	case EditorCommand::Clear:
		Clear();
		SetLastXChosen();
		EnsureCaretVisible();
		break;
	case EditorCommand::Undo:
		Undo();
		SetLastXChosen();
		break;
	case EditorCommand::Redo:
		Redo();
		break;
	case EditorCommand::SelectAll:
		SelectAll();
		break;
	case EditorCommand::VerticalCentreCaret:
		VerticalCentreCaret();
		break;
	case EditorCommand::MoveSelectedLinesUp:
		MoveSelectedLinesUp();
		break;
	case EditorCommand::MoveSelectedLinesDown:
		MoveSelectedLinesDown();
		break;
	case EditorCommand::RotateSelection:
		sel.RotateMain();
		InvalidateWholeSelection();
		break;
	case EditorCommand::SwapMainAnchorCaret:
		InvalidateSelection(sel.RangeMain());
		sel.RangeMain().Swap();
		break;
	case EditorCommand::MultipleSelectAddNext:
		MultipleSelectAdd(AddNumber::one);
		break;
	case EditorCommand::MultipleSelectAddEach:
		MultipleSelectAdd(AddNumber::each);
		break;
	case EditorCommand::LinesJoin:
		LinesJoin();
		break;
	case EditorCommand::LinesSplit:
		// Pixel width is not available on the command path; use zero (document wrap width).
		LinesSplit(0);
		break;
	case EditorCommand::SearchAnchor:
		SearchAnchor();
		break;
	case EditorCommand::SearchNext:
	case EditorCommand::SearchPrev:
		// Parameterized search stays on the temporary message path (text + flags).
		break;
	case EditorCommand::SetZoom:
		// Key binding sends no level: reset zoom to 0.
		if (SetAppearance(vs.zoomLevel, 0)) {
			NotifyZoom();
		}
		break;
	default:
		break;
	}
		return 0;
}

// When overtype is on, each typed character replaces the character to the right of the caret;
// when off, characters are inserted. Toggling updates selection notifications and caret draw.
void Editor::SetOvertype(bool overtype) {
	if (inOverstrike != overtype) {
		inOverstrike = overtype;
		ContainerNeedsUpdate(Update::Selection);
		ShowCaretAtCurrentPosition();
		SetIdle(true);
	}
}

// True when overtype (overstrike) mode is active.
bool Editor::GetOvertype() const noexcept {
	return inOverstrike;
}

// Bind a key chord to a command. EditorCommand::None clears the binding for that chord.
void Editor::AssignCmdKey(Keys key, KeyMod modifiers, EditorCommand command) {
	kmap.AssignCmdKey(key, modifiers, command);
}

// Remove the binding for a key chord (same as assigning EditorCommand::None).
void Editor::ClearCmdKey(Keys key, KeyMod modifiers) {
	kmap.AssignCmdKey(key, modifiers, EditorCommand::None);
}

// Remove every key binding.
void Editor::ClearAllCmdKeys() {
	kmap.Clear();
}
