// Scintilla source code edit control
/** @file EditorCommands.cxx
 ** Bindable editing actions: EditorCommand conversion helpers and ExecuteCommand.
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
#include "EditorRecording.h"
#include "ElapsedPeriod.h"

using namespace Scintilla;
using namespace Scintilla::Internal;

namespace Scintilla::Internal {

Message MessageFromCommand(EditorCommand command) noexcept {
	switch (command) {
	case EditorCommand::LineDown: return Message::LineDown;
	case EditorCommand::LineDownExtend: return Message::LineDownExtend;
	case EditorCommand::LineDownRectExtend: return Message::LineDownRectExtend;
	case EditorCommand::LineUp: return Message::LineUp;
	case EditorCommand::LineUpExtend: return Message::LineUpExtend;
	case EditorCommand::LineUpRectExtend: return Message::LineUpRectExtend;
	case EditorCommand::LineScrollDown: return Message::LineScrollDown;
	case EditorCommand::LineScrollUp: return Message::LineScrollUp;
	case EditorCommand::ParaDown: return Message::ParaDown;
	case EditorCommand::ParaDownExtend: return Message::ParaDownExtend;
	case EditorCommand::ParaUp: return Message::ParaUp;
	case EditorCommand::ParaUpExtend: return Message::ParaUpExtend;
	case EditorCommand::CharLeft: return Message::CharLeft;
	case EditorCommand::CharLeftExtend: return Message::CharLeftExtend;
	case EditorCommand::CharLeftRectExtend: return Message::CharLeftRectExtend;
	case EditorCommand::CharRight: return Message::CharRight;
	case EditorCommand::CharRightExtend: return Message::CharRightExtend;
	case EditorCommand::CharRightRectExtend: return Message::CharRightRectExtend;
	case EditorCommand::WordLeft: return Message::WordLeft;
	case EditorCommand::WordLeftExtend: return Message::WordLeftExtend;
	case EditorCommand::WordRight: return Message::WordRight;
	case EditorCommand::WordRightExtend: return Message::WordRightExtend;
	case EditorCommand::WordLeftEnd: return Message::WordLeftEnd;
	case EditorCommand::WordLeftEndExtend: return Message::WordLeftEndExtend;
	case EditorCommand::WordRightEnd: return Message::WordRightEnd;
	case EditorCommand::WordRightEndExtend: return Message::WordRightEndExtend;
	case EditorCommand::WordPartLeft: return Message::WordPartLeft;
	case EditorCommand::WordPartLeftExtend: return Message::WordPartLeftExtend;
	case EditorCommand::WordPartRight: return Message::WordPartRight;
	case EditorCommand::WordPartRightExtend: return Message::WordPartRightExtend;
	case EditorCommand::Home: return Message::Home;
	case EditorCommand::HomeExtend: return Message::HomeExtend;
	case EditorCommand::HomeRectExtend: return Message::HomeRectExtend;
	case EditorCommand::HomeDisplay: return Message::HomeDisplay;
	case EditorCommand::HomeDisplayExtend: return Message::HomeDisplayExtend;
	case EditorCommand::HomeWrap: return Message::HomeWrap;
	case EditorCommand::HomeWrapExtend: return Message::HomeWrapExtend;
	case EditorCommand::VCHome: return Message::VCHome;
	case EditorCommand::VCHomeExtend: return Message::VCHomeExtend;
	case EditorCommand::VCHomeRectExtend: return Message::VCHomeRectExtend;
	case EditorCommand::VCHomeDisplay: return Message::VCHomeDisplay;
	case EditorCommand::VCHomeDisplayExtend: return Message::VCHomeDisplayExtend;
	case EditorCommand::VCHomeWrap: return Message::VCHomeWrap;
	case EditorCommand::VCHomeWrapExtend: return Message::VCHomeWrapExtend;
	case EditorCommand::LineEnd: return Message::LineEnd;
	case EditorCommand::LineEndExtend: return Message::LineEndExtend;
	case EditorCommand::LineEndRectExtend: return Message::LineEndRectExtend;
	case EditorCommand::LineEndDisplay: return Message::LineEndDisplay;
	case EditorCommand::LineEndDisplayExtend: return Message::LineEndDisplayExtend;
	case EditorCommand::LineEndWrap: return Message::LineEndWrap;
	case EditorCommand::LineEndWrapExtend: return Message::LineEndWrapExtend;
	case EditorCommand::DocumentStart: return Message::DocumentStart;
	case EditorCommand::DocumentStartExtend: return Message::DocumentStartExtend;
	case EditorCommand::DocumentEnd: return Message::DocumentEnd;
	case EditorCommand::DocumentEndExtend: return Message::DocumentEndExtend;
	case EditorCommand::PageUp: return Message::PageUp;
	case EditorCommand::PageUpExtend: return Message::PageUpExtend;
	case EditorCommand::PageUpRectExtend: return Message::PageUpRectExtend;
	case EditorCommand::PageDown: return Message::PageDown;
	case EditorCommand::PageDownExtend: return Message::PageDownExtend;
	case EditorCommand::PageDownRectExtend: return Message::PageDownRectExtend;
	case EditorCommand::StutteredPageUp: return Message::StutteredPageUp;
	case EditorCommand::StutteredPageUpExtend: return Message::StutteredPageUpExtend;
	case EditorCommand::StutteredPageDown: return Message::StutteredPageDown;
	case EditorCommand::StutteredPageDownExtend: return Message::StutteredPageDownExtend;
	case EditorCommand::ScrollToStart: return Message::ScrollToStart;
	case EditorCommand::ScrollToEnd: return Message::ScrollToEnd;
	case EditorCommand::EditToggleOvertype: return Message::EditToggleOvertype;
	case EditorCommand::Cancel: return Message::Cancel;
	case EditorCommand::DeleteBack: return Message::DeleteBack;
	case EditorCommand::DeleteBackNotLine: return Message::DeleteBackNotLine;
	case EditorCommand::Tab: return Message::Tab;
	case EditorCommand::LineIndent: return Message::LineIndent;
	case EditorCommand::BackTab: return Message::BackTab;
	case EditorCommand::LineDedent: return Message::LineDedent;
	case EditorCommand::NewLine: return Message::NewLine;
	case EditorCommand::FormFeed: return Message::FormFeed;
	case EditorCommand::ZoomIn: return Message::ZoomIn;
	case EditorCommand::ZoomOut: return Message::ZoomOut;
	case EditorCommand::SetZoom: return Message::SetZoom;
	case EditorCommand::DelWordLeft: return Message::DelWordLeft;
	case EditorCommand::DelWordRight: return Message::DelWordRight;
	case EditorCommand::DelWordRightEnd: return Message::DelWordRightEnd;
	case EditorCommand::DelLineLeft: return Message::DelLineLeft;
	case EditorCommand::DelLineRight: return Message::DelLineRight;
	case EditorCommand::LineCopy: return Message::LineCopy;
	case EditorCommand::LineCut: return Message::LineCut;
	case EditorCommand::LineDelete: return Message::LineDelete;
	case EditorCommand::LineTranspose: return Message::LineTranspose;
	case EditorCommand::LineReverse: return Message::LineReverse;
	case EditorCommand::LineDuplicate: return Message::LineDuplicate;
	case EditorCommand::SelectionDuplicate: return Message::SelectionDuplicate;
	case EditorCommand::LowerCase: return Message::LowerCase;
	case EditorCommand::UpperCase: return Message::UpperCase;
	case EditorCommand::Cut: return Message::Cut;
	case EditorCommand::Copy: return Message::Copy;
	case EditorCommand::Paste: return Message::Paste;
	case EditorCommand::Clear: return Message::Clear;
	case EditorCommand::CopyAllowLine: return Message::CopyAllowLine;
	case EditorCommand::CutAllowLine: return Message::CutAllowLine;
	case EditorCommand::SelectAll: return Message::SelectAll;
	case EditorCommand::Undo: return Message::Undo;
	case EditorCommand::Redo: return Message::Redo;
	case EditorCommand::VerticalCentreCaret: return Message::VerticalCentreCaret;
	case EditorCommand::MoveSelectedLinesUp: return Message::MoveSelectedLinesUp;
	case EditorCommand::MoveSelectedLinesDown: return Message::MoveSelectedLinesDown;
	case EditorCommand::RotateSelection: return Message::RotateSelection;
	case EditorCommand::SwapMainAnchorCaret: return Message::SwapMainAnchorCaret;
	case EditorCommand::MultipleSelectAddNext: return Message::MultipleSelectAddNext;
	case EditorCommand::MultipleSelectAddEach: return Message::MultipleSelectAddEach;
	case EditorCommand::LinesJoin: return Message::LinesJoin;
	case EditorCommand::LinesSplit: return Message::LinesSplit;
	case EditorCommand::SearchAnchor: return Message::SearchAnchor;
	case EditorCommand::SearchNext: return Message::SearchNext;
	case EditorCommand::SearchPrev: return Message::SearchPrev;
	default:
		return static_cast<Message>(0);
	}
}

EditorCommand CommandFromMessage(Message message) noexcept {
	switch (message) {
	case Message::LineDown: return EditorCommand::LineDown;
	case Message::LineDownExtend: return EditorCommand::LineDownExtend;
	case Message::LineDownRectExtend: return EditorCommand::LineDownRectExtend;
	case Message::LineUp: return EditorCommand::LineUp;
	case Message::LineUpExtend: return EditorCommand::LineUpExtend;
	case Message::LineUpRectExtend: return EditorCommand::LineUpRectExtend;
	case Message::LineScrollDown: return EditorCommand::LineScrollDown;
	case Message::LineScrollUp: return EditorCommand::LineScrollUp;
	case Message::ParaDown: return EditorCommand::ParaDown;
	case Message::ParaDownExtend: return EditorCommand::ParaDownExtend;
	case Message::ParaUp: return EditorCommand::ParaUp;
	case Message::ParaUpExtend: return EditorCommand::ParaUpExtend;
	case Message::CharLeft: return EditorCommand::CharLeft;
	case Message::CharLeftExtend: return EditorCommand::CharLeftExtend;
	case Message::CharLeftRectExtend: return EditorCommand::CharLeftRectExtend;
	case Message::CharRight: return EditorCommand::CharRight;
	case Message::CharRightExtend: return EditorCommand::CharRightExtend;
	case Message::CharRightRectExtend: return EditorCommand::CharRightRectExtend;
	case Message::WordLeft: return EditorCommand::WordLeft;
	case Message::WordLeftExtend: return EditorCommand::WordLeftExtend;
	case Message::WordRight: return EditorCommand::WordRight;
	case Message::WordRightExtend: return EditorCommand::WordRightExtend;
	case Message::WordLeftEnd: return EditorCommand::WordLeftEnd;
	case Message::WordLeftEndExtend: return EditorCommand::WordLeftEndExtend;
	case Message::WordRightEnd: return EditorCommand::WordRightEnd;
	case Message::WordRightEndExtend: return EditorCommand::WordRightEndExtend;
	case Message::WordPartLeft: return EditorCommand::WordPartLeft;
	case Message::WordPartLeftExtend: return EditorCommand::WordPartLeftExtend;
	case Message::WordPartRight: return EditorCommand::WordPartRight;
	case Message::WordPartRightExtend: return EditorCommand::WordPartRightExtend;
	case Message::Home: return EditorCommand::Home;
	case Message::HomeExtend: return EditorCommand::HomeExtend;
	case Message::HomeRectExtend: return EditorCommand::HomeRectExtend;
	case Message::HomeDisplay: return EditorCommand::HomeDisplay;
	case Message::HomeDisplayExtend: return EditorCommand::HomeDisplayExtend;
	case Message::HomeWrap: return EditorCommand::HomeWrap;
	case Message::HomeWrapExtend: return EditorCommand::HomeWrapExtend;
	case Message::VCHome: return EditorCommand::VCHome;
	case Message::VCHomeExtend: return EditorCommand::VCHomeExtend;
	case Message::VCHomeRectExtend: return EditorCommand::VCHomeRectExtend;
	case Message::VCHomeDisplay: return EditorCommand::VCHomeDisplay;
	case Message::VCHomeDisplayExtend: return EditorCommand::VCHomeDisplayExtend;
	case Message::VCHomeWrap: return EditorCommand::VCHomeWrap;
	case Message::VCHomeWrapExtend: return EditorCommand::VCHomeWrapExtend;
	case Message::LineEnd: return EditorCommand::LineEnd;
	case Message::LineEndExtend: return EditorCommand::LineEndExtend;
	case Message::LineEndRectExtend: return EditorCommand::LineEndRectExtend;
	case Message::LineEndDisplay: return EditorCommand::LineEndDisplay;
	case Message::LineEndDisplayExtend: return EditorCommand::LineEndDisplayExtend;
	case Message::LineEndWrap: return EditorCommand::LineEndWrap;
	case Message::LineEndWrapExtend: return EditorCommand::LineEndWrapExtend;
	case Message::DocumentStart: return EditorCommand::DocumentStart;
	case Message::DocumentStartExtend: return EditorCommand::DocumentStartExtend;
	case Message::DocumentEnd: return EditorCommand::DocumentEnd;
	case Message::DocumentEndExtend: return EditorCommand::DocumentEndExtend;
	case Message::PageUp: return EditorCommand::PageUp;
	case Message::PageUpExtend: return EditorCommand::PageUpExtend;
	case Message::PageUpRectExtend: return EditorCommand::PageUpRectExtend;
	case Message::PageDown: return EditorCommand::PageDown;
	case Message::PageDownExtend: return EditorCommand::PageDownExtend;
	case Message::PageDownRectExtend: return EditorCommand::PageDownRectExtend;
	case Message::StutteredPageUp: return EditorCommand::StutteredPageUp;
	case Message::StutteredPageUpExtend: return EditorCommand::StutteredPageUpExtend;
	case Message::StutteredPageDown: return EditorCommand::StutteredPageDown;
	case Message::StutteredPageDownExtend: return EditorCommand::StutteredPageDownExtend;
	case Message::ScrollToStart: return EditorCommand::ScrollToStart;
	case Message::ScrollToEnd: return EditorCommand::ScrollToEnd;
	case Message::EditToggleOvertype: return EditorCommand::EditToggleOvertype;
	case Message::Cancel: return EditorCommand::Cancel;
	case Message::DeleteBack: return EditorCommand::DeleteBack;
	case Message::DeleteBackNotLine: return EditorCommand::DeleteBackNotLine;
	case Message::Tab: return EditorCommand::Tab;
	case Message::LineIndent: return EditorCommand::LineIndent;
	case Message::BackTab: return EditorCommand::BackTab;
	case Message::LineDedent: return EditorCommand::LineDedent;
	case Message::NewLine: return EditorCommand::NewLine;
	case Message::FormFeed: return EditorCommand::FormFeed;
	case Message::ZoomIn: return EditorCommand::ZoomIn;
	case Message::ZoomOut: return EditorCommand::ZoomOut;
	case Message::SetZoom: return EditorCommand::SetZoom;
	case Message::DelWordLeft: return EditorCommand::DelWordLeft;
	case Message::DelWordRight: return EditorCommand::DelWordRight;
	case Message::DelWordRightEnd: return EditorCommand::DelWordRightEnd;
	case Message::DelLineLeft: return EditorCommand::DelLineLeft;
	case Message::DelLineRight: return EditorCommand::DelLineRight;
	case Message::LineCopy: return EditorCommand::LineCopy;
	case Message::LineCut: return EditorCommand::LineCut;
	case Message::LineDelete: return EditorCommand::LineDelete;
	case Message::LineTranspose: return EditorCommand::LineTranspose;
	case Message::LineReverse: return EditorCommand::LineReverse;
	case Message::LineDuplicate: return EditorCommand::LineDuplicate;
	case Message::SelectionDuplicate: return EditorCommand::SelectionDuplicate;
	case Message::LowerCase: return EditorCommand::LowerCase;
	case Message::UpperCase: return EditorCommand::UpperCase;
	case Message::Cut: return EditorCommand::Cut;
	case Message::Copy: return EditorCommand::Copy;
	case Message::Paste: return EditorCommand::Paste;
	case Message::Clear: return EditorCommand::Clear;
	case Message::CopyAllowLine: return EditorCommand::CopyAllowLine;
	case Message::CutAllowLine: return EditorCommand::CutAllowLine;
	case Message::SelectAll: return EditorCommand::SelectAll;
	case Message::Undo: return EditorCommand::Undo;
	case Message::Redo: return EditorCommand::Redo;
	case Message::VerticalCentreCaret: return EditorCommand::VerticalCentreCaret;
	case Message::MoveSelectedLinesUp: return EditorCommand::MoveSelectedLinesUp;
	case Message::MoveSelectedLinesDown: return EditorCommand::MoveSelectedLinesDown;
	case Message::RotateSelection: return EditorCommand::RotateSelection;
	case Message::SwapMainAnchorCaret: return EditorCommand::SwapMainAnchorCaret;
	case Message::MultipleSelectAddNext: return EditorCommand::MultipleSelectAddNext;
	case Message::MultipleSelectAddEach: return EditorCommand::MultipleSelectAddEach;
	case Message::LinesJoin: return EditorCommand::LinesJoin;
	case Message::LinesSplit: return EditorCommand::LinesSplit;
	case Message::SearchAnchor: return EditorCommand::SearchAnchor;
	case Message::SearchNext: return EditorCommand::SearchNext;
	case Message::SearchPrev: return EditorCommand::SearchPrev;
	default:
		return EditorCommand::None;
	}
}

} // namespace Scintilla::Internal

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
		// Key binding sends no level: reset zoom to 0 (same as WndProc SetZoom with wParam 0).
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
