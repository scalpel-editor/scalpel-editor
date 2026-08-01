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

#include "EditorBasicTypes.h"
#include "EditorDocumentTypes.h"
#include "EditorStyleTypes.h"
#include "EditorInputTypes.h"
#include "EditorLayoutTypes.h"
#include "ILoader.h"
#include "ILexer.h"

#include "Debugging.h"
#include "DrawSurface.h"
#include "FontPlatform.h"
#include "Geometry.h"
#include "Platform.h"
#include "Renderer.h"
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
	// Production capture delivers typed actions here while recording is on.
	SetRecordingCallback(MakeRecordingCallback());
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

void TestEditor::MouseRightDown(Point point, Scintilla::KeyMod modifiers) {
	RightButtonDownWithModifiers(point, currentTime, modifiers);
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
	std::unique_ptr<DrawSurface> surface = PaintToSurface();
	if (surface) {
		surface->Release();
	}
}

std::unique_ptr<DrawSurface> TestEditor::PaintToSurface() {
	paintState = PaintState::painting;
	rcPaint = GetClientRectangle();
	paintingAllText = true;
	const int width = std::max(1, static_cast<int>(rcPaint.Width()));
	const int height = std::max(1, static_cast<int>(rcPaint.Height()));
	host.EnsureRenderer();
	const double fontSize = static_cast<double>(Platform::DefaultFontSize());
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(
		*host.GetRenderer(), width, height, FontFallback::Fixed(TestFontFallbackFaces(fontSize)));
	Paint(surface.get(), rcPaint);
	paintState = PaintState::notPainting;
	paintingAllText = false;
	return surface;
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
	// not a one-shot host effect log. Only clear request logs and counters.
	const int listBoxesAllocated = host.log.listBoxesAllocated;
	host.log = {};
	host.log.listBoxesAllocated = listBoxesAllocated;
}

void TestEditor::OnRecordedAction(const RecordedAction &action) {
	observations.recordedActions.push_back(action);
}

RecordingCallback TestEditor::MakeRecordingCallback() {
	return [this](const RecordedAction &action) {
		OnRecordedAction(action);
	};
}

void TestEditor::IndicSetStyle(size_t indicator, Scintilla::IndicatorStyle style) {
	Editor::IndicSetStyle(indicator, style);
}

Scintilla::IndicatorStyle TestEditor::IndicGetStyle(size_t indicator) const noexcept {
	return Editor::IndicGetStyle(indicator);
}

void TestEditor::IndicSetFore(size_t indicator, int rgb) {
	Editor::IndicSetFore(indicator, rgb);
}

int TestEditor::IndicGetFore(size_t indicator) const noexcept {
	return Editor::IndicGetFore(indicator);
}

void TestEditor::SetIndicatorCurrent(int indicator) {
	Editor::SetIndicatorCurrent(indicator);
}

int TestEditor::GetIndicatorCurrent() const noexcept {
	return Editor::GetIndicatorCurrent();
}

void TestEditor::SetIndicatorValue(int value) {
	Editor::SetIndicatorValue(value);
}

int TestEditor::GetIndicatorValue() const noexcept {
	return Editor::GetIndicatorValue();
}

void TestEditor::IndicatorFillRange(Sci::Position start, Sci::Position lengthFill) {
	Editor::IndicatorFillRange(start, lengthFill);
}

void TestEditor::IndicatorClearRange(Sci::Position start, Sci::Position lengthClear) {
	Editor::IndicatorClearRange(start, lengthClear);
}

int TestEditor::IndicatorValueAt(int indicator, Sci::Position pos) const {
	return Editor::IndicatorValueAt(indicator, pos);
}

Sci::Position TestEditor::IndicatorStart(int indicator, Sci::Position pos) const {
	return Editor::IndicatorStart(indicator, pos);
}

Sci::Position TestEditor::IndicatorEnd(int indicator, Sci::Position pos) const {
	return Editor::IndicatorEnd(indicator, pos);
}

void TestEditor::BraceHighlight(Sci::Position pos0, Sci::Position pos1) {
	Editor::BraceHighlight(pos0, pos1);
}

