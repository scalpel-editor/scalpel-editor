// scalpel-editor test code
/** @file TestEditor.cxx
 ** Concrete ScintillaBase subclass with an observable in-memory host.
 **/

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <forward_list>
#include <map>
#include <cstring>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
#include "Document.h"
#include "UniConversion.h"
#include "Selection.h"
#include "PositionCache.h"
#include "EditModel.h"
#include "MarginView.h"
#include "EditView.h"
#include "Editor.h"
#include "AutoComplete.h"
#include "CallTip.h"
#include "ScintillaBase.h"

#include "TestPlatform.h"
#include "TestEditor.h"

namespace Scintilla::Internal {

bool TestEditorSnapshot::operator==(const TestEditorSnapshot &other) const noexcept {
	return wrapMode == other.wrapMode &&
		horizontalOffset == other.horizontalOffset &&
		horizontalScrollUpdates == other.horizontalScrollUpdates &&
		verticalScrollUpdates == other.verticalScrollUpdates &&
		scrollbarChanges == other.scrollbarChanges &&
		scrollbarReconfigurations == other.scrollbarReconfigurations &&
		scrollbarMaximum == other.scrollbarMaximum &&
		scrollbarPage == other.scrollbarPage &&
		invalidateAllCount == other.invalidateAllCount &&
		invalidatedRectangles == other.invalidatedRectangles &&
		updateNotifications == other.updateNotifications;
}

TestEditor::TestEditor(TestHost &host_, PRectangle clientRectangle) : host(host_) {
	host.mainWindow.rect = clientRectangle;
	wMain = static_cast<WindowID>(&host.mainWindow);
}

TestEditor::~TestEditor() {
	Finalise();
}

void TestEditor::SetClientRectangle(PRectangle clientRectangle) {
	host.mainWindow.rect = clientRectangle;
	ChangeSize();
}

PRectangle TestEditor::ClientRectangle() const {
	return GetClientRectangle();
}

void TestEditor::SetText(std::string_view text) {
	Editor::SetText(text);
}

std::string TestEditor::Text() const {
	return GetText();
}

void TestEditor::SetHorizontalOffset(int offset) {
	HorizontalScrollTo(offset);
}

int TestEditor::HorizontalOffset() const noexcept {
	return xOffset;
}

void TestEditor::InsertInput(std::string_view text) {
	InsertCharacter(text, Scintilla::CharacterSource::DirectInput);
}

int TestEditor::KeyDown(Scintilla::Keys key, Scintilla::KeyMod modifiers, bool *consumed) {
	return KeyDownWithModifiers(key, modifiers, consumed);
}

int TestEditor::RunCommand(EditorCommand command) {
	return ExecuteCommand(command);
}

Sci::Position TestEditor::CurrentPos() const {
	return sel.MainCaret();
}

void TestEditor::MouseDown(Point point, Scintilla::KeyMod modifiers) {
	ButtonDownWithModifiers(point, currentTime, modifiers);
}

void TestEditor::MouseMove(Point point, Scintilla::KeyMod modifiers) {
	ButtonMoveWithModifiers(point, currentTime, modifiers);
}

void TestEditor::MouseUp(Point point, Scintilla::KeyMod modifiers) {
	ButtonUpWithModifiers(point, currentTime, modifiers);
}

void TestEditor::AdvanceTime(unsigned int milliseconds) noexcept {
	currentTime += milliseconds;
}

unsigned int TestEditor::CurrentTime() const noexcept {
	return currentTime;
}

void TestEditor::FlushUpdateNotifications() {
	NotifyUpdateUI();
}

void TestEditor::PaintAll() {
	paintState = PaintState::painting;
	rcPaint = GetClientRectangle();
	paintingAllText = true;
	std::unique_ptr<Surface> surface = Surface::Allocate(Scintilla::Technology::Default);
	surface->Init(static_cast<SurfaceID>(&host.mainWindow), static_cast<WindowID>(&host.mainWindow));
	Paint(surface.get(), rcPaint);
	surface->Release();
	paintState = PaintState::notPainting;
	paintingAllText = false;
}

void TestEditor::ClearObservations() {
	const std::string clipboard = observations.clipboard;
	const bool mouseCaptured = host.mainWindow.mouseCaptured;
	const bool idleRequested = observations.idleRequested;
	observations = {};
	observations.clipboard = clipboard;
	observations.mouseCaptured = mouseCaptured;
	observations.idleRequested = idleRequested;
	host.mainWindow.invalidations.clear();
	host.mainWindow.invalidateAllCount = 0;
	// Keep live list-box and call-tip window state: those reflect current UI,
	// not a one-shot host effect log. Only clear request/draw logs and counters.
	const int listBoxesAllocated = host.log.listBoxesAllocated;
	const int surfacesAllocated = host.log.surfacesAllocated;
	const int fontsAllocated = host.log.fontsAllocated;
	host.log = {};
	host.log.listBoxesAllocated = listBoxesAllocated;
	host.log.surfacesAllocated = surfacesAllocated;
	host.log.fontsAllocated = fontsAllocated;
}

TestEditorSnapshot TestEditor::Snapshot() const {
	TestEditorSnapshot snapshot;
	snapshot.wrapMode = GetWrapMode();
	snapshot.horizontalOffset = xOffset;
	snapshot.horizontalScrollUpdates = observations.horizontalScrollUpdates;
	snapshot.verticalScrollUpdates = observations.verticalScrollUpdates;
	snapshot.scrollbarChanges = observations.scrollbarChanges;
	snapshot.scrollbarReconfigurations = observations.scrollbarReconfigurations;
	snapshot.scrollbarMaximum = observations.scrollbarMaximum;
	snapshot.scrollbarPage = observations.scrollbarPage;
	snapshot.invalidateAllCount = host.mainWindow.invalidateAllCount;
	snapshot.invalidatedRectangles = host.mainWindow.invalidations.size();
	for (const TestNotification &notification : observations.notifications) {
		if (notification.code == Scintilla::Notification::UpdateUI)
			snapshot.updateNotifications.push_back(notification.updated);
	}
	return snapshot;
}

void TestEditor::SetHorizontalScrollPos() {
	observations.horizontalScrollUpdates++;
}

void TestEditor::SetVerticalScrollPos() {
	Editor::SetVerticalScrollPos();
	observations.verticalScrollUpdates++;
}

bool TestEditor::ModifyScrollBars(Sci::Line nMax, Sci::Line nPage) {
	const bool changed = observations.scrollbarMaximum != nMax || observations.scrollbarPage != nPage;
	observations.scrollbarChanges++;
	observations.scrollbarMaximum = nMax;
	observations.scrollbarPage = nPage;
	return changed;
}

void TestEditor::ReconfigureScrollBars() {
	observations.scrollbarReconfigurations++;
}

void TestEditor::Copy() {
	if (!sel.Empty()) {
		SelectionText selectedText;
		CopySelectionRange(&selectedText);
		CopyToClipboard(selectedText);
	}
}

void TestEditor::Paste() {
	if (!observations.clipboard.empty())
		InsertPaste(observations.clipboard);
}

void TestEditor::ClaimSelection() {
	observations.selectionClaims++;
}

void TestEditor::NotifyChange() {
	observations.changeNotifications++;
}

void TestEditor::NotifyParent(Scintilla::NotificationData scn) {
	TestNotification notification;
	notification.code = scn.nmhdr.code;
	notification.position = scn.position;
	notification.length = scn.length;
	notification.modificationType = scn.modificationType;
	notification.updated = scn.updated;
	notification.message = scn.message;
	notification.wParam = scn.wParam;
	notification.lParam = scn.lParam;
	if (scn.text) {
		if (scn.length > 0)
			notification.text.assign(scn.text, static_cast<size_t>(scn.length));
		else
			notification.text.assign(scn.text);
	}
	observations.notifications.push_back(std::move(notification));
}

void TestEditor::CopyToClipboard(const SelectionText &selectedText) {
	observations.clipboard.assign(selectedText.AsView());
}

void TestEditor::SetMouseCapture(bool on) {
	host.mainWindow.mouseCaptured = on;
	observations.mouseCaptured = on;
}

bool TestEditor::HaveMouseCapture() {
	return host.mainWindow.mouseCaptured;
}

void TestEditor::CreateCallTipWindow(PRectangle rc) {
	// Assign a real in-memory window so CallTip Show / SetPosition /
	// InvalidateAll / Destroy update inspectable host state. Without a
	// WindowID, Created() stays false and cancel cannot destroy the tip.
	host.callTipWindow.rect = rc;
	host.callTipWindow.cursor = Window::Cursor::invalid;
	host.callTipWindow.visible = false;
	host.callTipWindow.mouseCaptured = false;
	host.callTipWindow.invalidations.clear();
	host.callTipWindow.invalidateAllCount = 0;
	host.callTip.created = true;
	host.callTip.visible = false;
	host.callTip.rect = rc;
	host.callTip.createCount++;
	host.callTip.invalidateAllCount = 0;
	ct.wCallTip = static_cast<WindowID>(&host.callTipWindow);
	observations.callTipWindows.push_back(rc);
}

void TestEditor::AddToPopUp(const char *label, int cmd, bool enabled) {
	observations.popupItems.push_back(std::string(label ? label : "") + " " +
		std::to_string(cmd) + (enabled ? " enabled" : " disabled"));
}

bool TestEditor::FineTickerRunning(TickReason reason) {
	return tickers[static_cast<size_t>(reason)];
}

void TestEditor::FineTickerStart(TickReason reason, int millis, int tolerance) {
	tickers[static_cast<size_t>(reason)] = true;
	observations.tickerRequests.push_back({static_cast<int>(reason), millis, tolerance, true});
}

void TestEditor::FineTickerCancel(TickReason reason) {
	tickers[static_cast<size_t>(reason)] = false;
	observations.tickerRequests.push_back({static_cast<int>(reason), 0, 0, false});
}

bool TestEditor::SetIdle(bool on) {
	observations.idleRequested = on;
	return true;
}

Scintilla::sptr_t TestEditor::DefWndProc(Scintilla::Message message, Scintilla::uptr_t,
	Scintilla::sptr_t) {
	observations.defaultWindowCalls.push_back(message);
	return 0;
}

}
