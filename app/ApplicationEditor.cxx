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
#include "ScrollBar.h"
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

bool DamageIntersects(const std::vector<PRectangle> &damage,
	PRectangle area) noexcept {
	if (damage.empty()) {
		return true;
	}
	for (const PRectangle &rectangle : damage) {
		if (rectangle.Intersects(area)) {
			return true;
		}
	}
	return false;
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
	// Grow the assumed horizontal width from measured visible lines rather than
	// scanning the whole document. Matches Scintilla's trackLineWidth path.
	SetScrollWidthTracking(true);
	RetainInitialDocument();
	RefreshScrollMetrics();
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
	SetScrollWidthTracking(true);
	RetainInitialDocument();
	RefreshScrollMetrics();
}

ApplicationEditor::~ApplicationEditor() {
	ReleaseRetainedDocuments();
	Finalise();
}

void ApplicationEditor::RetainInitialDocument() {
	// EditModel already owns pdoc with one reference; the table holds another.
	activeDocumentId = nextDocumentId++;
	RetainedDocument entry;
	entry.document = pdoc;
	pdoc->AddRef();
	retainedDocuments.emplace(activeDocumentId, std::move(entry));
}

void ApplicationEditor::ReleaseRetainedDocuments() {
	for (auto &[id, entry] : retainedDocuments) {
		(void)id;
		if (entry.document) {
			entry.document->Release();
			entry.document = nullptr;
		}
	}
	retainedDocuments.clear();
	activeDocumentId = 0;
}

ApplicationEditor::RetainedDocument *ApplicationEditor::FindRetained(DocumentId id) {
	const auto it = retainedDocuments.find(id);
	if (it == retainedDocuments.end()) {
		return nullptr;
	}
	return &it->second;
}

const ApplicationEditor::RetainedDocument *ApplicationEditor::FindRetained(
	DocumentId id) const {
	const auto it = retainedDocuments.find(id);
	if (it == retainedDocuments.end()) {
		return nullptr;
	}
	return &it->second;
}

void ApplicationEditor::SnapshotActiveView() {
	RetainedDocument *entry = FindRetained(activeDocumentId);
	if (!entry) {
		return;
	}
	entry->selection = GetSelectionSerialized();
	const Scintilla::Line firstVisibleLine = GetFirstVisibleLine();
	entry->firstVisibleLine = firstVisibleLine;
	entry->firstVisibleDocumentLine = pcs->DocFromDisplay(firstVisibleLine);
	entry->firstVisibleSubLine =
		firstVisibleLine - pcs->DisplayFromDoc(entry->firstVisibleDocumentLine);
	entry->xOffset = GetXOffset();
}

void ApplicationEditor::RestoreActiveView() {
	const RetainedDocument *entry = FindRetained(activeDocumentId);
	if (!entry) {
		return;
	}
	// Selection(std::string_view) leaves ranges empty for an empty string, so
	// never feed it "". New documents start with a caret at 0.
	if (entry->selection.empty()) {
		SetEmptySelection(0);
	} else {
		SetSelectionSerialized(entry->selection);
	}
	// SetDocPointer starts with one display line per document line until wrapping
	// is rebuilt. Restore the display line immediately where that temporary
	// layout permits it, then let WrapLines recover the exact document line and
	// wrapped subline if the display line had to be clamped.
	SetFirstVisibleLine(entry->firstVisibleLine);
	if (Wrapping()) {
		scrollToAfterWrap = LineDocSub{
			entry->firstVisibleDocumentLine, entry->firstVisibleSubLine};
	}
	// The retained offset may no longer fit after a resize, or wrapping may now
	// require a zero horizontal origin.
	ScrollHorizontalTo(entry->xOffset);
}

DocumentId ApplicationEditor::CreateDocument() {
	auto *document = new Scintilla::Internal::Document(
		Scintilla::DocumentOption::Default);
	// Table holds the only reference until the document is activated.
	document->AddRef();
	const DocumentId id = nextDocumentId++;
	RetainedDocument entry;
	entry.document = document;
	retainedDocuments.emplace(id, std::move(entry));
	return id;
}

