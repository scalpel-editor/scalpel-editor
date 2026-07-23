// Production ScintillaBase host used by the standalone application.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>

#include "ApplicationEditor.h"
#include "Document.h"
#include "DrawSurface.h"
#include "GlContext.h"
#include "Renderer.h"

namespace Scalpel {

using Scintilla::Internal::DrawSurface;
using Scintilla::Internal::PRectangle;

namespace {

const char *ClipboardStatusName(ApplicationClipboardStatus status) noexcept {
	switch (status) {
	case ApplicationClipboardStatus::Published:
		return "published";
	case ApplicationClipboardStatus::Complete:
		return "complete";
	case ApplicationClipboardStatus::Unavailable:
		return "service unavailable";
	case ApplicationClipboardStatus::NoText:
		return "no text";
	case ApplicationClipboardStatus::InvalidText:
		return "invalid UTF-8";
	case ApplicationClipboardStatus::Cancelled:
		return "cancelled";
	case ApplicationClipboardStatus::Failed:
		return "transfer failed";
	case ApplicationClipboardStatus::TooLarge:
		return "text too large";
	case ApplicationClipboardStatus::TimedOut:
		return "transfer timed out";
	case ApplicationClipboardStatus::Superseded:
		return "superseded";
	}
	return "unknown";
}

}

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
	documentGeneration++;
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

void ApplicationEditor::HandlePointerInput(const PointerInput &input) {
	const Scintilla::Internal::Point point(input.x, input.y);
	switch (input.action) {
	case PointerAction::Move:
		ButtonMoveWithModifiers(point, input.time, input.modifiers);
		break;
	case PointerAction::Leave:
		MouseLeave();
		break;
	case PointerAction::Press:
		if (input.button == 0) {
			ButtonDownWithModifiers(point, input.time, input.modifiers);
		} else if (input.button == 1) {
			RightButtonDownWithModifiers(point, input.time, input.modifiers);
		}
		break;
	case PointerAction::Release:
		if (input.button == 0) {
			ButtonUpWithModifiers(point, input.time, input.modifiers);
		}
		break;
	case PointerAction::Scroll: {
		const bool shift = (input.modifiers & Scintilla::KeyMod::Shift) !=
			Scintilla::KeyMod::Norm;
		if (shift || std::abs(input.deltaX) > std::abs(input.deltaY)) {
			const double amount = std::abs(input.deltaX) > std::abs(input.deltaY) ?
				input.deltaX : input.deltaY;
			horizontalWheelRemainder += amount * 4.0;
			const int pixels = static_cast<int>(horizontalWheelRemainder);
			horizontalWheelRemainder -= pixels;
			if (pixels != 0) {
				HorizontalScrollTo(xOffset + pixels);
			}
		} else {
			verticalWheelRemainder += input.deltaY * 0.3;
			const Scintilla::Line lines = static_cast<Scintilla::Line>(verticalWheelRemainder);
			verticalWheelRemainder -= lines;
			if (lines != 0) {
				ScrollTo(topLine + lines);
			}
		}
		break;
	}
	}
}

void ApplicationEditor::SetPointerCapture(bool captured) {
	ChangeMouseCapture(captured);
}

void ApplicationEditor::RequestClipboardCopy() {
	Copy();
}

void ApplicationEditor::RequestClipboardPaste() {
	Paste();
}

void ApplicationEditor::SetClipboardPasteAvailable(bool available) noexcept {
	clipboardPasteAvailable = available;
}

bool ApplicationEditor::ClipboardPasteAvailable() {
	return clipboardPasteAvailable && CanPaste();
}

std::vector<ApplicationClipboardRequest> ApplicationEditor::TakeClipboardRequests() {
	return std::exchange(clipboardRequests, {});
}

void ApplicationEditor::HandleClipboardResult(uint64_t id,
	ApplicationClipboardOperation operation, ApplicationClipboardStatus status,
	std::string text) {
	ApplicationClipboardStatus reportedStatus = status;
	if (operation == ApplicationClipboardOperation::Paste &&
		status == ApplicationClipboardStatus::Complete) {
		if (!pendingPaste || pendingPaste->id != id ||
			pendingPaste->documentGeneration != documentGeneration) {
			reportedStatus = ApplicationClipboardStatus::Superseded;
		} else if (!text.empty() && CanPaste()) {
			Scintilla::Internal::UndoGroup undoGroup(pdoc);
			ClearSelection(multiPasteMode == Scintilla::MultiPaste::Each);
			InsertPasteShape(text, PasteShape::stream);
			EnsureCaretVisible();
			Redraw();
		}
	}
	if (operation == ApplicationClipboardOperation::Paste &&
		pendingPaste && pendingPaste->id == id &&
		status != ApplicationClipboardStatus::Published) {
		pendingPaste.reset();
	}
	clipboardResults.push_back({id, operation, reportedStatus});
	if (reportedStatus != ApplicationClipboardStatus::Published &&
		reportedStatus != ApplicationClipboardStatus::Complete &&
		reportedStatus != ApplicationClipboardStatus::Superseded &&
		reportedStatus != ApplicationClipboardStatus::NoText) {
		std::fprintf(stderr, "scalpel-editor clipboard: %s\n",
			ClipboardStatusName(reportedStatus));
	}
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
	if (sel.Empty()) {
		return;
	}
	Scintilla::Internal::SelectionText selectedText;
	CopySelectionRange(&selectedText);
	CopyToClipboard(selectedText);
}

void ApplicationEditor::Paste() {
	if (!CanPaste()) {
		return;
	}
	const uint64_t request = nextClipboardRequest++;
	pendingPaste = PendingPaste{request, documentGeneration};
	clipboardRequests.push_back(
		{request, ApplicationClipboardOperation::Paste, {}});
}

void ApplicationEditor::ClaimSelection() {
}

void ApplicationEditor::CopyToClipboard(
	const Scintilla::Internal::SelectionText &selectedText) {
	const uint64_t request = nextClipboardRequest++;
	clipboardRequests.push_back({
		request,
		ApplicationClipboardOperation::Copy,
		std::string(selectedText.AsView()),
	});
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