Sci::Position TestEditor::BraceMatch(Sci::Position pos, Sci::Position maxReStyle) const noexcept {
	return Editor::BraceMatch(pos, maxReStyle);
}

void TestEditor::SetControlCharSymbol(int symbol) {
	Editor::SetControlCharSymbol(symbol);
}

int TestEditor::GetControlCharSymbol() const noexcept {
	return Editor::GetControlCharSymbol();
}

void TestEditor::SetRepresentation(std::string_view charBytes, std::string_view value) {
	Editor::SetRepresentation(charBytes, value);
}

int TestEditor::GetRepresentation(std::string_view charBytes, char *buffer) const {
	return Editor::GetRepresentation(charBytes, buffer);
}

void TestEditor::ClearRepresentation(std::string_view charBytes) {
	Editor::ClearRepresentation(charBytes);
}

void TestEditor::SetHotspotActiveFore(bool useSetting, int rgb) {
	Editor::SetHotspotActiveFore(useSetting, rgb);
}

int TestEditor::GetHotspotActiveFore() const {
	return Editor::GetHotspotActiveFore();
}

void TestEditor::AnnotationSetText(Sci::Line line, const char *text) {
	Editor::AnnotationSetText(line, text);
}

std::string TestEditor::AnnotationGetText(Sci::Line line) const {
	return Editor::AnnotationGetText(line);
}

void TestEditor::AnnotationSetStyle(Sci::Line line, int style) {
	Editor::AnnotationSetStyle(line, style);
}

int TestEditor::AnnotationGetStyle(Sci::Line line) const noexcept {
	return Editor::AnnotationGetStyle(line);
}

void TestEditor::AnnotationClearAll() {
	Editor::AnnotationClearAll();
}

void TestEditor::SetAnnotationVisible(Scintilla::AnnotationVisible visible) {
	Editor::SetAnnotationVisible(visible);
}

Scintilla::AnnotationVisible TestEditor::AnnotationGetVisible() const noexcept {
	return Editor::AnnotationGetVisible();
}

void TestEditor::EOLAnnotationSetText(Sci::Line line, const char *text) {
	Editor::EOLAnnotationSetText(line, text);
}

std::string TestEditor::EOLAnnotationGetText(Sci::Line line) const {
	return Editor::EOLAnnotationGetText(line);
}

void TestEditor::EOLAnnotationClearAll() {
	Editor::EOLAnnotationClearAll();
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
	if (scn.code == Scintilla::Notification::Modified &&
		observations.replayOnModified.has_value()) {
		const RecordedAction nestedAction = std::move(*observations.replayOnModified);
		observations.replayOnModified.reset();
		ReplayRecordedAction(nestedAction);
	}
	if (scn.code == Scintilla::Notification::Modified &&
		FlagSet(scn.modificationType, Scintilla::ModificationFlags::InsertCheck) &&
		observations.changeInsertionOnInsertCheck.has_value()) {
		ChangeInsertion(*observations.changeInsertionOnInsertCheck);
	}
	TestNotification notification;
	notification.code = scn.code;
	notification.position = scn.position;
	notification.length = scn.length;
	notification.line = scn.line;
	notification.linesAdded = scn.linesAdded;
	notification.ch = scn.ch;
	notification.margin = scn.margin;
	notification.listType = scn.listType;
	notification.x = scn.x;
	notification.y = scn.y;
	notification.modifiers = scn.modifiers;
	notification.modificationType = scn.modificationType;
	notification.foldLevelNow = scn.foldLevelNow;
	notification.foldLevelPrev = scn.foldLevelPrev;
	notification.token = scn.token;
	notification.annotationLinesAdded = scn.annotationLinesAdded;
	notification.updated = scn.updated;
	notification.listCompletionMethod = scn.listCompletionMethod;
	notification.characterSource = scn.characterSource;
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

// Load text, clear undo history from the load, and mark the save point.
void LoadClean(TestEditor &editor, std::string_view text) {
	editor.SetText(text);
	editor.EmptyUndoBuffer();
	editor.SetSavePoint();
}

}
