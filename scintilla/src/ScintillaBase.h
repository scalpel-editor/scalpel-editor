// Scintilla source code edit control
/** @file ScintillaBase.h
 ** Defines an enhanced subclass of Editor with calltips, autocomplete and context menu.
 **/
// Copyright 1998-2002 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef SCINTILLABASE_H
#define SCINTILLABASE_H

namespace Scintilla::Internal {

// For most platforms (not Cocoa) all IME indicators are drawn in same colour,
// blue, with different patterns.
constexpr ColourRGBA colourIME(0x0, 0x0, 0xffU);

constexpr int IndicatorInput = static_cast<int>(Scintilla::IndicatorNumbers::Ime);
constexpr int IndicatorTarget = IndicatorInput + 1;
constexpr int IndicatorConverted = IndicatorInput + 2;
constexpr int IndicatorUnknown = IndicatorInput + 3;

class LexState;
/**
 */
class ScintillaBase : public Editor, IListBoxDelegate {
protected:
	/** Enumeration of commands and child windows. */
	enum {
		idCallTip=1,
		idAutoComplete=2,

		idcmdUndo=10,
		idcmdRedo=11,
		idcmdCut=12,
		idcmdCopy=13,
		idcmdPaste=14,
		idcmdDelete=15,
		idcmdSelectAll=16
	};

	Scintilla::PopUp displayPopupMenu;
	Menu popup;
	Scintilla::Internal::AutoComplete ac;

	CallTip ct;

	int listType;			///< 0 is an autocomplete list
	int maxListWidth;		/// Maximum width of list, in average character widths
	Scintilla::MultiAutoComplete multiAutoCMode; /// Mode for autocompleting when multiple selections are present

	LexState *DocumentLexState();
	void Colourise(int start, int end);

	ScintillaBase();
	// Deleted so ScintillaBase objects can not be copied.
	ScintillaBase(const ScintillaBase &) = delete;
	ScintillaBase(ScintillaBase &&) = delete;
	ScintillaBase &operator=(const ScintillaBase &) = delete;
	ScintillaBase &operator=(ScintillaBase &&) = delete;
	// ~ScintillaBase() in public section
	void Initialise() override {}
	void Finalise() override;

	void InsertCharacter(std::string_view sv, Scintilla::CharacterSource charSource) override;
	void Command(int cmdId);
	void CancelModes() override;
	int ExecuteCommand(EditorCommand command) override;

	void MoveImeCarets(Sci::Position offset) noexcept;
	void DrawImeIndicator(int indicator, Sci::Position len);

	// Autocomplete private work — definitions in EditorAutocomplete.cxx
	void AutoCompleteInsert(Sci::Position startPos, Sci::Position removeLen, std::string_view text);
	void AutoCompleteStart(Sci::Position lenEntered, const char *list);
	void AutoCompleteCancel();
	void AutoCompleteMove(int delta);
	void AutoCompleteCharacterAdded(char ch);
	void AutoCompleteCharacterDeleted();
	void AutoCompleteNotifyCompleted(char ch, CompletionMethods completionMethod, Sci::Position firstPos, const char *text);
	void AutoCompleteCompleted(char ch, Scintilla::CompletionMethods completionMethod);
	void AutoCompleteMoveToCurrentWord();
	void AutoCompleteSelection();
	void ListNotify(ListBoxEvent *plbe) override;

	// Call tip private work — definitions in EditorCallTips.cxx
	void CallTipClick();
	void CallTipShow(Point pt, const char *defn);
	virtual void CreateCallTipWindow(PRectangle rc) = 0;

	virtual void AddToPopUp(const char *label, int cmd=0, bool enabled=true) = 0;
	bool ShouldDisplayPopup(Point ptInWindowCoordinates) const;
	void ContextMenu(Point pt);

	void ButtonDownWithModifiers(Point pt, unsigned int curTime, Scintilla::KeyMod modifiers) override;
	void RightButtonDownWithModifiers(Point pt, unsigned int curTime, Scintilla::KeyMod modifiers) override;

