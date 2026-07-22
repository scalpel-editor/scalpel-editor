// Production ScintillaBase host used by the standalone application.

#include <algorithm>
#include <chrono>
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

ApplicationResources::ApplicationResources(int width, int height) :
	ApplicationResources(std::make_unique<Scintilla::Internal::GlContext>(), width, height) {
}

ApplicationResources::ApplicationResources(
	std::unique_ptr<Scintilla::Internal::GlContext> context, int width, int height) {
	if (width <= 0 || height <= 0) {
		throw std::invalid_argument("ApplicationEditor requires a positive size");
	}
	if (!context) {
		throw std::invalid_argument("ApplicationEditor requires a GL context");
	}
	window.rectangle = PRectangle(0, 0, width, height);
	window.visible = true;
	glContext = std::move(context);
	renderer = std::make_unique<Scintilla::Internal::Renderer>(*glContext);
}

ApplicationResources::~ApplicationResources() = default;

ApplicationEditor::ApplicationEditor(int width, int height, NowFunction now_) :
	ApplicationResources(width, height), now(std::move(now_)) {
	if (!now) {
		throw std::invalid_argument("ApplicationEditor requires a clock");
	}
	wMain = static_cast<Scintilla::Internal::WindowID>(&window);
}

ApplicationEditor::ApplicationEditor(std::unique_ptr<Scintilla::Internal::GlContext> context,
	int width, int height, NowFunction now_) :
	ApplicationResources(std::move(context), width, height), now(std::move(now_)) {
	if (!now) {
		throw std::invalid_argument("ApplicationEditor requires a clock");
	}
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

void ApplicationEditor::HandleKeyboardInput(const KeyboardInput &input) {
	if (!input.pressed) {
		return;
	}
	bool consumed = false;
	if (input.key != static_cast<Scintilla::Keys>(0)) {
		KeyDownWithModifiers(input.key, input.modifiers, &consumed);
	}
	const Scintilla::KeyMod commandModifiers = Scintilla::KeyMod::Ctrl |
		Scintilla::KeyMod::Alt | Scintilla::KeyMod::Super | Scintilla::KeyMod::Meta;
	if (!consumed && !input.text.empty() &&
		(input.modifiers & commandModifiers) == Scintilla::KeyMod::Norm) {
		InsertCharacter(input.text, Scintilla::CharacterSource::DirectInput);
	}
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

void ApplicationEditor::PresentFrame() {
	if (!glContext->HasWindowSurface()) {
		throw std::runtime_error("ApplicationEditor::PresentFrame requires a window surface");
	}
	const int width = FrameWidth();
	const int height = FrameHeight();
	frame = Scintilla::Internal::CreateExternalDrawSurface(*renderer, 0, width, height);
	paintState = PaintState::painting;
	rcPaint = GetClientRectangle();
	paintingAllText = true;
	Paint(frame.get(), rcPaint);
	paintState = PaintState::notPainting;
	paintingAllText = false;
	window.invalidatedRectangles.clear();
	glContext->SwapBuffers();
}

void ApplicationEditor::RunPendingWork() {
	const Clock::time_point current = now();
	for (size_t index = 0; index < tickers.size(); index++) {
		FineTickerState &ticker = tickers[index];
		int fires = 0;
		while (ticker.running && current >= ticker.nextFire && fires < 8) {
			ticker.nextFire += ticker.period;
			fires++;
			TickFor(static_cast<TickReason>(index));
		}
		if (ticker.running && current >= ticker.nextFire) {
			ticker.nextFire = current + ticker.period;
		}
	}
	if (queuedIdleWork) {
		IdleWork();
		queuedIdleWork = false;
	}
	if (idleRequested) {
		idleRequested = Idle();
	}
}

std::optional<std::chrono::milliseconds> ApplicationEditor::TimeUntilNextWork() const {
	using std::chrono::ceil;
	using std::chrono::milliseconds;
	if (queuedIdleWork || idleRequested) {
		return milliseconds::zero();
	}

	const Clock::time_point current = now();
	std::optional<Clock::duration> shortest;
	for (const FineTickerState &ticker : tickers) {
		if (!ticker.running) {
			continue;
		}
		const Clock::duration remaining = std::max(Clock::duration::zero(), ticker.nextFire - current);
		if (!shortest || remaining < *shortest) {
			shortest = remaining;
		}
	}
	return shortest ? std::optional<milliseconds>(ceil<milliseconds>(*shortest)) : std::nullopt;
}

bool ApplicationEditor::NeedsRedraw() const noexcept {
	return !window.invalidatedRectangles.empty();
}

std::vector<uint8_t> ApplicationEditor::FramePixels() const {
	if (!frame || !frame->Buffer().Valid()) {
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
	return tickers[static_cast<size_t>(reason)].running;
}

void ApplicationEditor::FineTickerStart(TickReason reason, int milliseconds, int tolerance) {
	const size_t index = static_cast<size_t>(reason);
	FineTickerState &ticker = tickers[index];
	ticker.running = true;
	ticker.period = std::chrono::milliseconds(std::max(1, milliseconds));
	// The host currently keeps exact deadlines. Retain tolerance in the request
	// record for callers and tests until the event loop coalesces timer wakeups.
	ticker.nextFire = now() + ticker.period;
	tickerRequests.push_back({static_cast<int>(index), milliseconds, tolerance, true});
}

void ApplicationEditor::FineTickerCancel(TickReason reason) {
	const size_t index = static_cast<size_t>(reason);
	tickers[index].running = false;
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
