// scalpel-editor test code
/** @file TestEditor.h
 ** Concrete ScintillaBase subclass with an observable in-memory host.
 **/

#ifndef TESTEDITOR_H
#define TESTEDITOR_H

namespace Scintilla::Internal {

struct TestNotification {
	Scintilla::Notification code = Scintilla::Notification::StyleNeeded;
	Sci::Position position = 0;
	Sci::Position length = 0;
	Scintilla::ModificationFlags modificationType = Scintilla::ModificationFlags::None;
	Scintilla::Update updated = Scintilla::Update::None;
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
	void MouseDown(Point point, Scintilla::KeyMod modifiers);
	void MouseMove(Point point, Scintilla::KeyMod modifiers);
	void MouseUp(Point point, Scintilla::KeyMod modifiers);
	void AdvanceTime(unsigned int milliseconds) noexcept;
	unsigned int CurrentTime() const noexcept;
	void FlushUpdateNotifications();
	void PaintAll();
	void ClearObservations();
	TestEditorSnapshot Snapshot() const;

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
	std::string UTF8FromEncoded(std::string_view encoded) const override;
	std::string EncodedFromUTF8(std::string_view utf8) const override;
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

}

#endif
