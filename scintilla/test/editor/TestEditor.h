// scalpel-editor test code
/** @file TestEditor.h
 ** Concrete ScintillaBase subclass with an observable in-memory host.
 **/

#ifndef TESTEDITOR_H
#define TESTEDITOR_H

#include "EditorRecording.h"

namespace Scintilla::Internal {

struct TestNotification {
	Scintilla::Notification code = Scintilla::Notification::StyleNeeded;
	Sci::Position position = 0;
	Sci::Position length = 0;
	Sci::Position line = 0;
	int ch = 0;
	int margin = 0;
	int listType = 0;
	int x = 0;
	int y = 0;
	Scintilla::KeyMod modifiers = Scintilla::KeyMod::Norm;
	Scintilla::ModificationFlags modificationType = Scintilla::ModificationFlags::None;
	Scintilla::Update updated = Scintilla::Update::None;
	Scintilla::CompletionMethods listCompletionMethod{};
	Scintilla::CharacterSource characterSource = Scintilla::CharacterSource::DirectInput;
	std::string text;
};

struct TestTickerRequest {
	int reason = 0;
	int milliseconds = 0;
	int tolerance = 0;
	bool started = false;
};

struct TestEditorSnapshot {
	Scintilla::Wrap wrapMode = Scintilla::Wrap::None;
	int horizontalOffset = 0;
	int horizontalScrollUpdates = 0;
	int verticalScrollUpdates = 0;
	int scrollbarChanges = 0;
	int scrollbarReconfigurations = 0;
	Sci::Line scrollbarMaximum = 0;
	Sci::Line scrollbarPage = 0;
	int invalidateAllCount = 0;
	size_t invalidatedRectangles = 0;
	std::vector<Scintilla::Update> updateNotifications;

	bool operator==(const TestEditorSnapshot &other) const noexcept;
};

struct TestEditorObservations {
	int horizontalScrollUpdates = 0;
	int verticalScrollUpdates = 0;
	int scrollbarChanges = 0;
	int scrollbarReconfigurations = 0;
	Sci::Line scrollbarMaximum = 0;
	Sci::Line scrollbarPage = 0;
	int selectionClaims = 0;
	int changeNotifications = 0;
	bool mouseCaptured = false;
	bool idleRequested = false;
	std::string clipboard;
	std::vector<TestNotification> notifications;
	std::vector<TestTickerRequest> tickerRequests;
	std::vector<PRectangle> callTipWindows;
	std::vector<std::string> popupItems;
	// Typed recording sink (phase 4 step 14+). Copies survive the callback.
	std::vector<RecordedAction> recordedActions;
	// When set, NotifyParent replaces InsertCheck text via ChangeInsertion.
	std::optional<std::string> changeInsertionOnInsertCheck;
	// When set, the next Modified notification re-enters replay once.
	std::optional<RecordedAction> replayOnModified;
};

class TestEditor final : public ScintillaBase {
public:
	explicit TestEditor(TestHost &host, PRectangle clientRectangle = PRectangle(0, 0, 640, 480));
	~TestEditor() override;

	void SetClientRectangle(PRectangle clientRectangle);
	PRectangle ClientRectangle() const;
	void SetText(std::string_view text);
	std::string Text() const;
	void SetHorizontalOffset(int offset);
	int HorizontalOffset() const noexcept;
	void InsertInput(std::string_view text);
	int KeyDown(Scintilla::Keys key, Scintilla::KeyMod modifiers, bool *consumed);
	int RunCommand(EditorCommand command);
	Sci::Position CurrentPos() const;
	void MouseDown(Point point, Scintilla::KeyMod modifiers);
	void MouseMove(Point point, Scintilla::KeyMod modifiers);
	void MouseUp(Point point, Scintilla::KeyMod modifiers);
	void AdvanceTime(unsigned int milliseconds) noexcept;
	unsigned int CurrentTime() const noexcept;
	void FlushUpdateNotifications();
	void PaintAll();
	void ClearObservations();
	TestEditorSnapshot Snapshot() const;

