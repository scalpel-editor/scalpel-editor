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
		WndProc(Message::Undo, 0, 0);
		break;

	case idcmdRedo:
		WndProc(Message::Redo, 0, 0);
		break;

	case idcmdCut:
		WndProc(Message::Cut, 0, 0);
		break;

	case idcmdCopy:
		WndProc(Message::Copy, 0, 0);
		break;

	case idcmdPaste:
		WndProc(Message::Paste, 0, 0);
		break;

	case idcmdDelete:
		WndProc(Message::Clear, 0, 0);
		break;

	case idcmdSelectAll:
		WndProc(Message::SelectAll, 0, 0);
		break;

	default:
		break;
	}
}

int ScintillaBase::KeyCommand(Message iMessage) {
	// Most key commands cancel autocompletion mode
	if (ac.Active()) {
		switch (iMessage) {
			// Except for these
		case Message::LineDown:
			AutoCompleteMove(1);
			return 0;
		case Message::LineUp:
			AutoCompleteMove(-1);
			return 0;
		case Message::PageDown:
			AutoCompleteMove(ac.lb->GetVisibleRows());
			return 0;
		case Message::PageUp:
			AutoCompleteMove(-ac.lb->GetVisibleRows());
			return 0;
		case Message::VCHome:
			AutoCompleteMove(-5000);
			return 0;
		case Message::LineEnd:
			AutoCompleteMove(5000);
			return 0;
		case Message::DeleteBack:
			DelCharBack(true);
			AutoCompleteCharacterDeleted();
			EnsureCaretVisible();
			return 0;
		case Message::DeleteBackNotLine:
			DelCharBack(false);
			AutoCompleteCharacterDeleted();
			EnsureCaretVisible();
			return 0;
		case Message::Tab:
			AutoCompleteCompleted(0, CompletionMethods::Tab);
			return 0;
		case Message::NewLine:
			AutoCompleteCompleted(0, CompletionMethods::Newline);
			return 0;

		default:
			AutoCompleteCancel();
		}
	}

	if (ct.inCallTipMode) {
		if (
		    (iMessage != Message::CharLeft) &&
		    (iMessage != Message::CharLeftExtend) &&
		    (iMessage != Message::CharRight) &&
		    (iMessage != Message::CharRightExtend) &&
		    (iMessage != Message::EditToggleOvertype) &&
		    (iMessage != Message::DeleteBack) &&
		    (iMessage != Message::DeleteBackNotLine)
		) {
			ct.CallTipCancel();
		}
		if ((iMessage == Message::DeleteBack) || (iMessage == Message::DeleteBackNotLine)) {
			if (sel.MainCaret() <= ct.posStartCallTip) {
				ct.CallTipCancel();
			}
		}
	}
	return Editor::KeyCommand(iMessage);
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
		const bool writable = !WndProc(Message::GetReadOnly, 0, 0);
		popup.CreatePopUp();
		AddToPopUp("Undo", idcmdUndo, writable && pdoc->CanUndo());
		AddToPopUp("Redo", idcmdRedo, writable && pdoc->CanRedo());
		AddToPopUp("");
		AddToPopUp("Cut", idcmdCut, writable && !sel.Empty());
		AddToPopUp("Copy", idcmdCopy, !sel.Empty());
		AddToPopUp("Paste", idcmdPaste, writable && WndProc(Message::CanPaste, 0, 0));
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

namespace Scintilla::Internal {

class LexState : public LexInterface {
public:
	explicit LexState(Document *pdoc_) noexcept;

	// LexInterface deleted the standard operators and defined the virtual destructor so don't need to here.

	const char *DescribeWordListSets();
	void SetWordList(int n, const char *wl);
	[[nodiscard]] int GetIdentifier() const;
	[[nodiscard]] const char *GetName() const;
	void *PrivateCall(int operation, void *pointer);
	const char *PropertyNames();
	TypeProperty PropertyType(const char *name);
	const char *DescribeProperty(const char *name);
	void PropSet(const char *key, const char *val);
	const char *PropGet(const char *key) const;
	int PropGetInt(const char *key, int defaultValue=0) const;

	LineEndType LineEndTypesSupported() override;
	int AllocateSubStyles(int styleBase, int numberStyles);
	int SubStylesStart(int styleBase);
	int SubStylesLength(int styleBase);
	int StyleFromSubStyle(int subStyle);
	int PrimaryStyleFromStyle(int style);
	void FreeSubStyles();
	void SetIdentifiers(int style, const char *identifiers);
	int DistanceToSecondaryStyles();
	const char *GetSubStyleBases();
	int NamedStyles();
	const char *NameOfStyle(int style);
	const char *TagsOfStyle(int style);
	const char *DescriptionOfStyle(int style);
};

}

LexState::LexState(Document *pdoc_) noexcept : LexInterface(pdoc_) {
}

LexState *ScintillaBase::DocumentLexState() {
	if (!pdoc->GetLexInterface()) {
		pdoc->SetLexInterface(std::make_unique<LexState>(pdoc));
	}
	return dynamic_cast<LexState *>(pdoc->GetLexInterface());
}

const char *LexState::DescribeWordListSets() {
	if (instance) {
		return instance->DescribeWordListSets();
	}
	return nullptr;
}

void LexState::SetWordList(int n, const char *wl) {
	if (instance) {
		const Sci_Position firstModification = instance->WordListSet(n, wl);
		if (firstModification >= 0) {
			pdoc->ModifiedAt(firstModification);
		}
	}
}

int LexState::GetIdentifier() const {
	if (instance) {
		return instance->GetIdentifier();
	}
	return 0;
}

const char *LexState::GetName() const {
	if (instance) {
		return instance->GetName();
	}
	return "";
}

void *LexState::PrivateCall(int operation, void *pointer) {
	if (instance) {
		return instance->PrivateCall(operation, pointer);
	}
	return nullptr;
}

const char *LexState::PropertyNames() {
	if (instance) {
		return instance->PropertyNames();
	}
	return nullptr;
}

TypeProperty LexState::PropertyType(const char *name) {
	if (instance) {
		return static_cast<TypeProperty>(instance->PropertyType(name));
	}
	return TypeProperty::Boolean;
}

const char *LexState::DescribeProperty(const char *name) {
	if (instance) {
		return instance->DescribeProperty(name);
	}
	return nullptr;
}

void LexState::PropSet(const char *key, const char *val) {
	if (instance) {
		const Sci_Position firstModification = instance->PropertySet(key, val);
		if (firstModification >= 0) {
			pdoc->ModifiedAt(firstModification);
		}
	}
}

const char *LexState::PropGet(const char *key) const {
	if (instance) {
		return instance->PropertyGet(key);
	}
	return nullptr;
}

int LexState::PropGetInt(const char *key, int defaultValue) const {
	if (instance) {
		const char *value = instance->PropertyGet(key);
		if (value && *value) {
			return atoi(value);
		}
	}
	return defaultValue;
}

LineEndType LexState::LineEndTypesSupported() {
	if (instance) {
		return static_cast<LineEndType>(instance->LineEndTypesSupported());
	}
	return LineEndType::Default;
}

int LexState::AllocateSubStyles(int styleBase, int numberStyles) {
	if (instance) {
		return instance->AllocateSubStyles(styleBase, numberStyles);
	}
	return -1;
}

int LexState::SubStylesStart(int styleBase) {
	if (instance) {
		return instance->SubStylesStart(styleBase);
	}
	return -1;
}

int LexState::SubStylesLength(int styleBase) {
	if (instance) {
		return instance->SubStylesLength(styleBase);
	}
	return 0;
}

int LexState::StyleFromSubStyle(int subStyle) {
	if (instance) {
		return instance->StyleFromSubStyle(subStyle);
	}
	return 0;
}

int LexState::PrimaryStyleFromStyle(int style) {
	if (instance) {
		return instance->PrimaryStyleFromStyle(style);
	}
	return 0;
}

void LexState::FreeSubStyles() {
	if (instance) {
		instance->FreeSubStyles();
	}
}

void LexState::SetIdentifiers(int style, const char *identifiers) {
	if (instance) {
		instance->SetIdentifiers(style, identifiers);
		pdoc->ModifiedAt(0);
	}
}

int LexState::DistanceToSecondaryStyles() {
	if (instance) {
		return instance->DistanceToSecondaryStyles();
	}
	return 0;
}

const char *LexState::GetSubStyleBases() {
	if (instance) {
		return instance->GetSubStyleBases();
	}
	return "";
}

int LexState::NamedStyles() {
	if (instance) {
		return instance->NamedStyles();
	}
	return -1;
}

const char *LexState::NameOfStyle(int style) {
	if (instance) {
		return instance->NameOfStyle(style);
	}
	return nullptr;
}

const char *LexState::TagsOfStyle(int style) {
	if (instance) {
		return instance->TagsOfStyle(style);
	}
	return nullptr;
}

const char *LexState::DescriptionOfStyle(int style) {
	if (instance) {
		return instance->DescriptionOfStyle(style);
	}
	return nullptr;
}

void ScintillaBase::NotifyStyleToNeeded(Sci::Position endStyleNeeded) {
	if (!DocumentLexState()->UseContainerLexing()) {
		const Sci::Position startStyling = pdoc->LineStartPosition(pdoc->GetEndStyled());
		DocumentLexState()->Colourise(startStyling, endStyleNeeded);
		return;
	}
	Editor::NotifyStyleToNeeded(endStyleNeeded);
}

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
		displayPopupMenu = static_cast<PopUp>(wParam);
		break;

	case Message::GetLexer:
		return DocumentLexState()->GetIdentifier();

	case Message::SetILexer:
		DocumentLexState()->SetInstance(static_cast<ILexer5 *>(PtrFromSPtr(lParam)));
		return 0;

	case Message::Colourise:
		if (DocumentLexState()->UseContainerLexing()) {
			pdoc->ModifiedAt(PositionFromUPtr(wParam));
			NotifyStyleToNeeded((lParam == -1) ? pdoc->Length() : lParam);
		} else {
			DocumentLexState()->Colourise(PositionFromUPtr(wParam), lParam);
		}
		Redraw();
		break;

	case Message::SetProperty:
		DocumentLexState()->PropSet(ConstCharPtrFromUPtr(wParam),
		          ConstCharPtrFromSPtr(lParam));
		break;

	case Message::GetProperty:
		return StringResult(lParam, DocumentLexState()->PropGet(ConstCharPtrFromUPtr(wParam)));

	case Message::GetPropertyExpanded:
		return StringResult(lParam, DocumentLexState()->PropGet(ConstCharPtrFromUPtr(wParam)));

	case Message::GetPropertyInt:
		return DocumentLexState()->PropGetInt(ConstCharPtrFromUPtr(wParam), static_cast<int>(lParam));

	case Message::SetKeyWords:
		DocumentLexState()->SetWordList(static_cast<int>(wParam), ConstCharPtrFromSPtr(lParam));
		break;

	case Message::GetLexerLanguage:
		return StringResult(lParam, DocumentLexState()->GetName());

	case Message::PrivateLexerCall:
		return reinterpret_cast<sptr_t>(
			DocumentLexState()->PrivateCall(static_cast<int>(wParam), PtrFromSPtr(lParam)));

#ifdef INCLUDE_DEPRECATED_FEATURES
	case SCI_GETSTYLEBITSNEEDED:
		return 8;
#endif

	case Message::PropertyNames:
		return StringResult(lParam, DocumentLexState()->PropertyNames());

	case Message::PropertyType:
		return static_cast<sptr_t>(DocumentLexState()->PropertyType(ConstCharPtrFromUPtr(wParam)));

	case Message::DescribeProperty:
		return StringResult(lParam,
				    DocumentLexState()->DescribeProperty(ConstCharPtrFromUPtr(wParam)));

	case Message::DescribeKeyWordSets:
		return StringResult(lParam, DocumentLexState()->DescribeWordListSets());

	case Message::GetLineEndTypesSupported:
		return static_cast<sptr_t>(DocumentLexState()->LineEndTypesSupported());

	case Message::AllocateSubStyles:
		return DocumentLexState()->AllocateSubStyles(static_cast<int>(wParam), static_cast<int>(lParam));

	case Message::GetSubStylesStart:
		return DocumentLexState()->SubStylesStart(static_cast<int>(wParam));

	case Message::GetSubStylesLength:
		return DocumentLexState()->SubStylesLength(static_cast<int>(wParam));

	case Message::GetStyleFromSubStyle:
		return DocumentLexState()->StyleFromSubStyle(static_cast<int>(wParam));

	case Message::GetPrimaryStyleFromStyle:
		return DocumentLexState()->PrimaryStyleFromStyle(static_cast<int>(wParam));

	case Message::FreeSubStyles:
		DocumentLexState()->FreeSubStyles();
		break;

	case Message::SetIdentifiers:
		DocumentLexState()->SetIdentifiers(static_cast<int>(wParam),
						   ConstCharPtrFromSPtr(lParam));
		break;

	case Message::DistanceToSecondaryStyles:
		return DocumentLexState()->DistanceToSecondaryStyles();

	case Message::GetSubStyleBases:
		return StringResult(lParam, DocumentLexState()->GetSubStyleBases());

	case Message::GetNamedStyles:
		return DocumentLexState()->NamedStyles();

	case Message::NameOfStyle:
		return StringResult(lParam, DocumentLexState()->
				    NameOfStyle(static_cast<int>(wParam)));

	case Message::TagsOfStyle:
		return StringResult(lParam, DocumentLexState()->
				    TagsOfStyle(static_cast<int>(wParam)));

	case Message::DescriptionOfStyle:
		return StringResult(lParam, DocumentLexState()->
				    DescriptionOfStyle(static_cast<int>(wParam)));

	default:
		return Editor::WndProc(iMessage, wParam, lParam);
	}
	return 0;
}
