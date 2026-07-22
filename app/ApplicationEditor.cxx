// Production ScintillaBase host used by the standalone application.

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>

#include "ApplicationEditor.h"
#include "DrawSurface.h"
#include "GlContext.h"
#include "Renderer.h"

namespace Scalpel {

using Scintilla::Internal::DrawSurface;
using Scintilla::Internal::PRectangle;

ApplicationResources::ApplicationResources(int width, int height) {
	if (width <= 0 || height <= 0) {
		throw std::invalid_argument("ApplicationEditor requires a positive size");
	}
	window.rectangle = PRectangle(0, 0, width, height);
	window.visible = true;
	glContext = std::make_unique<Scintilla::Internal::GlContext>();
	renderer = std::make_unique<Scintilla::Internal::Renderer>(*glContext);
}

ApplicationResources::~ApplicationResources() = default;

ApplicationEditor::ApplicationEditor(int width, int height) : ApplicationResources(width, height) {
	wMain = static_cast<Scintilla::Internal::WindowID>(&window);
}

ApplicationEditor::~ApplicationEditor() {
	Finalise();
}

void ApplicationEditor::LoadInitialBuffer(std::string_view text) {
	SetText(text);
	EmptyUndoBuffer();
	SetSavePoint();
}

std::string ApplicationEditor::Text() const {
	return GetText();
}

void ApplicationEditor::Resize(int width, int height) {
	if (width <= 0 || height <= 0) {
		throw std::invalid_argument("ApplicationEditor::Resize requires a positive size");
	}
	window.rectangle = PRectangle(0, 0, width, height);
	frame.reset();
	ChangeSize();
	wMain.InvalidateAll();
}

void ApplicationEditor::SetKeyboardFocus(bool focused) {
	SetFocus(focused);
}

void ApplicationEditor::SetPointerCapture(bool captured) {
	ChangeMouseCapture(captured);
}

void ApplicationEditor::RequestClipboardCopy() {
	Copy();
}

bool ApplicationEditor::ClipboardPasteAvailable() {
	RecordUnsupported("clipboard paste availability");
	return false;
}

void ApplicationEditor::RenderFrame() {
	const int width = FrameWidth();
	const int height = FrameHeight();
	frame = Scintilla::Internal::CreateDrawSurface(*renderer, width, height);
	paintState = PaintState::painting;
	rcPaint = GetClientRectangle();
	paintingAllText = true;
	Paint(frame.get(), rcPaint);
	paintState = PaintState::notPainting;
	paintingAllText = false;
	window.invalidatedRectangles.clear();
}

void ApplicationEditor::RunPendingWork() {
	if (queuedIdleWork) {
		IdleWork();
		queuedIdleWork = false;
	}
	if (idleRequested) {
		idleRequested = Idle();
	}
}

std::vector<uint8_t> ApplicationEditor::FramePixels() const {
	if (!frame) {
		return {};
	}
	renderer->MakeCurrent();
	return frame->Buffer().ReadPixelsTopDown();
}

int ApplicationEditor::FrameWidth() const noexcept {
	return std::max(1, static_cast<int>(window.rectangle.Width()));
}

int ApplicationEditor::FrameHeight() const noexcept {
	return std::max(1, static_cast<int>(window.rectangle.Height()));
}

void ApplicationEditor::SetHorizontalScrollPos() {
	scrollbars.horizontalPosition = xOffset;
	scrollbars.horizontalUpdates++;
}

void ApplicationEditor::SetVerticalScrollPos() {
	Editor::SetVerticalScrollPos();
	scrollbars.verticalPosition = topLine;
	scrollbars.verticalUpdates++;
}

bool ApplicationEditor::ModifyScrollBars(Scintilla::Line maximum, Scintilla::Line page) {
	const bool changed = scrollbars.maximum != maximum || scrollbars.page != page;
	scrollbars.maximum = maximum;
	scrollbars.page = page;
	scrollbars.changes++;
	return changed;
}

void ApplicationEditor::ReconfigureScrollBars() {
	scrollbars.reconfigurations++;
}

void ApplicationEditor::Copy() {
	RecordUnsupported("clipboard copy");
}

void ApplicationEditor::Paste() {
	RecordUnsupported("clipboard paste");
}

void ApplicationEditor::ClaimSelection() {
}

void ApplicationEditor::CopyToClipboard(const Scintilla::Internal::SelectionText &) {
	RecordUnsupported("clipboard write");
}

void ApplicationEditor::StartDrag() {
	RecordUnsupported("drag and drop");
}

void ApplicationEditor::NotifyChange() {
	changeNotifications++;
}

void ApplicationEditor::NotifyParent(Scintilla::NotificationData notification) {
	notifications.push_back(notification.code);
}

void ApplicationEditor::SetMouseCapture(bool captured) {
	window.mouseCaptured = captured;
}

bool ApplicationEditor::HaveMouseCapture() {
	return window.mouseCaptured;
}

void ApplicationEditor::CreateCallTipWindow(PRectangle) {
	RecordUnsupported("call-tip window");
	// Window identity is intentionally omitted until popup windows are implemented.
}

void ApplicationEditor::AddToPopUp(const char *label, int command, bool enabled) {
	std::string request = "context-menu item: ";
	request += label ? label : "";
	request += " command=" + std::to_string(command);
	request += enabled ? " enabled" : " disabled";
	RecordUnsupported(std::move(request));
}

bool ApplicationEditor::FineTickerRunning(TickReason reason) {
	return tickers[static_cast<size_t>(reason)];
}

void ApplicationEditor::FineTickerStart(TickReason reason, int milliseconds, int tolerance) {
	const size_t index = static_cast<size_t>(reason);
	tickers[index] = true;
	tickerRequests.push_back({static_cast<int>(index), milliseconds, tolerance, true});
}

void ApplicationEditor::FineTickerCancel(TickReason reason) {
	const size_t index = static_cast<size_t>(reason);
	tickers[index] = false;
	tickerRequests.push_back({static_cast<int>(index), 0, 0, false});
}

bool ApplicationEditor::SetIdle(bool enabled) {
	idleRequested = enabled;
	return true;
}

void ApplicationEditor::QueueIdleWork(Scintilla::Internal::WorkItems items, Scintilla::Position upTo) {
	Editor::QueueIdleWork(items, upTo);
	queuedIdleWork = true;
}

void ApplicationEditor::RecordUnsupported(std::string request) {
	unsupportedRequests.push_back(std::move(request));
	std::fprintf(stderr, "scalpel-editor unsupported: %s\n", unsupportedRequests.back().c_str());
}

}
