// Scintilla source code edit control
/** @file ScintillaBase.cxx
 ** ScintillaBase shell: context menu, IME helpers, and lexer glue.
 ** Autocomplete lives in EditorAutocomplete.cxx; call tips live in EditorCallTips.cxx.
 **/
// Copyright 1998-2003 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cstddef>
#include <memory>
#include <string_view>

#include "AutoComplete.h"
#include "CallTip.h"
#include "Document.h"
#include "Editor.h"
#include "EditorCommands.h"
#include "EditorInputTypes.h"
#include "EditorLayoutTypes.h"
#include "EditorStyleTypes.h"
#include "Geometry.h"
#include "Platform.h"
#include "Position.h"
#include "ScintillaBase.h"
#include "Selection.h"

using namespace Scintilla;
using namespace Scintilla::Internal;

ScintillaBase::ScintillaBase() {
	displayPopupMenu = PopUp::All;
	listType = 0;
	maxListWidth = 0;
	multiAutoCMode = MultiAutoComplete::Once;
}

ScintillaBase::~ScintillaBase() = default;

void ScintillaBase::Finalise() {
	Editor::Finalise();
	popup.Destroy();
}

void ScintillaBase::InsertCharacter(std::string_view sv, CharacterSource charSource) {
	const bool acActive = ac.Active();
	const bool isFillUp = acActive && ac.IsFillUpChar(sv[0]);
	if (!isFillUp) {
		Editor::InsertCharacter(sv, charSource);
	}
	if (acActive && ac.Active()) { // if it was and still is active
		AutoCompleteCharacterAdded(sv[0]);
		// For fill ups add the character after the autocompletion has
		// triggered so containers see the key so can display a calltip.
		if (isFillUp) {
			Editor::InsertCharacter(sv, charSource);
		}
	}
}

void ScintillaBase::Command(int cmdId) {

	switch (cmdId) {

	case idAutoComplete:  	// Nothing to do

		break;

	case idCallTip:  	// Nothing to do

		break;

	case idcmdUndo:
		ExecuteCommand(EditorCommand::Undo);
		break;

	case idcmdRedo:
		ExecuteCommand(EditorCommand::Redo);
		break;

	case idcmdCut:
		ExecuteCommand(EditorCommand::Cut);
		break;

	case idcmdCopy:
		ExecuteCommand(EditorCommand::Copy);
		break;

	case idcmdPaste:
		ExecuteCommand(EditorCommand::Paste);
		break;

	case idcmdDelete:
		ExecuteCommand(EditorCommand::Clear);
		break;

	case idcmdSelectAll:
		ExecuteCommand(EditorCommand::SelectAll);
		break;

	default:
		break;
	}
}

int ScintillaBase::ExecuteCommand(EditorCommand command) {
	// Most key commands cancel autocompletion mode
	if (ac.Active()) {
		switch (command) {
			// Except for these
		case EditorCommand::LineDown:
			AutoCompleteMove(1);
			return 0;
		case EditorCommand::LineUp:
			AutoCompleteMove(-1);
			return 0;
		case EditorCommand::PageDown:
			AutoCompleteMove(ac.lb->GetVisibleRows());
			return 0;
		case EditorCommand::PageUp:
			AutoCompleteMove(-ac.lb->GetVisibleRows());
			return 0;
		case EditorCommand::VCHome:
			AutoCompleteMove(-5000);
			return 0;
		case EditorCommand::LineEnd:
			AutoCompleteMove(5000);
			return 0;
		case EditorCommand::DeleteBack:
			DelCharBack(true);
			AutoCompleteCharacterDeleted();
			EnsureCaretVisible();
			return 0;
		case EditorCommand::DeleteBackNotLine:
			DelCharBack(false);
			AutoCompleteCharacterDeleted();
			EnsureCaretVisible();
			return 0;
		case EditorCommand::Tab:
			AutoCompleteCompleted(0, CompletionMethods::Tab);
			return 0;
		case EditorCommand::NewLine:
			AutoCompleteCompleted(0, CompletionMethods::Newline);
			return 0;

		default:
			AutoCompleteCancel();
		}
	}

	if (ct.inCallTipMode) {
		if (
		    (command != EditorCommand::CharLeft) &&
		    (command != EditorCommand::CharLeftExtend) &&
		    (command != EditorCommand::CharRight) &&
		    (command != EditorCommand::CharRightExtend) &&
		    (command != EditorCommand::EditToggleOvertype) &&
		    (command != EditorCommand::DeleteBack) &&
		    (command != EditorCommand::DeleteBackNotLine)
		) {
			ct.CallTipCancel();
		}
		if ((command == EditorCommand::DeleteBack) || (command == EditorCommand::DeleteBackNotLine)) {
			if (sel.MainCaret() <= ct.posStartCallTip) {
				ct.CallTipCancel();
			}
		}
	}
	return Editor::ExecuteCommand(command);
}

