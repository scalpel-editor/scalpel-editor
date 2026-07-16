// Scintilla source code edit control
/** @file ScintillaBase.cxx
 ** ScintillaBase shell: context menu, IME helpers, lexer glue, and temporary
 ** message forwarding. Autocomplete lives in EditorAutocomplete.cxx; call tips
 ** live in EditorCallTips.cxx.
 **/
// Copyright 1998-2003 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <cmath>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <algorithm>
#include <memory>

#include "ScintillaTypes.h"
#include "ScintillaMessages.h"
#include "ScintillaStructures.h"
#include "ILoader.h"
#include "ILexer.h"

#include "Debugging.h"
#include "Geometry.h"
#include "Platform.h"

#include "CharacterCategoryMap.h"

#include "Position.h"
#include "UniqueString.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "ContractionState.h"
#include "CellBuffer.h"
#include "CallTip.h"
#include "KeyMap.h"
#include "Indicator.h"
#include "LineMarker.h"
#include "Style.h"
#include "ViewStyle.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "Document.h"
#include "Selection.h"
#include "PositionCache.h"
#include "EditModel.h"
#include "MarginView.h"
#include "EditView.h"
#include "Editor.h"
#include "EditorCommands.h"
#include "AutoComplete.h"
#include "ScintillaBase.h"

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