	// Typed recording host surface. Appends a full copy of the action so text
	// remains available after the RecordingCallback returns. The constructor
	// installs MakeRecordingCallback as the production sink so StartRecording
	// delivers into observations.recordedActions.
	void OnRecordedAction(const RecordedAction &action);
	RecordingCallback MakeRecordingCallback();

	using Editor::StartRecording;
	using Editor::StopRecording;
	using Editor::IsRecording;
	using Editor::SetRecordingCallback;
	using Editor::ReplayRecordedAction;
	using Editor::ReplayRecordedActions;

	// Parameterized recording entry points (protected on Editor).
	using Editor::AddText;
	using Editor::AppendText;
	using Editor::ClearAll;
	using Editor::InsertText;
	using Editor::ReplaceSel;
	using Editor::SearchAnchor;
	using Editor::SearchText;
	using Editor::SetSelectionMode;
	using Editor::InsertCharacter;
	using Editor::SetEmptySelection;
	using Editor::SetMultipleSelection;
	using Editor::SetSelection;
	using Editor::AddSelection;
	using Editor::SetSelectionNCaret;
	using Editor::GetSelectionNCaret;
	using Editor::SetSelectionNAnchor;
	using Editor::GetSelectionNAnchor;
	using Editor::SetSelectionNStart;
	using Editor::GetSelectionNStart;
	using Editor::SetSelectionNEnd;
	using Editor::GetSelectionNEnd;
	using Editor::SetRectangularSelectionModifier;
	using Editor::GetRectangularSelectionModifier;
	using Editor::TextWidth;
	using Editor::TextHeightPixels;

	// Wrapping concern (protected on Editor); test-only access for focused suites.
	using Editor::WrapCount;
	using Editor::SetWrapVisualFlags;
	using Editor::GetWrapVisualFlags;
	using Editor::SetWrapVisualFlagsLocation;
	using Editor::GetWrapVisualFlagsLocation;
	using Editor::SetWrapStartIndent;
	using Editor::GetWrapStartIndent;
	using Editor::SetWrapIndentMode;
	using Editor::GetWrapIndentMode;

	// Line / index helpers used by focused command and text-boundary suites.
	using Editor::SetUseTabs;
	using Editor::SetTabWidth;
	using Editor::AllocateLineCharacterIndex;
	using Editor::ReleaseLineCharacterIndex;

	// Document mutators and queries (protected on Editor).
	using Editor::GetLength;
	using Editor::GetCharAt;
	using Editor::DeleteRange;
	using Editor::Allocate;
	using Editor::GetRangePointer;
	using Editor::GetGapPosition;
	using Editor::GetTextRange;
	using Editor::GetStyledText;

	// History (protected on Editor).
	using Editor::EmptyUndoBuffer;
	using Editor::SetUndoCollection;
	using Editor::GetUndoCollection;
	using Editor::GetUndoSequence;
	using Editor::SetChangeHistory;
	using Editor::GetChangeHistory;

	// Clipboard options and helpers (protected on Editor).
	using Editor::SelectAll;
	using Editor::CopyRangeToClipboard;
	using Editor::CopyText;
	using Editor::SetPasteConvertEndings;
	using Editor::GetPasteConvertEndings;
	using Editor::SetMultiPaste;
	using Editor::GetMultiPaste;
	using Editor::SetCopySeparator;
	using Editor::GetCopySeparator;

	// Scrolling options (protected on Editor).
	using Editor::SetHScrollBar;
	using Editor::GetHScrollBar;
	using Editor::SetVScrollBar;
	using Editor::GetVScrollBar;
	using Editor::SetScrollWidth;
	using Editor::GetScrollWidth;
	using Editor::SetScrollWidthTracking;
	using Editor::GetScrollWidthTracking;
	using Editor::SetEndAtLastLine;
	using Editor::GetEndAtLastLine;

