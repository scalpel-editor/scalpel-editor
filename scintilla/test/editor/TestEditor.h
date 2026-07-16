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
	Scintilla::ModificationFlags modificationType = Scintilla::ModificationFlags::None;
	Scintilla::Update updated = Scintilla::Update::None;
	Scintilla::Message message = Scintilla::Message::Null;
	Scintilla::uptr_t wParam = 0;
	Scintilla::sptr_t lParam = 0;
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
	std::vector<Scintilla::Message> defaultWindowCalls;
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

	// Thin public forwards of protected Editor decoration operations so free-function
	// tests can compare the named path with temporary message forwarders.
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
	Scintilla::sptr_t DefWndProc(Scintilla::Message message, Scintilla::uptr_t wParam,
		Scintilla::sptr_t lParam) override;

	TestHost &host;
	unsigned int currentTime = 0;
	std::array<bool, 5> tickers{};
};

// Load text, clear undo history from the load, and mark the save point.
void LoadClean(TestEditor &editor, std::string_view text);

}

#endif
