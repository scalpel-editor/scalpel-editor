// Production ScintillaBase host used by the standalone application.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "ApplicationEditor.h"
#include "Document.h"
#include "DrawSurface.h"
#include "GlContext.h"
#include "Renderer.h"
#include "UniConversion.h"

namespace Scalpel {

using Scintilla::Internal::DrawSurface;
using Scintilla::Internal::PRectangle;

namespace {

PRectangle DamageBounds(
	const std::vector<PRectangle> &damage, PRectangle client) {
	std::optional<PRectangle> bounds;
	for (const PRectangle &rectangle : damage) {
		const PRectangle clipped{
			std::clamp(rectangle.left, client.left, client.right),
			std::clamp(rectangle.top, client.top, client.bottom),
			std::clamp(rectangle.right, client.left, client.right),
			std::clamp(rectangle.bottom, client.top, client.bottom),
		};
		if (clipped.left >= clipped.right || clipped.top >= clipped.bottom) {
			continue;
		}
		if (!bounds) {
			bounds = clipped;
		} else {
			bounds->left = std::min(bounds->left, clipped.left);
			bounds->top = std::min(bounds->top, clipped.top);
			bounds->right = std::max(bounds->right, clipped.right);
			bounds->bottom = std::max(bounds->bottom, clipped.bottom);
		}
	}
	return bounds.value_or(client);
}

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

const char *PrimarySelectionStatusName(
	ApplicationPrimarySelectionStatus status) noexcept {
	switch (status) {
	case ApplicationPrimarySelectionStatus::Published:
		return "published";
	case ApplicationPrimarySelectionStatus::Complete:
		return "complete";
	case ApplicationPrimarySelectionStatus::Unavailable:
		return "service unavailable";
	case ApplicationPrimarySelectionStatus::NoText:
		return "no text";
	case ApplicationPrimarySelectionStatus::InvalidText:
		return "invalid UTF-8";
	case ApplicationPrimarySelectionStatus::Cancelled:
		return "cancelled";
	case ApplicationPrimarySelectionStatus::Failed:
		return "transfer failed";
	case ApplicationPrimarySelectionStatus::TooLarge:
		return "text too large";
	case ApplicationPrimarySelectionStatus::TimedOut:
		return "transfer timed out";
	case ApplicationPrimarySelectionStatus::Superseded:
		return "superseded";
	case ApplicationPrimarySelectionStatus::NotApplied:
		return "text not applied";
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
	ApplicationResources(width, height), now(std::move(now_)),
	bufferWidth(width), bufferHeight(height) {
	if (!now) {
		throw std::invalid_argument("ApplicationEditor requires a clock");
	}
	wMain = static_cast<Scintilla::Internal::WindowID>(&window);
	ConfigureLineNumberMargins();
}

ApplicationEditor::ApplicationEditor(std::unique_ptr<Scintilla::Internal::GlContext> context,
	int width, int height, NowFunction now_) :
	ApplicationResources(std::move(context), width, height), now(std::move(now_)),
	bufferWidth(width), bufferHeight(height) {
	if (!now) {
		throw std::invalid_argument("ApplicationEditor requires a clock");
	}
	wMain = static_cast<Scintilla::Internal::WindowID>(&window);
	ConfigureLineNumberMargins();
}

ApplicationEditor::~ApplicationEditor() {
	Finalise();
}

void ApplicationEditor::ConfigureLineNumberMargins() {
	// Margin 0 is Number by default; hide the empty symbol margins so only line numbers show.
	SetMarginTypeN(0, Scintilla::MarginType::Number);
	SetMarginWidthN(1, 0);
	SetMarginWidthN(2, 0);
	// Blank gap between the gutter and the text (default is 1px).
	SetMarginLeft(24);
	ApplyLineNumberStyle();
	UpdateLineNumberWidth();
}

void ApplicationEditor::ApplyLineNumberStyle() {
	// Unstyled buffer text uses style 0. Styles start with a null font name; on Refresh,
	// those take fonts.begin(). StyleClearAll copies STYLE_DEFAULT (system-ui) onto every
	// style first so a monospace gutter cannot become the accidental text face.
	// ClearStyles also forces StyleLineNumber.back to Platform::Chrome(); colours below
	// re-apply after that.
	StyleClearAll();

	// Colours are Scintilla RGB integers: R | (G << 8) | (B << 16).
	// Muted digits on a light gutter near Platform::Chrome.
	constexpr int lineNumber = static_cast<int>(Scintilla::StylesCommon::LineNumber);
	// Monospace so every digit shares one advance and right-aligned numbers line up
	// (proportional faces make "0" wider, so 10/20/30 sit further left than 11/12/13).
	StyleSetFont(lineNumber, "monospace");
	StyleSetCheckMonospaced(lineNumber, true);
	StyleSetFore(lineNumber, 0x008a7f6b);
	StyleSetBack(lineNumber, 0x00f7f5f4);
}

void ApplicationEditor::UpdateLineNumberWidth() {
	const Scintilla::Line lineCount = std::max<Scintilla::Line>(1, GetLineCount());
	int digits = 1;
	for (Scintilla::Line n = lineCount; n >= 10; n /= 10) {
		++digits;
	}
	// At least two digits so single-digit files do not look cramped.
	digits = std::max(2, digits);

	const std::string probe(static_cast<size_t>(digits), '9');
	const long textWidth = TextWidth(
		static_cast<int>(Scintilla::StylesCommon::LineNumber), probe);
	// Scintilla already right-pads by marginNumberPadding; add left breathing room.
	constexpr int extraPadding = 8;
	const int width = static_cast<int>(std::max<long>(1, textWidth)) + extraPadding;
	SetMarginWidthN(0, width);
	// SetMarginWidthN invalidates style data, but line-number placement uses the derived
	// fixedColumnWidth. Refresh it now so a cached-pane repaint cannot use the old width.
	RefreshStyleData();
}

void ApplicationEditor::LoadInitialBuffer(std::string_view text) {
	CancelTextInput();
	documentGeneration++;
	SetText(text);
	EmptyUndoBuffer();
	SetSavePoint();
	UpdateLineNumberWidth();
	textInputStateDirty = true;
}

std::string ApplicationEditor::Text() const {
	return GetText();
}

bool ApplicationEditor::Modified() const noexcept {
	return GetModify();
}

void ApplicationEditor::MarkSaved() {
	SetSavePoint();
}

Scintilla::Line ApplicationEditor::LineCount() const noexcept {
	return GetLineCount();
}

int ApplicationEditor::LineNumberMarginWidth() const noexcept {
	return vs.fixedColumnWidth;
}

int ApplicationEditor::TextLeftGap() const noexcept {
	return GetMarginLeft();
}

std::string ApplicationEditor::StyleFontName(int style) {
	char buffer[256]{};
	StyleGetFont(style, buffer);
	return buffer;
}

void ApplicationEditor::Resize(int width, int height) {
	if (width <= 0 || height <= 0) {
		throw std::invalid_argument("ApplicationEditor::Resize requires a positive size");
	}
	window.rectangle = PRectangle(0, 0, width, height);
	frame.reset();
	ChangeSize();
	wMain.InvalidateAll();
	textInputStateDirty = true;
}

void ApplicationEditor::SetFrameBufferSize(int width, int height) {
	if (width <= 0 || height <= 0) {
		throw std::invalid_argument(
			"ApplicationEditor::SetFrameBufferSize requires a positive size");
	}
	if (bufferWidth == width && bufferHeight == height) {
		return;
	}
	bufferWidth = width;
	bufferHeight = height;
	frame.reset();
	wMain.InvalidateAll();
}

void ApplicationEditor::SetKeyboardFocus(bool focused) {
	SetFocus(focused);
	if (!focused) {
		CancelTextInput();
	}
	textInputStateDirty = true;
}

void ApplicationEditor::HandleTextInputBatch(
	const ApplicationTextInputBatch &batch) {
	if (batch.cancel || pdoc->IsReadOnly() || SelectionContainsProtected()) {
		CancelTextInput();
		textInputStateDirty = textInputStateDirty || batch.refreshState;
		return;
	}
	const auto insertText = [this](std::string_view text,
		Scintilla::CharacterSource source) {
		for (size_t offset = 0; offset < text.size();) {
			const size_t length = Scintilla::Internal::UTF8DrawBytes(
				text.data() + offset, text.size() - offset);
			InsertCharacter(text.substr(offset, length), source);
			offset += length;
		}
	};

	const bool replacingPreedit = pdoc->TentativeActive();
	if (replacingPreedit) {
		pdoc->TentativeUndo();
		textInputPreeditRange.reset();
	}
	bool changed = replacingPreedit;

	{
		Scintilla::Internal::UndoGroup undoGroup(
			pdoc, batch.deletion.has_value() || batch.commit.has_value());
		if (batch.deletion && !DeleteTextInputSurrounding(*batch.deletion)) {
			textInputStateDirty = true;
			textInputChangeCause = ApplicationTextChangeCause::Other;
			return;
		}
		changed = changed || (batch.deletion &&
			(batch.deletion->beforeLength != 0 ||
				batch.deletion->afterLength != 0));
		if (batch.commit && !batch.commit->empty()) {
			insertText(*batch.commit, Scintilla::CharacterSource::ImeResult);
			changed = true;
		}
	}

	if (batch.preedit && !batch.preedit->text.empty()) {
		if (!replacingPreedit) {
			ClearBeforeTentativeStart();
		}
		const Scintilla::Position preeditStart = CurrentPosition();
		pdoc->TentativeStart();
		insertText(batch.preedit->text,
			Scintilla::CharacterSource::TentativeInput);
		textInputPreeditRange = TextInputPreeditRange{
			preeditStart,
			static_cast<Scintilla::Position>(batch.preedit->text.size()),
		};
		DrawImeIndicator(Scintilla::Internal::IndicatorInput,
			static_cast<Scintilla::Position>(batch.preedit->text.size()));
		if (batch.preedit->cursorBegin < 0) {
			DropCaret();
		} else {
			for (size_t selection = 0; selection < sel.Count(); selection++) {
				const Scintilla::Position insertEnd =
					sel.Range(selection).Start().Position();
				const Scintilla::Position insertStart = insertEnd -
					static_cast<Scintilla::Position>(batch.preedit->text.size());
				InvalidateSelection(sel.Range(selection));
				sel.Range(selection) = Scintilla::Internal::SelectionRange(
					insertStart + batch.preedit->cursorEnd,
					insertStart + batch.preedit->cursorBegin);
				InvalidateSelection(sel.Range(selection));
			}
			EnsureCaretVisible();
			ShowCaretAtCurrentPosition();
		}
		changed = true;
	}

	textInputStateDirty = textInputStateDirty || batch.refreshState || changed;
	if (changed) {
		textInputChangeCause = ApplicationTextChangeCause::InputMethod;
	}
}

std::optional<ApplicationTextInputState> ApplicationEditor::TakeTextInputState() {
	if (!textInputStateDirty) {
		return std::nullopt;
	}
	ApplicationTextInputState state = BuildTextInputState();
	state.changeCause = textInputChangeCause;
	textInputStateDirty = false;
	textInputChangeCause = ApplicationTextChangeCause::Other;
	return state;
}

int ApplicationEditor::ImeIndicatorAt(Scintilla::Position position) const {
	return IndicatorValueAt(Scintilla::Internal::IndicatorInput, position);
}

void ApplicationEditor::HandleKeyboardInput(const KeyboardInput &input) {
	if (!input.pressed) {
		return;
	}
	const Scintilla::Position caretBefore = sel.MainCaret();
	const Scintilla::Position anchorBefore = sel.MainAnchor();
	const int xOffsetBefore = xOffset;
	const Scintilla::Line topLineBefore = topLine;
	if (pdoc->TentativeActive() &&
		(input.key != static_cast<Scintilla::Keys>(0) || !input.text.empty())) {
		CancelTextInput();
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
	if (sel.MainCaret() != caretBefore || sel.MainAnchor() != anchorBefore ||
		xOffset != xOffsetBefore || topLine != topLineBefore) {
		textInputStateDirty = true;
		textInputChangeCause = ApplicationTextChangeCause::Other;
	}
}

void ApplicationEditor::HandlePointerInput(const PointerInput &input) {
	const Scintilla::Internal::Point point(input.x, input.y);
	const Scintilla::Position caretBefore = sel.MainCaret();
	const Scintilla::Position anchorBefore = sel.MainAnchor();
	const int xOffsetBefore = xOffset;
	const Scintilla::Line topLineBefore = topLine;
	if (input.action == PointerAction::Press && pdoc->TentativeActive()) {
		CancelTextInput();
	}
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
		} else if (input.button == 2) {
			suppressPrimarySelectionClaim = true;
			try {
				Scintilla::Internal::SelectionPosition position =
					SPositionFromLocation(point);
				position = MovePositionOutsideChar(
					position, sel.MainCaret() - position.Position());
				SetEmptySelection(position);
				RequestPrimarySelectionPaste(position.Position());
			} catch (...) {
				suppressPrimarySelectionClaim = false;
				throw;
			}
			suppressPrimarySelectionClaim = false;
		}
		break;
	case PointerAction::Release:
		if (input.button == 0) {
			ButtonUpWithModifiers(point, input.time, input.modifiers);
			if (pendingPrimaryClaim) {
				QueuePrimarySelectionClaim(
					std::move(pendingPrimaryClaim->text));
				pendingPrimaryClaim.reset();
			}
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
	if (sel.MainCaret() != caretBefore || sel.MainAnchor() != anchorBefore ||
		xOffset != xOffsetBefore || topLine != topLineBefore) {
		textInputStateDirty = true;
		textInputChangeCause = ApplicationTextChangeCause::Other;
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

std::vector<ApplicationClipboardResult> ApplicationEditor::TakeClipboardResults() {
	return std::exchange(clipboardResults, {});
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
		reportedStatus != ApplicationClipboardStatus::NoText &&
		reportedStatus != ApplicationClipboardStatus::Cancelled) {
		std::fprintf(stderr, "scalpel-editor clipboard: %s\n",
			ClipboardStatusName(reportedStatus));
	}
}

std::vector<ApplicationPrimarySelectionRequest>
ApplicationEditor::TakePrimarySelectionRequests() {
	return std::exchange(primarySelectionRequests, {});
}

std::vector<ApplicationPrimarySelectionResult>
ApplicationEditor::TakePrimarySelectionResults() {
	return std::exchange(primarySelectionResults, {});
}

void ApplicationEditor::HandlePrimarySelectionResult(uint64_t id,
	ApplicationPrimarySelectionOperation operation,
	ApplicationPrimarySelectionStatus status, std::string text) {
	ApplicationPrimarySelectionStatus reportedStatus = status;
	if (operation == ApplicationPrimarySelectionOperation::Publish &&
		id == latestPrimarySelectionClaimRequest &&
		status != ApplicationPrimarySelectionStatus::Published) {
		primarySelectionClaimed = false;
	}
	if (operation == ApplicationPrimarySelectionOperation::Paste &&
		status == ApplicationPrimarySelectionStatus::Complete) {
		if (!pendingPrimaryPaste || pendingPrimaryPaste->id != id ||
			pendingPrimaryPaste->documentGeneration != documentGeneration) {
			reportedStatus = ApplicationPrimarySelectionStatus::Superseded;
		} else if (text.empty()) {
			reportedStatus = ApplicationPrimarySelectionStatus::NoText;
		} else if (!CanPaste()) {
			reportedStatus = ApplicationPrimarySelectionStatus::NotApplied;
		} else {
			suppressPrimarySelectionClaim = true;
			try {
				SetEmptySelection(pendingPrimaryPaste->position);
				Scintilla::Internal::UndoGroup undoGroup(pdoc);
				ClearSelection(multiPasteMode == Scintilla::MultiPaste::Each);
				InsertPasteShape(text, PasteShape::stream);
				EnsureCaretVisible();
				Redraw();
			} catch (...) {
				suppressPrimarySelectionClaim = false;
				throw;
			}
			suppressPrimarySelectionClaim = false;
		}
	}
	if (operation == ApplicationPrimarySelectionOperation::Paste &&
		pendingPrimaryPaste && pendingPrimaryPaste->id == id &&
		status != ApplicationPrimarySelectionStatus::Published) {
		pendingPrimaryPaste.reset();
	}
	primarySelectionResults.push_back({id, operation, reportedStatus});
	if (reportedStatus != ApplicationPrimarySelectionStatus::Published &&
		reportedStatus != ApplicationPrimarySelectionStatus::Complete &&
		reportedStatus != ApplicationPrimarySelectionStatus::Superseded &&
		reportedStatus != ApplicationPrimarySelectionStatus::NoText &&
		reportedStatus != ApplicationPrimarySelectionStatus::Cancelled &&
		!(operation == ApplicationPrimarySelectionOperation::Publish &&
			reportedStatus == ApplicationPrimarySelectionStatus::Unavailable)) {
		std::fprintf(stderr, "scalpel-editor primary selection: %s\n",
			PrimarySelectionStatusName(reportedStatus));
	}
}

void ApplicationEditor::RenderFrame() {
	RenderFrame(TakeFrameDamage());
}

void ApplicationEditor::RenderFrame(const std::vector<PRectangle> &damage) {
	const int width = FrameWidth();
	const int height = FrameHeight();
	frame = Scintilla::Internal::CreateDrawSurface(*renderer, width, height);
	paintState = PaintState::painting;
	rcPaint = DamageBounds(damage, GetClientRectangle());
	paintingAllText = rcPaint == GetClientRectangle();
	try {
		Paint(frame.get(), rcPaint);
		if (overlayPainter) {
			overlayPainter(*frame, width, height);
		}
	} catch (...) {
		paintState = PaintState::notPainting;
		paintingAllText = false;
		throw;
	}
	paintState = PaintState::notPainting;
	paintingAllText = false;
}

void ApplicationEditor::PresentFrame() {
	if (!glContext->HasWindowSurface()) {
		throw std::runtime_error("ApplicationEditor::PresentFrame requires a window surface");
	}
	PresentFrame(TakeFrameDamage(), {}, true);
}

void ApplicationEditor::PresentFrame(
	const std::vector<PRectangle> &damage,
	const std::vector<int> &eglDamage, bool fullSwap) {
	if (!glContext->HasWindowSurface()) {
		throw std::runtime_error("ApplicationEditor::PresentFrame requires a window surface");
	}
	if (eglDamage.size() % 4 != 0) {
		throw std::invalid_argument(
			"ApplicationEditor::PresentFrame requires complete EGL rectangles");
	}
	const int width = FrameWidth();
	const int height = FrameHeight();
	frame = Scintilla::Internal::CreateExternalDrawSurface(
		*renderer, 0, bufferWidth, bufferHeight, width, height);
	paintState = PaintState::painting;
	rcPaint = DamageBounds(damage, GetClientRectangle());
	paintingAllText = rcPaint == GetClientRectangle();
	try {
		Paint(frame.get(), rcPaint);
		if (overlayPainter) {
			overlayPainter(*frame, width, height);
		}
	} catch (...) {
		paintState = PaintState::notPainting;
		paintingAllText = false;
		throw;
	}
	paintState = PaintState::notPainting;
	paintingAllText = false;
	if (fullSwap) {
		glContext->SwapBuffers();
	} else {
		glContext->SwapBuffersWithDamage(
			eglDamage.data(), eglDamage.size() / 4);
	}
}

void ApplicationEditor::SetOverlayPainter(OverlayPainter painter) {
	overlayPainter = std::move(painter);
}

void ApplicationEditor::InvalidateClient() {
	wMain.InvalidateAll();
}

std::vector<PRectangle> ApplicationEditor::TakeFrameDamage() {
	return std::exchange(window.invalidatedRectangles, {});
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

int ApplicationEditor::BufferAge() const noexcept {
	return glContext->BufferAge();
}

bool ApplicationEditor::BufferAgeSupported() const noexcept {
	return glContext->BufferAgeSupported();
}

bool ApplicationEditor::DamageSwapSupported() const noexcept {
	return glContext->DamageSwapSupported();
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
	if (suppressPrimarySelectionClaim) {
		return;
	}
	std::optional<std::string> text;
	if (!sel.Empty()) {
		Scintilla::Internal::SelectionText selectedText;
		CopySelectionRange(&selectedText, false);
		text = std::string(selectedText.AsView());
	}
	if (HaveMouseCapture()) {
		pendingPrimaryClaim = PendingPrimaryClaim{std::move(text)};
	} else {
		pendingPrimaryClaim.reset();
		QueuePrimarySelectionClaim(std::move(text));
	}
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

void ApplicationEditor::QueuePrimarySelectionClaim(
	std::optional<std::string> text) {
	if (!text && !primarySelectionClaimed) {
		return;
	}
	primarySelectionClaimed = text.has_value();
	const uint64_t request = nextPrimarySelectionRequest++;
	latestPrimarySelectionClaimRequest = request;
	primarySelectionRequests.push_back({
		request,
		ApplicationPrimarySelectionOperation::Publish,
		std::move(text),
	});
}

void ApplicationEditor::RequestPrimarySelectionPaste(
	Scintilla::Position position) {
	const uint64_t request = nextPrimarySelectionRequest++;
	pendingPrimaryPaste = PendingPrimaryPaste{
		request, documentGeneration, position};
	primarySelectionRequests.push_back({
		request,
		ApplicationPrimarySelectionOperation::Paste,
		std::nullopt,
	});
}

ApplicationTextInputState ApplicationEditor::BuildTextInputState() {
	constexpr Scintilla::Position MaximumSurroundingBytes = 4000;
	if (textInputPreeditRange && !pdoc->TentativeActive()) {
		textInputPreeditRange.reset();
	}
	ApplicationTextInputState state;
	const Scintilla::Position preeditStart = textInputPreeditRange ?
		textInputPreeditRange->start : 0;
	const Scintilla::Position preeditLength = textInputPreeditRange ?
		textInputPreeditRange->length : 0;
	const Scintilla::Position caret = textInputPreeditRange ?
		preeditStart : sel.MainCaret();
	const Scintilla::Position anchor = textInputPreeditRange ?
		preeditStart : sel.MainAnchor();
	const Scintilla::Position selectionStart = std::min(caret, anchor);
	const Scintilla::Position selectionEnd = std::max(caret, anchor);
	const Scintilla::Position selectionLength = selectionEnd - selectionStart;
	if (selectionLength <= MaximumSurroundingBytes) {
		const Scintilla::Position documentLength =
			pdoc->Length() - preeditLength;
		const Scintilla::Position spare =
			MaximumSurroundingBytes - selectionLength;
		Scintilla::Position start = std::max<Scintilla::Position>(
			0, selectionStart - spare / 2);
		Scintilla::Position end = std::min<Scintilla::Position>(
			documentLength, start + MaximumSurroundingBytes);
		if (end < selectionEnd) {
			end = selectionEnd;
			start = std::max<Scintilla::Position>(
				0, end - MaximumSurroundingBytes);
		}
		const auto actualPosition = [preeditStart, preeditLength](
			Scintilla::Position position) {
			return position <= preeditStart ?
				position : position + preeditLength;
		};
		const auto virtualPosition = [preeditStart, preeditLength](
			Scintilla::Position position) {
			return position <= preeditStart ?
				position : position - preeditLength;
		};
		start = virtualPosition(pdoc->MovePositionOutsideChar(
			actualPosition(start), 1, false));
		end = virtualPosition(pdoc->MovePositionOutsideChar(
			actualPosition(end), -1, false));
		if (start <= selectionStart && end >= selectionEnd) {
			if (!textInputPreeditRange || end <= preeditStart) {
				state.surroundingText = RangeText(start, end);
			} else if (start >= preeditStart) {
				state.surroundingText = RangeText(
					start + preeditLength, end + preeditLength);
			} else {
				state.surroundingText =
					RangeText(start, preeditStart) +
					RangeText(preeditStart + preeditLength,
						end + preeditLength);
			}
			state.cursor = static_cast<int32_t>(caret - start);
			state.anchor = static_cast<int32_t>(anchor - start);
		}
	}

	const Scintilla::Internal::Point caretPoint = PointMainCaret();
	const auto coordinate = [](Scintilla::Internal::XYPOSITION value) {
		const double minimum =
			static_cast<double>(std::numeric_limits<int32_t>::min());
		const double maximum =
			static_cast<double>(std::numeric_limits<int32_t>::max());
		return static_cast<int32_t>(std::clamp<double>(value, minimum, maximum));
	};
	state.cursorRectangle = {
		coordinate(caretPoint.x),
		coordinate(caretPoint.y),
		1,
		std::max(1, vs.lineHeight),
	};
	return state;
}

bool ApplicationEditor::DeleteTextInputSurrounding(
	const ApplicationTextInputDelete &deletion) {
	const Scintilla::Position selectionStart = SelectionStart().Position();
	const Scintilla::Position selectionEnd = SelectionEnd().Position();
	if (deletion.beforeLength >
			static_cast<uint64_t>(selectionStart) ||
		deletion.afterLength >
			static_cast<uint64_t>(pdoc->Length() - selectionEnd)) {
		return false;
	}
	const Scintilla::Position deleteStart =
		selectionStart - deletion.beforeLength;
	const Scintilla::Position deleteEnd =
		selectionEnd + deletion.afterLength;
	if (pdoc->MovePositionOutsideChar(deleteStart, 1, false) != deleteStart ||
		pdoc->MovePositionOutsideChar(deleteEnd, -1, false) != deleteEnd ||
		(deletion.beforeLength != 0 &&
			RangeContainsProtected(deleteStart, selectionStart)) ||
		(deletion.afterLength != 0 &&
			RangeContainsProtected(selectionEnd, deleteEnd))) {
		return false;
	}
	if (deletion.afterLength != 0 &&
		!pdoc->DeleteChars(selectionEnd, deletion.afterLength)) {
		return false;
	}
	if (deletion.beforeLength != 0 &&
		!pdoc->DeleteChars(deleteStart, deletion.beforeLength)) {
		return false;
	}
	return true;
}

void ApplicationEditor::CancelTextInput() {
	if (pdoc->TentativeActive()) {
		pdoc->TentativeUndo();
		ShowCaretAtCurrentPosition();
	}
	textInputPreeditRange.reset();
	textInputStateDirty = true;
	textInputChangeCause = ApplicationTextChangeCause::Other;
}

void ApplicationEditor::NotifyChange() {
	documentGeneration++;
	changeNotifications++;
	textInputStateDirty = true;
	textInputChangeCause = ApplicationTextChangeCause::Other;
}

void ApplicationEditor::NotifyParent(Scintilla::NotificationData notification) {
	if (notification.code == Scintilla::Notification::Modified &&
		(Scintilla::FlagSet(notification.modificationType,
			Scintilla::ModificationFlags::InsertText) ||
			Scintilla::FlagSet(notification.modificationType,
				Scintilla::ModificationFlags::DeleteText)) &&
		notification.linesAdded != 0) {
		UpdateLineNumberWidth();
	}
	notifications.push_back(notification.code);
}

void ApplicationEditor::NotifyCaretMove() {
	textInputStateDirty = true;
	textInputChangeCause = ApplicationTextChangeCause::Other;
}

void ApplicationEditor::UpdateSystemCaret() {
	textInputStateDirty = true;
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