	// Selection options and helpers (protected on Editor).
	// SetMultipleSelection is already exposed with the recording entry points above.
	using Editor::GetMultipleSelection;
	using Editor::GetSelections;
	using Editor::HideSelection;
	using Editor::GetSelectionHidden;
	using Editor::TargetFromSelection;
	using Editor::SetSelectionSerialized;
	using Editor::GetSelectionSerialized;

	// Caret appearance (protected on Editor).
	using Editor::SetCaretPeriod;
	using Editor::GetCaretPeriod;
	using Editor::SetCaretSticky;
	using Editor::GetCaretSticky;
	using Editor::ToggleCaretSticky;
	using Editor::SetCaretStyle;
	using Editor::GetCaretStyle;
	using Editor::SetCaretWidth;
	using Editor::GetCaretWidth;
	using Editor::SetCaretLineVisible;
	using Editor::GetCaretLineVisible;
	using Editor::SetCaretLineVisibleAlways;
	using Editor::GetCaretLineVisibleAlways;
	using Editor::SetCaretLineFrame;
	using Editor::GetCaretLineFrame;
	using Editor::SetCaretLineHighlightSubLine;
	using Editor::GetCaretLineHighlightSubLine;
	using Editor::SetAdditionalCaretsBlink;
	using Editor::GetAdditionalCaretsBlink;
	using Editor::SetAdditionalCaretsVisible;
	using Editor::GetAdditionalCaretsVisible;

	// Lines / indentation / character classes (protected on Editor).
	using Editor::GetTabWidth;
	using Editor::GetUseTabs;
	using Editor::SetIndent;
	using Editor::GetIndent;
	using Editor::SetTabIndents;
	using Editor::GetTabIndents;
	using Editor::SetBackSpaceUnIndents;
	using Editor::GetBackSpaceUnIndents;
	using Editor::SetLineIndentation;
	using Editor::GetLineIndentation;
	using Editor::GetLineIndentPosition;
	using Editor::GetLine;
	using Editor::GetLineCount;
	using Editor::LineFromPosition;
	using Editor::PositionFromLine;
	using Editor::LineLength;
	using Editor::GetLineEndPosition;
	using Editor::GetColumn;
	using Editor::CountCharacters;
	using Editor::CountCodeUnits;
	using Editor::FindColumn;
	using Editor::SetViewEOL;
	using Editor::GetViewEOL;
	using Editor::SetSelEOLFilled;
	using Editor::GetSelEOLFilled;
	using Editor::SetWordChars;
	using Editor::GetWordChars;
	using Editor::GetLineCharacterIndex;
	using Editor::SetEdgeColumn;
	using Editor::GetEdgeColumn;

	// Margins (protected on Editor).
	using Editor::SetMargins;
	using Editor::GetMargins;
	using Editor::SetMarginTypeN;
	using Editor::GetMarginTypeN;
	using Editor::SetMarginWidthN;
	using Editor::GetMarginWidthN;
	using Editor::SetMarginMaskN;
	using Editor::GetMarginMaskN;
	using Editor::SetMarginSensitiveN;
	using Editor::GetMarginSensitiveN;
	using Editor::SetMarginCursorN;
	using Editor::GetMarginCursorN;
	using Editor::SetMarginBackN;
	using Editor::GetMarginBackN;
	using Editor::SetMarginLeft;
	using Editor::GetMarginLeft;
	using Editor::SetMarginRight;
	using Editor::GetMarginRight;
	using Editor::MarginSetText;
	using Editor::MarginGetText;
	using Editor::MarginSetStyle;
	using Editor::MarginGetStyle;
	using Editor::MarginSetStyles;
	using Editor::MarginGetStyles;
	using Editor::MarginTextClearAll;
	using Editor::MarginSetStyleOffset;
	using Editor::MarginGetStyleOffset;
	using Editor::SetMarginOptions;
	using Editor::GetMarginOptions;
	using Editor::SetFoldMarginColour;
	using Editor::SetFoldMarginHiColour;

