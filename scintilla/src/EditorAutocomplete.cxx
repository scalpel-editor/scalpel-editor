// Scintilla source code edit control
/** @file EditorAutocomplete.cxx
 ** Autocomplete and user-list concern for ScintillaBase: show and cancel the
 ** list, option state, completion, images, and selection notifications.
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
// Displays a completion list. lengthEntered is how many characters of the
// word are already typed; itemList is words separated by the current
// separator (space by default). Showing a list cancels any active call tip.
// With choose-single on and one list item, that item is inserted without
// showing the list and AutoCCompleted is sent.
void ScintillaBase::AutoCShow(Sci::Position lengthEntered, const char *itemList) {
	listType = 0;
	AutoCompleteStart(lengthEntered, itemList);
}

// Hides the list without AutoCCancelled. That notification is sent by
// AutoCompleteCancel (CancelModes, stop characters, and similar paths).
void ScintillaBase::AutoCCancel() {
	ac.Cancel();
}

bool ScintillaBase::AutoCActive() const noexcept {
	return ac.Active();
}

// Document position of the caret when the list was shown.
Sci::Position ScintillaBase::AutoCPosStart() const noexcept {
	return ac.posStart;
}

// Completes with the current selection, same as Tab while the list is active.
void ScintillaBase::AutoCComplete() {
	AutoCompleteCompleted(0, CompletionMethods::Command);
}

// Characters that cancel the list when typed. Empty by default.
void ScintillaBase::AutoCStops(const char *characterSet) {
	ac.SetStopChars(characterSet);
}

void ScintillaBase::AutoCSetSeparator(char separatorCharacter) {
	ac.SetSeparator(separatorCharacter);
}

char ScintillaBase::AutoCGetSeparator() const noexcept {
	return ac.GetSeparator();
}

// Selects the first list item whose text begins with select. Closes the list
// when auto-hide is on and nothing matches.
void ScintillaBase::AutoCSelect(const char *select) {
	ac.Select(select);
}

int ScintillaBase::AutoCGetCurrent() const {
	if (!ac.Active())
		return -1;
	return ac.GetSelection();
}

// Copies the selected item text into buffer (including the trailing NUL) and
// returns the string length without the NUL. Returns 0 when nothing is selected.
int ScintillaBase::AutoCGetCurrentText(char *buffer) const {
	if (ac.Active()) {
		const int item = ac.GetSelection();
		if (item != -1) {
			const std::string selected = ac.GetValue(item);
			if (buffer)
				memcpy(buffer, selected.c_str(), selected.length()+1);
			return static_cast<int>(selected.length());
		}
	}
	if (buffer)
		*buffer = '\0';
	return 0;
}

// When true (default), the list cancels if the caret returns to the position
// where the list opened. When false, cancel only after the caret moves before
// the word being completed.
void ScintillaBase::AutoCSetCancelAtStart(bool cancel) {
	ac.cancelAtStartPos = cancel;
}

bool ScintillaBase::AutoCGetCancelAtStart() const noexcept {
	return ac.cancelAtStartPos;
}

// Characters that complete the current item and are then inserted. Empty by default.
void ScintillaBase::AutoCSetFillUps(const char *characterSet) {
	ac.SetFillUpChars(characterSet);
}

// When true, a one-item list is inserted immediately without showing the popup.
void ScintillaBase::AutoCSetChooseSingle(bool chooseSingle) {
	ac.chooseSingle = chooseSingle;
}

bool ScintillaBase::AutoCGetChooseSingle() const noexcept {
	return ac.chooseSingle;
}

void ScintillaBase::AutoCSetIgnoreCase(bool ignoreCase) {
	ac.ignoreCase = ignoreCase;
}

bool ScintillaBase::AutoCGetIgnoreCase() const noexcept {
	return ac.ignoreCase;
}

void ScintillaBase::AutoCSetCaseInsensitiveBehaviour(CaseInsensitiveBehaviour behaviour) {
	ac.ignoreCaseBehaviour = behaviour;
}

CaseInsensitiveBehaviour ScintillaBase::AutoCGetCaseInsensitiveBehaviour() const noexcept {
	return ac.ignoreCaseBehaviour;
}

// Once inserts only into the main selection; Each inserts into every selection.
void ScintillaBase::AutoCSetMulti(MultiAutoComplete multi) {
	multiAutoCMode = multi;
}

MultiAutoComplete ScintillaBase::AutoCGetMulti() const noexcept {
	return multiAutoCMode;
}

// PreSorted expects an alphabetically sorted list; PerformSort sorts on show;
// Custom keeps application order and builds a sorted index for matching.
void ScintillaBase::AutoCSetOrder(Ordering order) {
	ac.autoSort = order;
}

Ordering ScintillaBase::AutoCGetOrder() const noexcept {
	return ac.autoSort;
}

// Same list machinery as AutoCShow, but listType_ must be greater than zero so
// completion reports UserListSelection instead of AutoCSelection and does not
// insert text. Choose-single has no effect on user lists.
void ScintillaBase::UserListShow(int listType_, const char *itemList) {
	listType = listType_;
	AutoCompleteStart(0, itemList);
}

// When true (default), the list closes when typing leaves no matching item.
void ScintillaBase::AutoCSetAutoHide(bool autoHide) {
	ac.autoHide = autoHide;
}

bool ScintillaBase::AutoCGetAutoHide() const noexcept {
	return ac.autoHide;
}

void ScintillaBase::AutoCSetOptions(AutoCompleteOption options) {
	ac.options = options;
}

AutoCompleteOption ScintillaBase::AutoCGetOptions() const noexcept {
	return ac.options;
}

// When true, completing deletes word characters after the caret first.
void ScintillaBase::AutoCSetDropRestOfWord(bool dropRestOfWord) {
	ac.dropRestOfWord = dropRestOfWord;
}

bool ScintillaBase::AutoCGetDropRestOfWord() const noexcept {
	return ac.dropRestOfWord;
}

// Maximum visible rows; more items show a vertical scrollbar. Default is 5.
void ScintillaBase::AutoCSetMaxHeight(int rowCount) {
	ac.lb->SetVisibleRows(rowCount);
}

int ScintillaBase::AutoCGetMaxHeight() const {
	return ac.lb->GetVisibleRows();
}

// Maximum list width in average character widths of the list style. Zero
// (default) sizes to the longest item.
void ScintillaBase::AutoCSetMaxWidth(int characterCount) {
	maxListWidth = characterCount;
}

int ScintillaBase::AutoCGetMaxWidth() const noexcept {
	return maxListWidth;
}

// Style used for the list font. Defaults to STYLE_DEFAULT.
void ScintillaBase::AutoCSetStyle(int style) {
	vs.autocStyle = style;
	InvalidateStyleRedraw();
}

int ScintillaBase::AutoCGetStyle() const noexcept {
	return vs.autocStyle;
}

// Scale factor for list images in percent (100 is native size).
void ScintillaBase::AutoCSetImageScale(int scalePercent) {
	ac.imageScale = static_cast<float>(scalePercent) / 100.0f;
}

int ScintillaBase::AutoCGetImageScale() const noexcept {
	return static_cast<int>(ac.imageScale * 100);
}

// Registers an XPM image under type for list entries that use that type id.
void ScintillaBase::RegisterImage(int type, const char *xpmData) {
	ac.lb->RegisterImage(type, xpmData);
}

// Registers an RGBA pixel buffer under type. Width and height come from the
// last RGBAImageSetWidth / RGBAImageSetHeight values on the editor.
void ScintillaBase::RegisterRGBAImage(int type, const unsigned char *pixels) {
	ac.lb->RegisterRGBAImage(type, static_cast<int>(sizeRGBAImage.x), static_cast<int>(sizeRGBAImage.y),
		pixels);
}

void ScintillaBase::ClearRegisteredImages() {
	ac.lb->ClearRegisteredImages();
}

// Separates the display text from a type integer in list items (default '?').
void ScintillaBase::AutoCSetTypeSeparator(char separatorCharacter) {
	ac.SetTypesep(separatorCharacter);
}

char ScintillaBase::AutoCGetTypeSeparator() const noexcept {
	return ac.GetTypesep();
}

void ScintillaBase::AutoCompleteInsert(Sci::Position startPos, Sci::Position removeLen, std::string_view text) {
	UndoGroup ug(pdoc);
	if (multiAutoCMode == MultiAutoComplete::Once) {
		pdoc->DeleteChars(startPos, removeLen);
		const Sci::Position lengthInserted = pdoc->InsertString(startPos, text);
		SetEmptySelection(startPos + lengthInserted);
	} else {
		// MultiAutoComplete::Each
		for (size_t r=0; r<sel.Count(); r++) {
			if (!RangeContainsProtected(sel.Range(r))) {
				Sci::Position positionInsert = sel.Range(r).Start().Position();
				positionInsert = RealizeVirtualSpace(positionInsert, sel.Range(r).caret.VirtualSpace());
				if (positionInsert - removeLen >= 0) {
					positionInsert -= removeLen;
					pdoc->DeleteChars(positionInsert, removeLen);
				}
				const Sci::Position lengthInserted = pdoc->InsertString(positionInsert, text);
				if (lengthInserted > 0) {
					sel.Range(r) = SelectionRange(positionInsert + lengthInserted);
				}
				sel.Range(r).ClearVirtualSpace();
			}
		}
	}
}

void ScintillaBase::AutoCompleteStart(Sci::Position lenEntered, const char *list) {
	//Platform::DebugPrintf("AutoComplete %s\n", list);
	ct.CallTipCancel();

	if (ac.chooseSingle && (listType == 0)) {
		if (list && !strchr(list, ac.GetSeparator())) {
			// list contains just one item so choose it
			const std::string_view item(list);
			const std::string_view choice = item.substr(0, item.find_first_of(ac.GetTypesep()));
			if (ac.ignoreCase) {
				// May need to convert the case before invocation, so remove lenEntered characters
				AutoCompleteInsert(sel.MainCaret() - lenEntered, lenEntered, choice);
			} else {
				AutoCompleteInsert(sel.MainCaret(), 0, choice.substr(lenEntered));
			}
			const Sci::Position firstPos = sel.MainCaret() - lenEntered;
			// Construct a string with a NUL at end as that is expected by applications
			const std::string selected(choice);
			AutoCompleteNotifyCompleted('\0', CompletionMethods::SingleChoice, firstPos, selected.c_str());

			ac.Cancel();
			return;
		}
	}

	const ListOptions options{
		vs.ElementColour(Element::List),
		vs.ElementColour(Element::ListBack),
		vs.ElementColour(Element::ListSelected),
		vs.ElementColour(Element::ListSelectedBack),
		ac.options,
		ac.imageScale,
	};

	int lineHeight;
	if (vs.autocStyle != StyleDefault) {
		AutoSurface surfaceMeasure(this);
		lineHeight = static_cast<int>(std::lround(surfaceMeasure->Height(vs.styles[vs.autocStyle].font.get())));
	} else {
		lineHeight = vs.lineHeight;
	}

	ac.Start(wMain, idAutoComplete, sel.MainCaret(), PointMainCaret(),
				lenEntered, lineHeight, technology, options);

	const PRectangle rcClient = GetClientRectangle();
	Point pt = LocationFromPosition(sel.MainCaret() - lenEntered);
	PRectangle rcPopupBounds = wMain.GetMonitorRect(pt);
	if (rcPopupBounds.Height() == 0)
		rcPopupBounds = rcClient;

	int heightLB = ac.heightLBDefault;
	int widthLB = ac.widthLBDefault;
	if (pt.x >= rcClient.right - widthLB) {
		HorizontalScrollTo(static_cast<int>(xOffset + pt.x - rcClient.right + widthLB));
		Redraw();
		pt = PointMainCaret();
	}
	if (wMargin.Created()) {
		pt = pt + GetVisibleOriginInMain();
	}
	PRectangle rcac;
	rcac.left = pt.x - ac.lb->CaretFromEdge();
	if (pt.y >= rcPopupBounds.bottom - heightLB &&  // Won't fit below.
	        pt.y >= (rcPopupBounds.bottom + rcPopupBounds.top) / 2) { // and there is more room above.
		rcac.top = pt.y - heightLB;
		if (rcac.top < rcPopupBounds.top) {
			heightLB -= static_cast<int>(rcPopupBounds.top - rcac.top);
			rcac.top = rcPopupBounds.top;
		}
	} else {
		rcac.top = pt.y + vs.lineHeight;
	}
	rcac.right = rcac.left + widthLB;
	rcac.bottom = static_cast<XYPOSITION>(std::min(static_cast<int>(rcac.top) + heightLB, static_cast<int>(rcPopupBounds.bottom)));
	ac.lb->SetPositionRelative(rcac, &wMain);
	ac.lb->SetFont(vs.styles[vs.autocStyle].font.get());
	const int aveCharWidth = static_cast<int>(vs.styles[vs.autocStyle].aveCharWidth);
	ac.lb->SetAverageCharWidth(aveCharWidth);
	ac.lb->SetDelegate(this);

	ac.SetList(list ? list : "");

	// Fiddle the position of the list so it is right next to the target and wide enough for all its strings
	PRectangle rcList = ac.lb->GetDesiredRect();
	const int heightAlloced = static_cast<int>(rcList.bottom - rcList.top);
	widthLB = std::max(widthLB, static_cast<int>(rcList.right - rcList.left));
	if (maxListWidth != 0)
		widthLB = std::min(widthLB, aveCharWidth*maxListWidth);
	// Make an allowance for large strings in list
	rcList.left = pt.x - ac.lb->CaretFromEdge();
	rcList.right = rcList.left + widthLB;
	if (((pt.y + vs.lineHeight) >= (rcPopupBounds.bottom - heightAlloced)) &&  // Won't fit below.
	        ((pt.y + vs.lineHeight / 2) >= (rcPopupBounds.bottom + rcPopupBounds.top) / 2)) { // and there is more room above.
		rcList.top = pt.y - heightAlloced;
	} else {
		rcList.top = pt.y + vs.lineHeight;
	}
	rcList.bottom = rcList.top + heightAlloced;
	ac.lb->SetPositionRelative(rcList, &wMain);
	ac.Show(true);
	if (lenEntered != 0) {
		AutoCompleteMoveToCurrentWord();
	}
}

// Cancels with AutoCCancelled when a list is active. Used by CancelModes,
// stop characters, and other internal paths. AutoCCancel does not notify.
void ScintillaBase::AutoCompleteCancel() {
	if (ac.Active()) {
		NotificationData scn = {};
		scn.nmhdr.code = Notification::AutoCCancelled;
		scn.wParam = 0;
		scn.listType = 0;
		NotifyParent(scn);
	}
	ac.Cancel();
}

void ScintillaBase::AutoCompleteMove(int delta) {
	ac.Move(delta);
}

void ScintillaBase::AutoCompleteMoveToCurrentWord() {
	if (FlagSet(ac.options, AutoCompleteOption::SelectFirstItem))
		return;
	std::string wordCurrent = RangeText(ac.posStart - ac.startLen, sel.MainCaret());
	ac.Select(wordCurrent.c_str());
}

// Report the highlighted item without inserting it. text is the item value,
// position is the list start, and listType distinguishes user lists from autocomplete.
void ScintillaBase::AutoCompleteSelection() {
	const int item = ac.GetSelection();
	std::string selected;
	if (item != -1) {
		selected = ac.GetValue(item);
	}

	NotificationData scn = {};
	scn.nmhdr.code = Notification::AutoCSelectionChange;
	scn.message = static_cast<Message>(0);
	scn.wParam = listType;
	scn.listType = listType;
	const Sci::Position firstPos = ac.posStart - ac.startLen;
	scn.position = firstPos;
	scn.lParam = firstPos;
	scn.text = selected.c_str();
	NotifyParent(scn);
}

void ScintillaBase::AutoCompleteCharacterAdded(char ch) {
	if (ac.IsFillUpChar(ch)) {
		AutoCompleteCompleted(ch, CompletionMethods::FillUp);
	} else if (ac.IsStopChar(ch)) {
		AutoCompleteCancel();
	} else {
		AutoCompleteMoveToCurrentWord();
	}
}

// AutoCCharDeleted has no payload beyond its notification code.
void ScintillaBase::AutoCompleteCharacterDeleted() {
	if (sel.MainCaret() < ac.posStart - ac.startLen) {
		AutoCompleteCancel();
	} else if (ac.cancelAtStartPos && (sel.MainCaret() <= ac.posStart)) {
		AutoCompleteCancel();
	} else {
		AutoCompleteMoveToCurrentWord();
	}
	NotificationData scn = {};
	scn.nmhdr.code = Notification::AutoCCharDeleted;
	scn.wParam = 0;
	scn.listType = 0;
	NotifyParent(scn);
}

// Report an insertion after it completes. text and position identify the selected
// item and list start; ch and listCompletionMethod describe how it was accepted.
void ScintillaBase::AutoCompleteNotifyCompleted(char ch, CompletionMethods completionMethod, Sci::Position firstPos, const char *text) {
	NotificationData scn = {};
	scn.nmhdr.code = Notification::AutoCCompleted;
	scn.message = static_cast<Message>(0);
	scn.ch = ch;
	scn.listCompletionMethod = completionMethod;
	scn.wParam = listType;
	scn.listType = listType;
	scn.position = firstPos;
	scn.lParam = firstPos;
	scn.text = text;
	NotifyParent(scn);
}

// Report a selection before insertion so the host may cancel it. User lists use
// UserListSelection and do not insert; the fields match AutoCCompleted otherwise.
void ScintillaBase::AutoCompleteCompleted(char ch, CompletionMethods completionMethod) {
	const int item = ac.GetSelection();
	if (item == -1) {
		AutoCompleteCancel();
		return;
	}
	const std::string selected = ac.GetValue(item);

	ac.Show(false);

	NotificationData scn = {};
	scn.nmhdr.code = listType > 0 ? Notification::UserListSelection : Notification::AutoCSelection;
	scn.message = static_cast<Message>(0);
	scn.ch = ch;
	scn.listCompletionMethod = completionMethod;
	scn.wParam = listType;
	scn.listType = listType;
	const Sci::Position firstPos = ac.posStart - ac.startLen;
	scn.position = firstPos;
	scn.lParam = firstPos;
	scn.text = selected.c_str();
	NotifyParent(scn);

	if (!ac.Active())
		return;
	ac.Cancel();

	if (listType > 0)
		return;

	Sci::Position endPos = sel.MainCaret();
	if (ac.dropRestOfWord)
		endPos = pdoc->ExtendWordSelect(endPos, 1, true);
	if (endPos < firstPos)
		return;
	AutoCompleteInsert(firstPos, endPos - firstPos, selected);
	SetLastXChosen();

	AutoCompleteNotifyCompleted(ch, completionMethod, firstPos, selected.c_str());
}