sptr_t ScintillaBase::WndProc(Message iMessage, uptr_t wParam, sptr_t lParam) {
	switch (iMessage) {
	// Autocomplete and call tip cases temporarily forward to named methods
	// in EditorAutocomplete.cxx / EditorCallTips.cxx until the message layer
	// is removed in phase 5.
	case Message::AutoCShow:
		AutoCShow(PositionFromUPtr(wParam), ConstCharPtrFromSPtr(lParam));
		break;

	case Message::AutoCCancel:
		AutoCCancel();
		break;

	case Message::AutoCActive:
		return AutoCActive();

	case Message::AutoCPosStart:
		return AutoCPosStart();

	case Message::AutoCComplete:
		AutoCComplete();
		break;

	case Message::AutoCSetSeparator:
		AutoCSetSeparator(static_cast<char>(wParam));
		break;

	case Message::AutoCGetSeparator:
		return AutoCGetSeparator();

	case Message::AutoCStops:
		AutoCStops(ConstCharPtrFromSPtr(lParam));
		break;

	case Message::AutoCSelect:
		AutoCSelect(ConstCharPtrFromSPtr(lParam));
		break;

	case Message::AutoCGetCurrent:
		return AutoCGetCurrent();

	case Message::AutoCGetCurrentText:
		return AutoCGetCurrentText(CharPtrFromSPtr(lParam));

	case Message::AutoCSetCancelAtStart:
		AutoCSetCancelAtStart(wParam != 0);
		break;

	case Message::AutoCGetCancelAtStart:
		return AutoCGetCancelAtStart();

	case Message::AutoCSetFillUps:
		AutoCSetFillUps(ConstCharPtrFromSPtr(lParam));
		break;

	case Message::AutoCSetChooseSingle:
		AutoCSetChooseSingle(wParam != 0);
		break;

	case Message::AutoCGetChooseSingle:
		return AutoCGetChooseSingle();

	case Message::AutoCSetIgnoreCase:
		AutoCSetIgnoreCase(wParam != 0);
		break;

	case Message::AutoCGetIgnoreCase:
		return AutoCGetIgnoreCase();

	case Message::AutoCSetCaseInsensitiveBehaviour:
		AutoCSetCaseInsensitiveBehaviour(static_cast<CaseInsensitiveBehaviour>(wParam));
		break;

	case Message::AutoCGetCaseInsensitiveBehaviour:
		return static_cast<sptr_t>(AutoCGetCaseInsensitiveBehaviour());

	case Message::AutoCSetMulti:
		AutoCSetMulti(static_cast<MultiAutoComplete>(wParam));
		break;

	case Message::AutoCGetMulti:
		return static_cast<sptr_t>(AutoCGetMulti());

	case Message::AutoCSetOrder:
		AutoCSetOrder(static_cast<Ordering>(wParam));
		break;

	case Message::AutoCGetOrder:
		return static_cast<sptr_t>(AutoCGetOrder());

	case Message::UserListShow:
		UserListShow(static_cast<int>(wParam), ConstCharPtrFromSPtr(lParam));
		break;

	case Message::AutoCSetAutoHide:
		AutoCSetAutoHide(wParam != 0);
		break;

	case Message::AutoCGetAutoHide:
		return AutoCGetAutoHide();

	case Message::AutoCSetOptions:
		AutoCSetOptions(static_cast<AutoCompleteOption>(wParam));
		break;

	case Message::AutoCGetOptions:
		return static_cast<sptr_t>(AutoCGetOptions());

	case Message::AutoCSetDropRestOfWord:
		AutoCSetDropRestOfWord(wParam != 0);
		break;

	case Message::AutoCGetDropRestOfWord:
		return AutoCGetDropRestOfWord();

	case Message::AutoCSetMaxHeight:
		AutoCSetMaxHeight(static_cast<int>(wParam));
		break;

	case Message::AutoCGetMaxHeight:
		return AutoCGetMaxHeight();

	case Message::AutoCSetMaxWidth:
		AutoCSetMaxWidth(static_cast<int>(wParam));
		break;

	case Message::AutoCGetMaxWidth:
		return AutoCGetMaxWidth();

	case Message::AutoCSetStyle:
		AutoCSetStyle(static_cast<int>(wParam));
		break;

	case Message::AutoCGetStyle:
		return AutoCGetStyle();

	case Message::AutoCSetImageScale:
		AutoCSetImageScale(static_cast<int>(wParam));
		break;

	case Message::AutoCGetImageScale:
		return AutoCGetImageScale();

	case Message::RegisterImage:
		RegisterImage(static_cast<int>(wParam), ConstCharPtrFromSPtr(lParam));
		break;

	case Message::RegisterRGBAImage:
		RegisterRGBAImage(static_cast<int>(wParam), ConstUCharPtrFromSPtr(lParam));
		break;

	case Message::ClearRegisteredImages:
		ClearRegisteredImages();
		break;

	case Message::AutoCSetTypeSeparator:
		AutoCSetTypeSeparator(static_cast<char>(wParam));
		break;

	case Message::AutoCGetTypeSeparator:
		return AutoCGetTypeSeparator();

	case Message::CallTipShow:
		CallTipShow(PositionFromUPtr(wParam), ConstCharPtrFromSPtr(lParam));
		break;

	case Message::CallTipCancel:
		CallTipCancel();
		break;

	case Message::CallTipActive:
		return CallTipActive();

	case Message::CallTipPosStart:
		return CallTipPosStart();

	case Message::CallTipSetPosStart:
		CallTipSetPosStart(PositionFromUPtr(wParam));
		break;

	case Message::CallTipSetHlt:
		CallTipSetHlt(PositionFromUPtr(wParam), lParam);
		break;

	case Message::CallTipSetBack:
		CallTipSetBack(ColourRGBA::FromIpRGB(SPtrFromUPtr(wParam)));
		break;

	case Message::CallTipSetFore:
		CallTipSetFore(ColourRGBA::FromIpRGB(SPtrFromUPtr(wParam)));
		break;

	case Message::CallTipSetForeHlt:
		CallTipSetForeHlt(ColourRGBA::FromIpRGB(SPtrFromUPtr(wParam)));
		break;

	case Message::CallTipUseStyle:
		CallTipUseStyle(static_cast<int>(wParam));
		break;

	case Message::CallTipSetPosition:
		CallTipSetPosition(wParam != 0);
		break;

	case Message::UsePopUp:
		UsePopUp(static_cast<PopUp>(wParam));
		break;

	case Message::GetLexer:
		return GetLexer();

	case Message::SetILexer:
		SetILexer(static_cast<ILexer5 *>(PtrFromSPtr(lParam)));
		return 0;

	case Message::Colourise:
		Colourise(PositionFromUPtr(wParam), lParam);
		break;

	case Message::SetProperty:
		SetProperty(ConstCharPtrFromUPtr(wParam), ConstCharPtrFromSPtr(lParam));
		break;

	case Message::GetProperty:
		return StringResult(lParam, GetProperty(ConstCharPtrFromUPtr(wParam)));

	case Message::GetPropertyExpanded:
		// Expanded form is not separately implemented; same as GetProperty.
		return StringResult(lParam, GetProperty(ConstCharPtrFromUPtr(wParam)));

	case Message::GetPropertyInt:
		return GetPropertyInt(ConstCharPtrFromUPtr(wParam), static_cast<int>(lParam));

	case Message::SetKeyWords:
		SetKeyWords(static_cast<int>(wParam), ConstCharPtrFromSPtr(lParam));
		break;

	case Message::GetLexerLanguage:
		return StringResult(lParam, GetLexerLanguage());

	case Message::PrivateLexerCall:
		return reinterpret_cast<sptr_t>(
			PrivateLexerCall(static_cast<int>(wParam), PtrFromSPtr(lParam)));

	case Message::PropertyNames:
		return StringResult(lParam, PropertyNames());

	case Message::PropertyType:
		return static_cast<sptr_t>(PropertyType(ConstCharPtrFromUPtr(wParam)));

	case Message::DescribeProperty:
		return StringResult(lParam, DescribeProperty(ConstCharPtrFromUPtr(wParam)));

	case Message::DescribeKeyWordSets:
		return StringResult(lParam, DescribeKeyWordSets());

	case Message::GetLineEndTypesSupported:
		return static_cast<sptr_t>(GetLineEndTypesSupported());

	case Message::AllocateSubStyles:
		return AllocateSubStyles(static_cast<int>(wParam), static_cast<int>(lParam));

	case Message::GetSubStylesStart:
		return GetSubStylesStart(static_cast<int>(wParam));

	case Message::GetSubStylesLength:
		return GetSubStylesLength(static_cast<int>(wParam));

	case Message::GetStyleFromSubStyle:
		return GetStyleFromSubStyle(static_cast<int>(wParam));

	case Message::GetPrimaryStyleFromStyle:
		return GetPrimaryStyleFromStyle(static_cast<int>(wParam));

	case Message::FreeSubStyles:
		FreeSubStyles();
		break;

	case Message::SetIdentifiers:
		SetIdentifiers(static_cast<int>(wParam), ConstCharPtrFromSPtr(lParam));
		break;

	case Message::DistanceToSecondaryStyles:
		return DistanceToSecondaryStyles();

	case Message::GetSubStyleBases:
		return StringResult(lParam, GetSubStyleBases());

	case Message::GetNamedStyles:
		return GetNamedStyles();

	case Message::NameOfStyle:
		return StringResult(lParam, NameOfStyle(static_cast<int>(wParam)));

	case Message::TagsOfStyle:
		return StringResult(lParam, TagsOfStyle(static_cast<int>(wParam)));

	case Message::DescriptionOfStyle:
		return StringResult(lParam, DescriptionOfStyle(static_cast<int>(wParam)));

	default:
		return Editor::WndProc(iMessage, wParam, lParam);
	}
	return 0;
}