	// Markers (protected on Editor).
	using Editor::MarkerDefine;
	using Editor::MarkerSymbolDefined;
	using Editor::MarkerSetFore;
	using Editor::MarkerSetBack;
	using Editor::MarkerSetBackSelected;
	using Editor::MarkerSetForeTranslucent;
	using Editor::MarkerSetBackTranslucent;
	using Editor::MarkerSetBackSelectedTranslucent;
	using Editor::MarkerSetStrokeWidth;
	using Editor::MarkerEnableHighlight;
	using Editor::MarkerSetAlpha;
	using Editor::MarkerSetLayer;
	using Editor::MarkerGetLayer;
	using Editor::MarkerAdd;
	using Editor::MarkerAddSet;
	using Editor::MarkerDelete;
	using Editor::MarkerDeleteAll;
	using Editor::MarkerGet;
	using Editor::MarkerNext;
	using Editor::MarkerPrevious;
	using Editor::MarkerLineFromHandle;
	using Editor::MarkerDeleteHandle;
	using Editor::MarkerHandleFromLine;
	using Editor::MarkerNumberFromLine;
	using Editor::MarkerDefinePixmap;
	using Editor::MarkerDefineRGBAImage;
	using Editor::RGBAImageSetWidth;
	using Editor::RGBAImageSetHeight;
	using Editor::RGBAImageSetScale;

	// Folding / visibility (protected on Editor).
	using Editor::SetFoldLevel;
	using Editor::GetFoldLevel;
	using Editor::GetLastChild;
	using Editor::GetFoldParent;
	using Editor::GetLineVisible;
	using Editor::GetAllLinesVisible;
	using Editor::ShowLines;
	using Editor::HideLines;
	using Editor::GetFoldExpanded;
	using Editor::SetFoldExpanded;
	using Editor::ContractedFoldNext;
	using Editor::EnsureVisible;
	using Editor::EnsureVisibleEnforcePolicy;
	using Editor::FoldLine;
	using Editor::FoldChildren;
	using Editor::ExpandChildren;
	using Editor::FoldAll;
	using Editor::ToggleFold;
	using Editor::ToggleFoldShowText;
	using Editor::SetFoldFlags;
	using Editor::SetAutomaticFold;
	using Editor::GetAutomaticFold;
	using Editor::FoldDisplayTextSetStyle;
	using Editor::FoldDisplayTextGetStyle;
	using Editor::SetDefaultFoldDisplayText;
	using Editor::VisibleFromDocLine;
	using Editor::DocLineFromVisible;
	using EditModel::GetDefaultFoldDisplayText;

	// Styling / view appearance (protected on Editor).
	using Editor::ClearDocumentStyle;
	using Editor::GetStyleAt;
	using Editor::GetStyleIndexAt;
	using Editor::GetEndStyled;
	using Editor::StartStyling;
	using Editor::SetStyling;
	using Editor::SetStylingEx;
	using Editor::SetIdleStyling;
	using Editor::GetIdleStyling;
	using Editor::StyleClearAll;
	using Editor::StyleSetFore;
	using Editor::StyleGetFore;
	using Editor::StyleSetBack;
	using Editor::StyleGetBack;
	using Editor::StyleSetBold;
	using Editor::StyleGetBold;
	using Editor::StyleSetSize;
	using Editor::StyleGetSize;
	using Editor::StyleSetFont;
	using Editor::StyleGetFont;
	using Editor::SetViewWS;
	using Editor::GetViewWS;
	using Editor::SetWhitespaceSize;
	using Editor::GetWhitespaceSize;
	using Editor::SetSelFore;
	using Editor::SetSelBack;
	using Editor::SetSelAlpha;
	using Editor::GetSelAlpha;
	using Editor::SetElementColour;
	using Editor::ResetElementColour;
	using Editor::GetElementIsSet;
	using Editor::SetLayoutCache;
	using Editor::GetLayoutCache;
	using Editor::SetPositionCache;
	using Editor::GetPositionCache;
	using Editor::SetPhasesDraw;
	using Editor::GetPhasesDraw;
	using Editor::SetExtraAscent;
	using Editor::GetExtraAscent;
	using Editor::SetExtraDescent;
	using Editor::GetExtraDescent;
	using Editor::SetEdgeMode;
	using Editor::GetEdgeMode;
	using Editor::SetEdgeColour;
	using Editor::GetEdgeColour;
	using Editor::MultiEdgeAddLine;
	using Editor::MultiEdgeClearAll;
	using Editor::GetMultiEdgeColumn;
	using Editor::SetHighlightGuide;
	using Editor::GetHighlightGuide;
	using Editor::AllocateExtendedStyles;
	using Editor::ReleaseAllExtendedStyles;
	using Editor::SetBidirectional;
	using Editor::GetBidirectional;