DocumentId ApplicationEditor::ActiveDocument() const noexcept {
	return activeDocumentId;
}

bool ApplicationEditor::HasDocument(DocumentId id) const noexcept {
	return retainedDocuments.find(id) != retainedDocuments.end();
}

void ApplicationEditor::ActivateDocument(DocumentId id) {
	if (id == activeDocumentId) {
		return;
	}
	RetainedDocument *incoming = FindRetained(id);
	if (!incoming || !incoming->document) {
		throw std::invalid_argument(
			"ApplicationEditor::ActivateDocument requires a retained document");
	}
	// Cancel tentative IME on the outgoing buffer before snapshotting so the
	// saved selection and text do not include uncommitted preedit.
	CancelTextInput();
	SnapshotActiveView();
	documentGeneration++;
	SetDocPointer(incoming->document);
	activeDocumentId = id;
	RestoreActiveView();
	UpdateLineNumberWidth();
	textInputStateDirty = true;
	textInputChangeCause = ApplicationTextChangeCause::Other;
}

void ApplicationEditor::CloseDocument(DocumentId id) {
	if (id == activeDocumentId) {
		throw std::invalid_argument(
			"ApplicationEditor::CloseDocument cannot close the active document");
	}
	const auto it = retainedDocuments.find(id);
	if (it == retainedDocuments.end()) {
		throw std::invalid_argument(
			"ApplicationEditor::CloseDocument requires a retained document");
	}
	if (it->second.document) {
		it->second.document->Release();
		it->second.document = nullptr;
	}
	retainedDocuments.erase(it);
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

std::string ApplicationEditor::Text(DocumentId id) const {
	const RetainedDocument *entry = FindRetained(id);
	if (!entry || !entry->document) {
		throw std::invalid_argument(
			"ApplicationEditor::Text requires a retained document");
	}
	if (id == activeDocumentId) {
		return GetText();
	}
	const Scintilla::Position length = entry->document->Length();
	std::string text(static_cast<size_t>(length), '\0');
	if (length > 0) {
		entry->document->GetCharRange(text.data(), 0, length);
	}
	return text;
}

bool ApplicationEditor::Modified() const noexcept {
	return GetModify();
}

bool ApplicationEditor::Modified(DocumentId id) const {
	const RetainedDocument *entry = FindRetained(id);
	if (!entry || !entry->document) {
		throw std::invalid_argument(
			"ApplicationEditor::Modified requires a retained document");
	}
	if (id == activeDocumentId) {
		return GetModify();
	}
	return !entry->document->IsSavePoint();
}

void ApplicationEditor::MarkSaved() {
	SetSavePoint();
}

void ApplicationEditor::MarkSaved(DocumentId id) {
	RetainedDocument *entry = FindRetained(id);
	if (!entry || !entry->document) {
		throw std::invalid_argument(
			"ApplicationEditor::MarkSaved requires a retained document");
	}
	if (id == activeDocumentId) {
		SetSavePoint();
		return;
	}
	entry->document->SetSavePoint();
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

const char *EditorFontFamilyName(EditorFont font) noexcept {
	switch (font) {
	case EditorFont::Monospace:
		return "monospace";
	case EditorFont::Serif:
		return "serif";
	case EditorFont::Sans:
		return "sans-serif";
	case EditorFont::System:
		return "system-ui";
	}
	return "system-ui";
}

EditorFont ApplicationEditor::CurrentEditorFont() const noexcept {
	return editorFont;
}

void ApplicationEditor::SetEditorFont(EditorFont font) {
	if (font == editorFont) {
		return;
	}
	editorFont = font;
	// STYLE_DEFAULT is the template StyleClearAll copies onto every style.
	const int defaultStyle = static_cast<int>(Scintilla::StylesCommon::Default);
	StyleSetFont(defaultStyle, EditorFontFamilyName(font));
	// Propagate default to plain-text styles, then restore the monospace gutter.
	ApplyLineNumberStyle();
	UpdateLineNumberWidth();
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

void ApplicationEditor::SetTopChromeInset(int logicalPixels) {
	if (logicalPixels < 0) {
		throw std::invalid_argument(
			"ApplicationEditor::SetTopChromeInset requires a non-negative inset");
	}
	if (topChromeInset == logicalPixels) {
		return;
	}
	topChromeInset = logicalPixels;
	DropGraphics();
	frame.reset();
	ChangeSize();
	wMain.InvalidateAll();
	textInputStateDirty = true;
}

PRectangle ApplicationEditor::FrameRectangle() const noexcept {
	return PRectangle::FromInts(0, 0, FrameWidth(), FrameHeight());
}

PRectangle ApplicationEditor::TopChromeRectangle() const noexcept {
	if (topChromeInset <= 0) {
		return PRectangle::FromInts(0, 0, 0, 0);
	}
	const int height = FrameHeight();
	const int inset = std::min(topChromeInset, height);
	return PRectangle::FromInts(0, 0, FrameWidth(), inset);
}

PRectangle ApplicationEditor::VerticalScrollBarRectangle() const noexcept {
	const ScrollBarLayout layout = LayoutScrollBars(FrameWidth(), FrameHeight(),
		topChromeInset, scrollbars.vertical, scrollbars.horizontal);
	return layout.vertical.track;
}

PRectangle ApplicationEditor::HorizontalScrollBarRectangle() const noexcept {
	const ScrollBarLayout layout = LayoutScrollBars(FrameWidth(), FrameHeight(),
		topChromeInset, scrollbars.vertical, scrollbars.horizontal);
	return layout.horizontal.track;
}

PRectangle ApplicationEditor::JunctionRectangle() const noexcept {
	const ScrollBarLayout layout = LayoutScrollBars(FrameWidth(), FrameHeight(),
		topChromeInset, scrollbars.vertical, scrollbars.horizontal);
	return layout.junction;
}

PRectangle ApplicationEditor::GetClientRectangle() const {
	const int width = FrameWidth();
	const int height = FrameHeight();
	// Keep at least one logical pixel of editor size when the frame permits it.
	const int top = topChromeInset > 0 ?
		std::min(topChromeInset, std::max(0, height - 1)) : 0;
	int rightInset = 0;
	int bottomInset = 0;
	if (scrollbars.vertical.visible && width > 1) {
		rightInset = std::min(ScrollBarThickness(), width - 1);
	}
	if (scrollbars.horizontal.visible) {
		const int available = std::max(0, height - top);
		if (available > 1) {
			bottomInset = std::min(ScrollBarThickness(), available - 1);
		}
	}
	// When both bars claim space, still leave a 1x1 client if possible.
	if (rightInset > 0 && bottomInset > 0) {
		if (width - rightInset < 1) {
			rightInset = std::max(0, width - 1);
		}
		if (height - top - bottomInset < 1) {
			bottomInset = std::max(0, height - top - 1);
		}
	}
	return PRectangle::FromInts(0, top, width - rightInset, height - bottomInset);
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
				ScrollHorizontalTo(static_cast<int>(xOffset) + pixels);
			}
		} else {
			verticalWheelRemainder += input.deltaY * 0.3;
			const Scintilla::Line lines = static_cast<Scintilla::Line>(verticalWheelRemainder);
			verticalWheelRemainder -= lines;
			if (lines != 0) {
				ScrollVerticalTo(topLine + lines);
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

void ApplicationEditor::ExecuteApplicationEdit(
	Scintilla::Internal::EditorCommand command) {
	const Scintilla::Position caretBefore = sel.MainCaret();
	const Scintilla::Position anchorBefore = sel.MainAnchor();
	const int xOffsetBefore = xOffset;
	const Scintilla::Line topLineBefore = topLine;
	if (pdoc->TentativeActive()) {
		CancelTextInput();
	}
	ExecuteCommand(command);
	if (sel.MainCaret() != caretBefore || sel.MainAnchor() != anchorBefore ||
		xOffset != xOffsetBefore || topLine != topLineBefore) {
		textInputStateDirty = true;
		textInputChangeCause = ApplicationTextChangeCause::Other;
	}
}

void ApplicationEditor::RequestUndo() {
	ExecuteApplicationEdit(Scintilla::Internal::EditorCommand::Undo);
}

void ApplicationEditor::RequestRedo() {
	ExecuteApplicationEdit(Scintilla::Internal::EditorCommand::Redo);
}

void ApplicationEditor::RequestCut() {
	ExecuteApplicationEdit(Scintilla::Internal::EditorCommand::Cut);
}

void ApplicationEditor::RequestSelectAll() {
	ExecuteApplicationEdit(Scintilla::Internal::EditorCommand::SelectAll);
}

void ApplicationEditor::ConvertLineEndings(Scintilla::EndOfLine lineEnding) {
	if (pdoc->TentativeActive()) {
		CancelTextInput();
	}
	ConvertEOLs(lineEnding);
	textInputStateDirty = true;
	textInputChangeCause = ApplicationTextChangeCause::Other;
}

void ApplicationEditor::RequestClipboardCopy() {
	ExecuteApplicationEdit(Scintilla::Internal::EditorCommand::Copy);
}

void ApplicationEditor::RequestClipboardPaste() {
	ExecuteApplicationEdit(Scintilla::Internal::EditorCommand::Paste);
}

ApplicationFindOutcome ApplicationEditor::FindTextForward(
	std::string_view query, Scintilla::Position origin) {
	if (query.empty()) {
		return ApplicationFindOutcome::NotFound;
	}
	// Drop tentative IME so target endpoints and selection stay committed.
	if (pdoc->TentativeActive()) {
		CancelTextInput();
	}

	const Scintilla::Position length = GetTextLength();
	if (origin < 0) {
		origin = 0;
	}
	if (origin > length) {
		origin = length;
	}

	const Scintilla::Position selStart = GetSelectionStart();
	const Scintilla::Position selEnd = GetSelectionEnd();
	SetSearchFlags(Scintilla::FindOption::None);

	const auto selectMatch = [this]() {
		SetSel(GetTargetStart(), GetTargetEnd());
		EnsureCaretVisible();
		textInputStateDirty = true;
		textInputChangeCause = ApplicationTextChangeCause::Other;
	};

	// First range: origin .. end.
	SetTargetRange(origin, length);
	if (SearchInTarget(query) >= 0) {
		selectMatch();
		return ApplicationFindOutcome::Found;
	}
	// Complementary wrap: start .. origin.
	if (origin > 0) {
		SetTargetRange(0, origin);
		if (SearchInTarget(query) >= 0) {
			selectMatch();
			return ApplicationFindOutcome::Wrapped;
		}
	}

	if (GetSelectionStart() != selStart || GetSelectionEnd() != selEnd) {
		SetSel(selStart, selEnd);
	}
	return ApplicationFindOutcome::NotFound;
}

ApplicationFindOutcome ApplicationEditor::FindTextBackward(
	std::string_view query, Scintilla::Position origin) {
	if (query.empty()) {
		return ApplicationFindOutcome::NotFound;
	}
	if (pdoc->TentativeActive()) {
		CancelTextInput();
	}

	const Scintilla::Position length = GetTextLength();
	if (origin < 0) {
		origin = 0;
	}
	if (origin > length) {
		origin = length;
	}

	const Scintilla::Position selStart = GetSelectionStart();
	const Scintilla::Position selEnd = GetSelectionEnd();
	SetSearchFlags(Scintilla::FindOption::None);

	const auto selectMatch = [this]() {
		SetSel(GetTargetStart(), GetTargetEnd());
		EnsureCaretVisible();
		textInputStateDirty = true;
		textInputChangeCause = ApplicationTextChangeCause::Other;
	};

	// First range: origin .. start (backward target order).
	if (origin > 0) {
		SetTargetRange(origin, 0);
		if (SearchInTarget(query) >= 0) {
			selectMatch();
			return ApplicationFindOutcome::Found;
		}
	}
	// Complementary wrap: end .. origin.
	if (origin < length) {
		SetTargetRange(length, origin);
		if (SearchInTarget(query) >= 0) {
			selectMatch();
			return ApplicationFindOutcome::Wrapped;
		}
	}

	if (GetSelectionStart() != selStart || GetSelectionEnd() != selEnd) {
		SetSel(selStart, selEnd);
	}
	return ApplicationFindOutcome::NotFound;
}

ApplicationFindOutcome ApplicationEditor::FindTextForwardFromSelection(
	std::string_view query) {
	return FindTextForward(query, GetSelectionEnd());
}

ApplicationFindOutcome ApplicationEditor::FindTextBackwardFromSelection(
	std::string_view query) {
	return FindTextBackward(query, GetSelectionStart());
}

void ApplicationEditor::SetClipboardPasteAvailable(bool available) noexcept {
	clipboardPasteAvailable = available;
}

bool ApplicationEditor::ClipboardPasteAvailable() {
	return clipboardPasteAvailable && CanPaste();
}

bool ApplicationEditor::HasSelection() const noexcept {
	return !GetSelectionEmpty();
}

bool ApplicationEditor::CanSelectAll() const noexcept {
	return GetTextLength() > 0;
}

bool ApplicationEditor::CanUndoEdit() const noexcept {
	return CanUndo();
}

bool ApplicationEditor::CanRedoEdit() const noexcept {
	return CanRedo();
}

bool ApplicationEditor::CanCut() const noexcept {
	return !GetReadOnly() && HasSelection();
}

bool ApplicationEditor::CanCopy() const noexcept {
	return HasSelection();
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
	const PRectangle client = GetClientRectangle();
	const bool paintOverlay = static_cast<bool>(overlayPainter);
	// Modal overlay spans the full frame (tabs + editor + scrollbars).
	const bool paintEditor = paintOverlay || DamageIntersects(damage, client);
	const bool paintChrome = permanentChromePainter && PermanentChromePresent() &&
		(paintOverlay || DamageIntersectsPermanentChrome(damage));
	if (paintOverlay) {
		rcPaint = client;
	} else if (paintEditor) {
		rcPaint = DamageBounds(damage, client);
	} else {
		rcPaint = PRectangle::FromInts(0, 0, 0, 0);
	}
	paintingAllText = paintEditor && (rcPaint == client);
	try {
		if (paintEditor) {
			Paint(frame.get(), rcPaint);
		}
		if (paintChrome) {
			permanentChromePainter(*frame, width, height);
		}
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
	const PRectangle client = GetClientRectangle();
	const bool paintOverlay = static_cast<bool>(overlayPainter);
	const bool paintEditor = paintOverlay || DamageIntersects(damage, client);
	const bool paintChrome = permanentChromePainter && PermanentChromePresent() &&
		(paintOverlay || DamageIntersectsPermanentChrome(damage));
	// Overlay alpha (scrim) must not re-blend outside the reported EGL damage.
	if (paintOverlay) {
		rcPaint = client;
		fullSwap = true;
	} else if (paintEditor) {
		rcPaint = DamageBounds(damage, client);
	} else {
		rcPaint = PRectangle::FromInts(0, 0, 0, 0);
	}
	paintingAllText = paintEditor && (rcPaint == client);
	try {
		if (paintEditor) {
			Paint(frame.get(), rcPaint);
		}
		if (paintChrome) {
			permanentChromePainter(*frame, width, height);
		}
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

void ApplicationEditor::SetOverlayPainter(OverlayPainter painter) noexcept {
	overlayPainter = std::move(painter);
}

void ApplicationEditor::SetPermanentChromePainter(
	PermanentChromePainter painter) noexcept {
	permanentChromePainter = std::move(painter);
}

void ApplicationEditor::InvalidateTopChrome() {
	const PRectangle chrome = TopChromeRectangle();
	if (chrome.Empty()) {
		return;
	}
	window.invalidatedRectangles.push_back(chrome);
}

void ApplicationEditor::InvalidateScrollBars() {
	InvalidateVerticalScrollBar();
	InvalidateHorizontalScrollBar();
	const PRectangle junction = JunctionRectangle();
	if (!junction.Empty()) {
		window.invalidatedRectangles.push_back(junction);
	}
}

void ApplicationEditor::InvalidateVerticalScrollBar() {
	const PRectangle bar = VerticalScrollBarRectangle();
	if (bar.Empty()) {
		return;
	}
	window.invalidatedRectangles.push_back(bar);
}

void ApplicationEditor::InvalidateHorizontalScrollBar() {
	const PRectangle bar = HorizontalScrollBarRectangle();
	if (bar.Empty()) {
		return;
	}
	window.invalidatedRectangles.push_back(bar);
}

void ApplicationEditor::InvalidateClient() {
	const PRectangle client = GetClientRectangle();
	if (client.Empty()) {
		return;
	}
	window.invalidatedRectangles.push_back(client);
}

void ApplicationEditor::InvalidateFrame() {
	window.invalidatedRectangles.push_back(
		PRectangle::FromInts(0, 0, FrameWidth(), FrameHeight()));
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
	const ScrollAxisMetrics before = scrollbars.horizontal;
	RefreshScrollMetrics();
	if (scrollbars.horizontal.visible &&
		(scrollbars.horizontal.position != before.position ||
			scrollbars.horizontal.upperBound != before.upperBound ||
			scrollbars.horizontal.pageSize != before.pageSize)) {
		// Geometry is stable; repaint only this bar's track and thumb.
		InvalidateHorizontalScrollBar();
	}
}

void ApplicationEditor::SetVerticalScrollPos() {
	Editor::SetVerticalScrollPos();
	const ScrollAxisMetrics before = scrollbars.vertical;
	RefreshScrollMetrics();
	if (scrollbars.vertical.visible &&
		(scrollbars.vertical.position != before.position ||
			scrollbars.vertical.upperBound != before.upperBound ||
			scrollbars.vertical.pageSize != before.pageSize)) {
		InvalidateVerticalScrollBar();
	}
}

bool ApplicationEditor::ModifyScrollBars(Scintilla::Line /*maximum*/, Scintilla::Line /*page*/) {
	const ScrollAxisMetrics verticalBefore = scrollbars.vertical;
	const ScrollAxisMetrics horizontalBefore = scrollbars.horizontal;
	RefreshScrollMetrics();

	// Clamp a stale horizontal origin after the assumed width or page shrinks.
	if (!Wrapping()) {
		const int upper = static_cast<int>(scrollbars.horizontal.upperBound);
		if (xOffset > upper) {
			HorizontalScrollTo(upper);
		}
	}

	const bool verticalChanged =
		scrollbars.vertical.position != verticalBefore.position ||
		scrollbars.vertical.upperBound != verticalBefore.upperBound ||
		scrollbars.vertical.pageSize != verticalBefore.pageSize ||
		scrollbars.vertical.pageIncrement != verticalBefore.pageIncrement ||
		scrollbars.vertical.visible != verticalBefore.visible;
	const bool horizontalChanged =
		scrollbars.horizontal.position != horizontalBefore.position ||
		scrollbars.horizontal.upperBound != horizontalBefore.upperBound ||
		scrollbars.horizontal.pageSize != horizontalBefore.pageSize ||
		scrollbars.horizontal.pageIncrement != horizontalBefore.pageIncrement ||
		scrollbars.horizontal.visible != horizontalBefore.visible;

	// Inset changes already full-frame invalidate via ReconfigureScrollBars.
	if (scrollbars.vertical.visible == verticalBefore.visible &&
		scrollbars.horizontal.visible == horizontalBefore.visible) {
		if (verticalChanged && scrollbars.vertical.visible) {
			InvalidateVerticalScrollBar();
		}
		if (horizontalChanged && scrollbars.horizontal.visible) {
			InvalidateHorizontalScrollBar();
		}
	}
	return verticalChanged || horizontalChanged;
}

void ApplicationEditor::ReconfigureScrollBars() {
	// SetWrapMode already clears xOffset before calling here.
	RefreshScrollMetrics();
	// Scintilla also calls this after SetScrollBars has already refreshed the
	// new visibility, so the previous inset cannot be recovered from metrics.
	// The hook itself means the configuration changed: rebuild client geometry.
	DropGraphics();
	frame.reset();
	ChangeSize();
	wMain.InvalidateAll();
	textInputStateDirty = true;
}

bool ApplicationEditor::PermanentChromePresent() const noexcept {
	return topChromeInset > 0 || scrollbars.vertical.visible ||
		scrollbars.horizontal.visible;
}

bool ApplicationEditor::DamageIntersectsPermanentChrome(
	const std::vector<PRectangle> &damage) const noexcept {
	if (damage.empty()) {
		return true;
	}
	if (DamageIntersects(damage, TopChromeRectangle())) {
		return true;
	}
	if (DamageIntersects(damage, VerticalScrollBarRectangle())) {
		return true;
	}
	if (DamageIntersects(damage, HorizontalScrollBarRectangle())) {
		return true;
	}
	if (DamageIntersects(damage, JunctionRectangle())) {
		return true;
	}
	return false;
}

Scintilla::Line ApplicationEditor::HorizontalUpperBound() const {
	if (Wrapping()) {
		return 0;
	}
	const PRectangle text = GetTextRectangle();
	const int textWidth = std::max(0, static_cast<int>(text.Width()));
	const int assumed = GetScrollWidth();
	return std::max(Scintilla::Line{0},
		static_cast<Scintilla::Line>(assumed) - textWidth);
}

void ApplicationEditor::RefreshScrollMetrics() {
	ScrollAxisMetrics vertical;
	vertical.position = topLine;
	vertical.upperBound = MaxScrollPos();
	vertical.pageSize = std::max(Scintilla::Line{0}, LinesOnScreen());
	vertical.pageIncrement = LinesToScroll();
	vertical.visible = GetVScrollBar();

	ScrollAxisMetrics horizontal;
	const PRectangle text = GetTextRectangle();
	const int textWidth = std::max(0, static_cast<int>(text.Width()));
	horizontal.position = xOffset;
	horizontal.upperBound = HorizontalUpperBound();
	horizontal.pageSize = textWidth;
	// One third of the visible text width, at least one pixel when a page exists.
	if (textWidth > 0) {
		horizontal.pageIncrement = std::max(1, textWidth / 3);
	} else {
		horizontal.pageIncrement = 1;
	}
	horizontal.visible = GetHScrollBar() && !Wrapping();

	scrollbars.vertical = vertical;
	scrollbars.horizontal = horizontal;
}

void ApplicationEditor::ScrollVerticalTo(Scintilla::Line line) {
	ScrollTo(line);
	RefreshScrollMetrics();
}

void ApplicationEditor::ScrollHorizontalTo(int xPos) {
	if (Wrapping()) {
		if (xOffset != 0) {
			SetXOffset(0);
		}
		RefreshScrollMetrics();
		return;
	}
	const int upper = static_cast<int>(HorizontalUpperBound());
	const int clamped = std::clamp(xPos, 0, upper);
	if (clamped != xOffset) {
		// HorizontalScrollTo only clamps the lower bound; keep the upper here.
		HorizontalScrollTo(clamped);
	}
	RefreshScrollMetrics();
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

void ApplicationEditor::CancelActiveTextInput() {
	CancelTextInput();
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