void ScintillaBase::ListNotify(ListBoxEvent *plbe) {
	switch (plbe->event) {
	case ListBoxEvent::EventType::selectionChange:
		AutoCompleteSelection();
		break;
	case ListBoxEvent::EventType::doubleClick:
		AutoCompleteCompleted(0, CompletionMethods::DoubleClick);
		break;
	}
}

void ScintillaBase::MoveImeCarets(Sci::Position offset) noexcept {
	// Move carets relatively by bytes.
	for (size_t r = 0; r < sel.Count(); r++) {
		const Sci::Position positionInsert = sel.Range(r).Start().Position();
		sel.Range(r) = SelectionRange(positionInsert + offset);
	}
}

void ScintillaBase::DrawImeIndicator(int indicator, Sci::Position len) {
	// Emulate the visual style of IME characters with indicators.
	// Draw an indicator on the character before caret by the character bytes of len
	// so it should be called after InsertCharacter().
	// It does not affect caret positions.
	const IndicatorNumbers ind = static_cast<IndicatorNumbers>(indicator);
	if (ind < IndicatorNumbers::Container || ind > IndicatorNumbers::Max) {
		return;
	}
	pdoc->DecorationSetCurrentIndicator(indicator);
	for (size_t r = 0; r < sel.Count(); r++) {
		const Sci::Position positionInsert = sel.Range(r).Start().Position();
		pdoc->DecorationFillRange(positionInsert - len, 1, len);
	}
}

bool ScintillaBase::ShouldDisplayPopup(Point ptInWindowCoordinates) const {
	return (displayPopupMenu == PopUp::All ||
		(displayPopupMenu == PopUp::Text && !PointInSelMargin(ptInWindowCoordinates)));
}

void ScintillaBase::ContextMenu(Point pt) {
	if (displayPopupMenu != PopUp::Never) {
		// Named editor state only — no temporary message path for menu enablement.
		const bool writable = !GetReadOnly();
		popup.CreatePopUp();
		AddToPopUp("Undo", idcmdUndo, writable && pdoc->CanUndo());
		AddToPopUp("Redo", idcmdRedo, writable && pdoc->CanRedo());
		AddToPopUp("");
		AddToPopUp("Cut", idcmdCut, writable && !sel.Empty());
		AddToPopUp("Copy", idcmdCopy, !sel.Empty());
		AddToPopUp("Paste", idcmdPaste, writable && CanPaste());
		AddToPopUp("Delete", idcmdDelete, writable && !sel.Empty());
		AddToPopUp("");
		AddToPopUp("Select All", idcmdSelectAll);
		popup.Show(pt, wMain);
	}
}

void ScintillaBase::CancelModes() {
	AutoCompleteCancel();
	ct.CallTipCancel();
	Editor::CancelModes();
}

void ScintillaBase::ButtonDownWithModifiers(Point pt, unsigned int curTime, KeyMod modifiers) {
	CancelModes();
	Editor::ButtonDownWithModifiers(pt, curTime, modifiers);
}

void ScintillaBase::RightButtonDownWithModifiers(Point pt, unsigned int curTime, KeyMod modifiers) {
	CancelModes();
	Editor::RightButtonDownWithModifiers(pt, curTime, modifiers);
}

// LexState, DocumentLexState, NotifyStyleToNeeded, and named lexer
// operations: definitions in EditorLexing.cxx.