	// Extra decoration / indicator operations beyond the thin wrappers below.
	using Editor::IndicSetHoverStyle;
	using Editor::IndicGetHoverStyle;
	using Editor::IndicSetHoverFore;
	using Editor::IndicGetHoverFore;
	using Editor::IndicSetFlags;
	using Editor::IndicGetFlags;
	using Editor::IndicSetUnder;
	using Editor::IndicGetUnder;
	using Editor::IndicSetAlpha;
	using Editor::IndicGetAlpha;
	using Editor::IndicSetOutlineAlpha;
	using Editor::IndicGetOutlineAlpha;
	using Editor::IndicSetStrokeWidth;
	using Editor::IndicGetStrokeWidth;
	using Editor::IndicatorAllOnFor;
	using Editor::BraceBadLight;
	using Editor::BraceBadLightIndicator;
	using Editor::BraceHighlightIndicator;
	using Editor::BraceMatchNext;
	using Editor::ClearAllRepresentations;
	using Editor::SetRepresentationAppearance;
	using Editor::GetRepresentationAppearance;
	using Editor::SetRepresentationColour;
	using Editor::GetRepresentationColour;
	using Editor::SetHotspotActiveBack;
	using Editor::GetHotspotActiveBack;
	using Editor::SetHotspotActiveUnderline;
	using Editor::GetHotspotActiveUnderline;
	using Editor::SetHotspotSingleLine;
	using Editor::GetHotspotSingleLine;
	using Editor::AnnotationGetLines;
	using Editor::AnnotationSetStyles;
	using Editor::AnnotationGetStyles;
	using Editor::AnnotationSetStyleOffset;
	using Editor::AnnotationGetStyleOffset;
	using Editor::EOLAnnotationSetStyle;
	using Editor::EOLAnnotationGetStyle;
	using Editor::EOLAnnotationSetStyleOffset;
	using Editor::EOLAnnotationGetStyleOffset;
	using Editor::EOLAnnotationGetVisible;
	using Editor::SetEOLAnnotationVisible;
	using Editor::GetSelectionMode;

	// ReplaceTarget and its enum stay protected on Editor; thin public wrapper for tests.
	Sci::Position ReplaceTargetBasic(std::string_view text) {
		return Editor::ReplaceTarget(Editor::ReplaceType::basic, text);
	}

	// Expose private input operations only to the focused editor tests.
	using Editor::AssignCmdKey;
	using Editor::ClearAllCmdKeys;
	using Editor::ClearCmdKey;
	using Editor::GetCursor;
	using Editor::GetDragDropEnabled;
	using Editor::GetIMEInteraction;
	using Editor::GetCommandEvents;
	using Editor::GetModEventMask;
	using Editor::GetMouseDownCaptures;
	using Editor::GetMouseDwellTime;
	using Editor::GetMouseWheelCaptures;
	using Editor::GetOvertype;
	using Editor::GetStatus;
	using Editor::SupportsFeature;
	using Editor::SetCursor;
	using Editor::SetDragDropEnabled;
	using Editor::SetIMEInteraction;
	using Editor::SetCommandEvents;
	using Editor::SetModEventMask;
	using Editor::SetMouseDownCaptures;
	using Editor::SetMouseDwellTime;
	using Editor::SetMouseWheelCaptures;
	using Editor::SetOvertype;
	using Editor::SetStatus;
	using ScintillaBase::GetUsePopUp;
	using ScintillaBase::UsePopUp;
	// Context menu and its command ids (protected on ScintillaBase).
	using ScintillaBase::Command;
	using ScintillaBase::ContextMenu;
	static constexpr int IdCmdUndo = idcmdUndo;
	static constexpr int IdCmdRedo = idcmdRedo;
	static constexpr int IdCmdCut = idcmdCut;
	static constexpr int IdCmdCopy = idcmdCopy;
	static constexpr int IdCmdPaste = idcmdPaste;
	static constexpr int IdCmdDelete = idcmdDelete;
	static constexpr int IdCmdSelectAll = idcmdSelectAll;