	void NotifyStyleToNeeded(Sci::Position endStyleNeeded) override;

public:
	~ScintillaBase() override;

	// Autocomplete and user-list operations — descriptions live beside the
	// definitions in EditorAutocomplete.cxx.
	void AutoCShow(Sci::Position lengthEntered, const char *itemList);
	void AutoCCancel();
	bool AutoCActive() const noexcept;
	Sci::Position AutoCPosStart() const noexcept;
	void AutoCComplete();
	void AutoCStops(const char *characterSet);
	void AutoCSetSeparator(char separatorCharacter);
	char AutoCGetSeparator() const noexcept;
	void AutoCSelect(const char *select);
	int AutoCGetCurrent() const;
	int AutoCGetCurrentText(char *buffer) const;
	void AutoCSetCancelAtStart(bool cancel);
	bool AutoCGetCancelAtStart() const noexcept;
	void AutoCSetFillUps(const char *characterSet);
	void AutoCSetChooseSingle(bool chooseSingle);
	bool AutoCGetChooseSingle() const noexcept;
	void AutoCSetIgnoreCase(bool ignoreCase);
	bool AutoCGetIgnoreCase() const noexcept;
	void AutoCSetCaseInsensitiveBehaviour(Scintilla::CaseInsensitiveBehaviour behaviour);
	Scintilla::CaseInsensitiveBehaviour AutoCGetCaseInsensitiveBehaviour() const noexcept;
	void AutoCSetMulti(Scintilla::MultiAutoComplete multi);
	Scintilla::MultiAutoComplete AutoCGetMulti() const noexcept;
	void AutoCSetOrder(Scintilla::Ordering order);
	Scintilla::Ordering AutoCGetOrder() const noexcept;
	void UserListShow(int listType_, const char *itemList);
	void AutoCSetAutoHide(bool autoHide);
	bool AutoCGetAutoHide() const noexcept;
	void AutoCSetOptions(Scintilla::AutoCompleteOption options);
	Scintilla::AutoCompleteOption AutoCGetOptions() const noexcept;
	void AutoCSetDropRestOfWord(bool dropRestOfWord);
	bool AutoCGetDropRestOfWord() const noexcept;
	void AutoCSetMaxHeight(int rowCount);
	int AutoCGetMaxHeight() const;
	void AutoCSetMaxWidth(int characterCount);
	int AutoCGetMaxWidth() const noexcept;
	void AutoCSetStyle(int style);
	int AutoCGetStyle() const noexcept;
	void AutoCSetImageScale(int scalePercent);
	int AutoCGetImageScale() const noexcept;
	void RegisterImage(int type, const char *xpmData);
	void RegisterRGBAImage(int type, const unsigned char *pixels);
	void ClearRegisteredImages();
	void AutoCSetTypeSeparator(char separatorCharacter);
	char AutoCGetTypeSeparator() const noexcept;

	// Call tip operations — descriptions live beside the definitions in
	// EditorCallTips.cxx.
	void CallTipShow(Sci::Position pos, const char *definition);
	void CallTipCancel();
	bool CallTipActive() const noexcept;
	Sci::Position CallTipPosStart() const noexcept;
	void CallTipSetPosStart(Sci::Position posStart);
	void CallTipSetHlt(Sci::Position highlightStart, Sci::Position highlightEnd);
	void CallTipSetBack(ColourRGBA back);
	void CallTipSetFore(ColourRGBA fore);
	void CallTipSetForeHlt(ColourRGBA fore);
	void CallTipUseStyle(int tabSize);
	void CallTipSetPosition(bool above);

	// Context-menu policy; definition in EditorInput.cxx with the other input surface.
	void UsePopUp(Scintilla::PopUp popUpMode);
	Scintilla::PopUp GetUsePopUp() const noexcept;

	// Public so scintilla_send_message can use it
	Scintilla::sptr_t WndProc(Scintilla::Message iMessage, Scintilla::uptr_t wParam, Scintilla::sptr_t lParam) override;
};

}

#endif