	// Thin public forwards of protected Editor decoration operations for free-function tests.
	void IndicSetStyle(size_t indicator, Scintilla::IndicatorStyle style);
	Scintilla::IndicatorStyle IndicGetStyle(size_t indicator) const noexcept;
	void IndicSetFore(size_t indicator, int rgb);
	int IndicGetFore(size_t indicator) const noexcept;
	void SetIndicatorCurrent(int indicator);
	int GetIndicatorCurrent() const noexcept;
	void SetIndicatorValue(int value);
	int GetIndicatorValue() const noexcept;
	void IndicatorFillRange(Sci::Position start, Sci::Position lengthFill);
	void IndicatorClearRange(Sci::Position start, Sci::Position lengthClear);
	int IndicatorValueAt(int indicator, Sci::Position pos) const;
	Sci::Position IndicatorStart(int indicator, Sci::Position pos) const;
	Sci::Position IndicatorEnd(int indicator, Sci::Position pos) const;
	void BraceHighlight(Sci::Position pos0, Sci::Position pos1);
	Sci::Position BraceMatch(Sci::Position pos, Sci::Position maxReStyle) const noexcept;
	void SetControlCharSymbol(int symbol);
	int GetControlCharSymbol() const noexcept;
	void SetRepresentation(std::string_view charBytes, std::string_view value);
	int GetRepresentation(std::string_view charBytes, char *buffer) const;
	void ClearRepresentation(std::string_view charBytes);
	void SetHotspotActiveFore(bool useSetting, int rgb);
	int GetHotspotActiveFore() const;
	void AnnotationSetText(Sci::Line line, const char *text);
	std::string AnnotationGetText(Sci::Line line) const;
	void AnnotationSetStyle(Sci::Line line, int style);
	int AnnotationGetStyle(Sci::Line line) const noexcept;
	void AnnotationClearAll();
	void SetAnnotationVisible(Scintilla::AnnotationVisible visible);
	Scintilla::AnnotationVisible AnnotationGetVisible() const noexcept;
	void EOLAnnotationSetText(Sci::Line line, const char *text);
	std::string EOLAnnotationGetText(Sci::Line line) const;
	void EOLAnnotationClearAll();

	TestEditorObservations observations;

private:
	void SetHorizontalScrollPos() override;
	void SetVerticalScrollPos() override;
	bool ModifyScrollBars(Sci::Line nMax, Sci::Line nPage) override;
	void ReconfigureScrollBars() override;
	void Copy() override;
	void Paste() override;
	void ClaimSelection() override;
	void NotifyChange() override;
	void NotifyParent(Scintilla::NotificationData scn) override;
	void CopyToClipboard(const SelectionText &selectedText) override;
	void SetMouseCapture(bool on) override;
	bool HaveMouseCapture() override;
	void CreateCallTipWindow(PRectangle rc) override;
	void AddToPopUp(const char *label, int cmd, bool enabled) override;
	bool FineTickerRunning(TickReason reason) override;
	void FineTickerStart(TickReason reason, int millis, int tolerance) override;
	void FineTickerCancel(TickReason reason) override;
	bool SetIdle(bool on) override;

	TestHost &host;
	unsigned int currentTime = 0;
	std::array<bool, 5> tickers{};
};

// Load text, clear undo history from the load, and mark the save point.
void LoadClean(TestEditor &editor, std::string_view text);

}

#endif
