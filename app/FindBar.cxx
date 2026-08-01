#include "FindBar.h"

#include <algorithm>
#include <limits>

#include "UniConversion.h"

namespace Scalpel {

namespace {

using Scintilla::Internal::ColourRGBA;
using Scintilla::Internal::Fill;
using Scintilla::Internal::Font;
using Scintilla::Internal::FontParameters;
using Scintilla::Internal::PRectangle;
using Scintilla::Internal::Point;
using Scintilla::Internal::Surface;
using Scintilla::Internal::UTF8BytesOfLead;
using Scintilla::Internal::UTF8IsValid;
using Scintilla::Internal::XYPOSITION;

bool NonEmpty(const PRectangle &rc) noexcept {
	return rc.right > rc.left && rc.bottom > rc.top;
}

bool NonEmptyContains(const PRectangle &rc, Point point) noexcept {
	return NonEmpty(rc) &&
		point.x >= rc.left && point.x < rc.right &&
		point.y >= rc.top && point.y < rc.bottom;
}

int SaturatingAdd(int value, int delta) noexcept {
	const int64_t result = static_cast<int64_t>(value) + delta;
	return static_cast<int>(std::clamp(
		result,
		static_cast<int64_t>(std::numeric_limits<int>::min()),
		static_cast<int64_t>(std::numeric_limits<int>::max())));
}

bool IsUtf8Boundary(std::string_view text, std::size_t index) noexcept {
	if (index > text.size()) {
		return false;
	}
	if (index == text.size() || index == 0) {
		return true;
	}
	return (static_cast<unsigned char>(text[index]) & 0xC0) != 0x80;
}

std::size_t Utf8Floor(std::string_view text, std::size_t index) noexcept {
	if (index >= text.size()) {
		return text.size();
	}
	while (index > 0 &&
		(static_cast<unsigned char>(text[index]) & 0xC0) == 0x80) {
		--index;
	}
	return index;
}

std::size_t Utf8Next(std::string_view text, std::size_t index) noexcept {
	if (index >= text.size()) {
		return text.size();
	}
	const unsigned char lead = static_cast<unsigned char>(text[index]);
	const std::size_t width = UTF8BytesOfLead[lead];
	if (width == 0) {
		return std::min(text.size(), index + 1);
	}
	return std::min(text.size(), index + width);
}

std::size_t Utf8Prev(std::string_view text, std::size_t index) noexcept {
	if (index == 0) {
		return 0;
	}
	return Utf8Floor(text, index - 1);
}

/** Display string: committed query with preedit replacing the selection. */
std::string DisplayText(const FindBarModel &model) {
	if (!model.preedit || model.preedit->text.empty()) {
		return model.query;
	}
	std::string display;
	display.reserve(model.query.size() + model.preedit->text.size());
	display.append(model.query, 0, model.SelectionStart());
	display.append(model.preedit->text);
	display.append(model.query, model.SelectionEnd(), std::string::npos);
	return display;
}

/** Caret byte offset into DisplayText. */
std::size_t DisplayCaret(const FindBarModel &model) noexcept {
	if (!model.preedit || model.preedit->text.empty()) {
		return model.caret;
	}
	const std::size_t start = model.SelectionStart();
	if (model.preedit->cursorBegin < 0) {
		return start + model.preedit->text.size();
	}
	const std::size_t cursor = static_cast<std::size_t>(std::clamp<int32_t>(
		model.preedit->cursorBegin, 0,
		static_cast<int32_t>(model.preedit->text.size())));
	return start + cursor;
}

std::size_t DisplayAnchor(const FindBarModel &model) noexcept {
	if (!model.preedit || model.preedit->text.empty()) {
		return model.anchor;
	}
	const std::size_t start = model.SelectionStart();
	if (model.preedit->cursorEnd < 0) {
		return start;
	}
	const std::size_t cursor = static_cast<std::size_t>(std::clamp<int32_t>(
		model.preedit->cursorEnd, 0,
		static_cast<int32_t>(model.preedit->text.size())));
	return start + cursor;
}

XYPOSITION TextWidth(Surface &surface, const Font *font,
	std::string_view text) {
	if (!font || text.empty()) {
		return 0.0;
	}
	return surface.WidthText(font, text);
}

/**
 * Horizontal scroll so the caret stays inside the field text rectangle.
 * Returns pixels of content scrolled off the left edge.
 */
XYPOSITION FieldScrollX(Surface &surface, const Font *font,
	const FindBarModel &model, const FindBarLayout &layout) {
	if (!NonEmpty(layout.fieldText) || !font) {
		return 0.0;
	}
	const std::string display = DisplayText(model);
	const std::size_t caret = DisplayCaret(model);
	const XYPOSITION caretX = TextWidth(surface, font,
		std::string_view(display).substr(0, caret));
	const XYPOSITION viewWidth = layout.fieldText.Width();
	if (viewWidth <= 0.0) {
		return 0.0;
	}
	// Prefer showing the caret with a small trailing margin.
	const XYPOSITION margin = 2.0;
	XYPOSITION scroll = 0.0;
	if (caretX + margin > viewWidth) {
		scroll = caretX + margin - viewWidth;
	}
	const XYPOSITION total = TextWidth(surface, font, display);
	if (total > viewWidth) {
		scroll = std::min(scroll, total - viewWidth);
	} else {
		scroll = 0.0;
	}
	if (scroll < 0.0) {
		scroll = 0.0;
	}
	return scroll;
}

void MarkQueryChanged(FindBarModel &model) noexcept {
	model.status = FindBarStatus::None;
	model.textInputStateDirty = true;
	model.textInputChangeCause = ApplicationTextChangeCause::Other;
}

void MarkTextInputDirty(FindBarModel &model,
	ApplicationTextChangeCause cause) noexcept {
	model.textInputStateDirty = true;
	model.textInputChangeCause = cause;
}

bool DeleteSurrounding(FindBarModel &model,
	const ApplicationTextInputDelete &deletion) {
	const std::size_t selectionStart = model.SelectionStart();
	const std::size_t selectionEnd = model.SelectionEnd();
	if (deletion.beforeLength > selectionStart ||
		deletion.afterLength > model.query.size() - selectionEnd) {
		return false;
	}
	const std::size_t deleteStart = selectionStart - deletion.beforeLength;
	const std::size_t deleteEnd = selectionEnd + deletion.afterLength;
	if (!IsUtf8Boundary(model.query, deleteStart) ||
		!IsUtf8Boundary(model.query, deleteEnd)) {
		return false;
	}
	model.query.erase(deleteStart, deleteEnd - deleteStart);
	model.caret = deleteStart;
	model.anchor = deleteStart;
	model.ClampCaretAndAnchor();
	return true;
}

FindBarRequest MakeSearchRequest(FindBarRequestKind kind) noexcept {
	FindBarRequest request;
	request.kind = kind;
	return request;
}

FindBarRequest MakeCloseRequest() noexcept {
	return MakeSearchRequest(FindBarRequestKind::Close);
}

FindBarRequest MakeClipboardCopy(FindBarModel &model, std::string text) {
	FindBarRequest request;
	request.kind = FindBarRequestKind::ClipboardCopy;
	request.clipboardId = model.nextClipboardRequest++;
	request.clipboardText = std::move(text);
	return request;
}

FindBarRequest MakeClipboardPaste(FindBarModel &model) {
	FindBarRequest request;
	request.kind = FindBarRequestKind::ClipboardPaste;
	request.clipboardId = model.nextClipboardRequest++;
	return request;
}

std::optional<FindBarPressKind> PressKindFromHit(FindBarHit hit) noexcept {
	switch (hit) {
	case FindBarHit::Previous:
		return FindBarPressKind::Previous;
	case FindBarHit::Next:
		return FindBarPressKind::Next;
	case FindBarHit::Close:
		return FindBarPressKind::Close;
	default:
		return std::nullopt;
	}
}

FindBarHit HitFromPressKind(FindBarPressKind kind) noexcept {
	switch (kind) {
	case FindBarPressKind::Previous:
		return FindBarHit::Previous;
	case FindBarPressKind::Next:
		return FindBarHit::Next;
	case FindBarPressKind::Close:
		return FindBarHit::Close;
	}
	return FindBarHit::None;
}

FindBarRequestKind RequestFromPressKind(FindBarPressKind kind) noexcept {
	switch (kind) {
	case FindBarPressKind::Previous:
		return FindBarRequestKind::SearchBackward;
	case FindBarPressKind::Next:
		return FindBarRequestKind::SearchForward;
	case FindBarPressKind::Close:
		return FindBarRequestKind::Close;
	}
	return FindBarRequestKind::Close;
}

void DrawCloseGlyph(Surface &surface, const PRectangle &rc, ColourRGBA ink) {
	if (rc.Empty()) {
		return;
	}
	const XYPOSITION inset = 7.0;
	const Point a(rc.left + inset, rc.top + inset);
	const Point b(rc.right - inset, rc.bottom - inset);
	const Point c(rc.right - inset, rc.top + inset);
	const Point d(rc.left + inset, rc.bottom - inset);
	surface.LineDraw(a, b, Scintilla::Internal::Stroke(ink, 1.5));
	surface.LineDraw(c, d, Scintilla::Internal::Stroke(ink, 1.5));
}

void DrawButton(Surface &surface, const PRectangle &rc, const Font *font,
	std::string_view label, bool pressed, bool closeGlyph,
	const UiStyle &style) {
	if (!NonEmpty(rc)) {
		return;
	}
	const ColourRGBA fill = pressed ? style.findButtonPressedFill :
		style.findButtonFill;
	surface.FillRectangle(rc, Fill(fill));
	if (closeGlyph) {
		DrawCloseGlyph(surface, rc, style.tabGlyphInk);
	} else {
		DrawCenteredLabel(surface, rc, font, label, style.text);
	}
	DrawInsideFrame(surface, rc, style.chromeBorder, 1.0);
}

}

int FindBarHeight() noexcept {
	return DefaultUiStyle().findBarHeight;
}

std::size_t FindBarModel::SelectionStart() const noexcept {
	return std::min(caret, anchor);
}

std::size_t FindBarModel::SelectionEnd() const noexcept {
	return std::max(caret, anchor);
}

bool FindBarModel::HasSelection() const noexcept {
	return caret != anchor;
}

void FindBarModel::ClampCaretAndAnchor() noexcept {
	if (caret > query.size()) {
		caret = query.size();
	}
	if (anchor > query.size()) {
		anchor = query.size();
	}
	caret = Utf8Floor(query, caret);
	anchor = Utf8Floor(query, anchor);
}

void FindBarModel::SelectAll() noexcept {
	ClampCaretAndAnchor();
	anchor = 0;
	caret = query.size();
}

void FindBarModel::CollapseSelectionToCaret() noexcept {
	ClampCaretAndAnchor();
	anchor = caret;
}

void FindBarModel::SetFocused(bool nowFocused) noexcept {
	if (focused == nowFocused) {
		if (nowFocused) {
			SelectAll();
			CancelPreedit();
			MarkTextInputDirty(*this, ApplicationTextChangeCause::Other);
		}
		return;
	}
	focused = nowFocused;
	if (focused) {
		SelectAll();
		CancelPreedit();
		MarkTextInputDirty(*this, ApplicationTextChangeCause::Other);
	} else {
		CancelPreedit();
		pressOrigin.reset();
		MarkTextInputDirty(*this, ApplicationTextChangeCause::Other);
	}
}

void FindBarModel::CancelPreedit() noexcept {
	if (preedit) {
		preedit.reset();
		MarkTextInputDirty(*this, ApplicationTextChangeCause::InputMethod);
	}
}

void FindBarModel::SetStatus(FindBarStatus newStatus) noexcept {
	status = newStatus;
}

bool FindBarModel::SetQuery(std::string_view text) {
	if (!text.empty() && !UTF8IsValid(text)) {
		return false;
	}
	query.assign(text);
	caret = query.size();
	anchor = 0;
	if (!focused) {
		anchor = caret;
	}
	CancelPreedit();
	MarkQueryChanged(*this);
	ClampCaretAndAnchor();
	if (focused) {
		SelectAll();
	}
	return true;
}

void FindBarModel::DeleteSelection() noexcept {
	ClampCaretAndAnchor();
	if (!HasSelection()) {
		return;
	}
	const std::size_t start = SelectionStart();
	const std::size_t end = SelectionEnd();
	query.erase(start, end - start);
	caret = start;
	anchor = start;
	MarkQueryChanged(*this);
}

bool FindBarModel::InsertText(std::string_view text) {
	if (!text.empty() && !UTF8IsValid(text)) {
		return false;
	}
	CancelPreedit();
	ClampCaretAndAnchor();
	const std::size_t start = SelectionStart();
	const std::size_t end = SelectionEnd();
	query.replace(start, end - start, text);
	caret = start + text.size();
	anchor = caret;
	MarkQueryChanged(*this);
	return true;
}

std::string FindBarModel::SelectedText() const {
	if (!HasSelection()) {
		return {};
	}
	return query.substr(SelectionStart(), SelectionEnd() - SelectionStart());
}

FindBarLayout LayoutFindBar(int bandWidth, int bandTop) noexcept {
	const UiStyle &style = DefaultUiStyle();
	FindBarLayout layout{
		PRectangle{},
		PRectangle{},
		PRectangle{},
		PRectangle{},
		PRectangle{},
		PRectangle{},
		PRectangle{},
	};
	if (bandWidth <= 0) {
		return layout;
	}
	const int height = style.findBarHeight;
	const int bandBottom = SaturatingAdd(bandTop, height);
	layout.band = PRectangle::FromInts(0, bandTop, bandWidth, bandBottom);

	const int innerTop = SaturatingAdd(bandTop, style.findBarPadY);
	const int innerBottom = std::max(innerTop,
		SaturatingAdd(bandBottom, -style.findBarPadY));
	const int pad = style.findBarPadX;
	const int gap = style.findButtonGap;

	// Place controls right-to-left so buttons win over status and field.
	int right = bandWidth - pad;
	const auto takeRight = [&](int desired) -> PRectangle {
		if (right <= pad || desired <= 0) {
			return PRectangle::FromInts(0, 0, 0, 0);
		}
		const int available = right - pad;
		const int width = std::min(desired, available);
		if (width <= 0) {
			return PRectangle::FromInts(0, 0, 0, 0);
		}
		const int left = right - width;
		right = left - gap;
		return PRectangle::FromInts(left, innerTop, left + width, innerBottom);
	};

	layout.closeButton = takeRight(style.findCloseWidth);
	layout.nextButton = takeRight(style.findButtonWidth);
	layout.previousButton = takeRight(style.findButtonWidth);

	// Status sits between the field and Previous when there is room.
	const int statusBudget = right - pad - style.findFieldMinWidth - gap;
	if (statusBudget >= style.findStatusMinWidth / 2) {
		const int statusWidth = std::min(style.findStatusMinWidth +
			style.findStatusPadX, std::max(0, statusBudget));
		layout.status = takeRight(statusWidth);
	}

	const int fieldLeft = pad;
	const int fieldRight = std::max(fieldLeft, right + gap);
	if (fieldRight > fieldLeft) {
		layout.field = PRectangle::FromInts(fieldLeft, innerTop, fieldRight,
			innerBottom);
		const int textLeft = fieldLeft + style.findFieldPadX;
		const int textRight = fieldRight - style.findFieldPadX;
		if (textRight > textLeft) {
			layout.fieldText = PRectangle::FromInts(textLeft, innerTop,
				textRight, innerBottom);
		}
	}

	return layout;
}

FindBarHitResult HitTestFindBar(const FindBarLayout &layout,
	Point point) noexcept {
	FindBarHitResult result;
	if (!NonEmptyContains(layout.band, point)) {
		return result;
	}
	if (NonEmptyContains(layout.closeButton, point)) {
		result.kind = FindBarHit::Close;
		return result;
	}
	if (NonEmptyContains(layout.nextButton, point)) {
		result.kind = FindBarHit::Next;
		return result;
	}
	if (NonEmptyContains(layout.previousButton, point)) {
		result.kind = FindBarHit::Previous;
		return result;
	}
	if (NonEmptyContains(layout.field, point)) {
		result.kind = FindBarHit::Field;
		return result;
	}
	result.kind = FindBarHit::Band;
	return result;
}

std::string_view FindBarStatusLabel(FindBarStatus status) noexcept {
	switch (status) {
	case FindBarStatus::NoMatches:
		return "No matches";
	case FindBarStatus::Wrapped:
		return "Wrapped";
	case FindBarStatus::None:
		return {};
	}
	return {};
}

FindBarPointerResult HandleFindBarPointer(FindBarModel &model,
	const FindBarLayout &layout, const PointerInput &input) noexcept {
	FindBarPointerResult result;
	const Point point(input.x, input.y);

	if (input.action == PointerAction::Leave) {
		if (model.pressOrigin) {
			model.pressOrigin.reset();
			result.dirty = true;
		}
		// Surface leave still needs to clear editor hover and capture.
		result.consumed = false;
		return result;
	}

	const FindBarHitResult hit = HitTestFindBar(layout, point);
	const bool overBand = hit.kind != FindBarHit::None;

	if (input.action == PointerAction::Move) {
		result.consumed = overBand || model.pressOrigin.has_value();
		return result;
	}

	if (input.action == PointerAction::Press && input.button == 0) {
		if (hit.kind == FindBarHit::Field) {
			model.SetFocused(true);
			result.fieldFocused = true;
			result.dirty = true;
			result.consumed = true;
			model.pressOrigin.reset();
			return result;
		}
		if (const auto press = PressKindFromHit(hit.kind)) {
			model.pressOrigin = press;
			result.dirty = true;
			result.consumed = true;
			return result;
		}
		if (overBand) {
			model.pressOrigin.reset();
			result.consumed = true;
			return result;
		}
		return result;
	}

	if (input.action == PointerAction::Release && input.button == 0) {
		const std::optional<FindBarPressKind> origin = model.pressOrigin;
		model.pressOrigin.reset();
		if (origin) {
			result.dirty = true;
			if (hit.kind == HitFromPressKind(*origin)) {
				result.requests.push_back(
					MakeSearchRequest(RequestFromPressKind(*origin)));
			}
			result.consumed = true;
			return result;
		}
		result.consumed = overBand;
		return result;
	}

	// Other buttons: swallow only over the band.
	result.consumed = overBand;
	return result;
}

FindBarKeyboardResult HandleFindBarKeyboard(FindBarModel &model,
	const KeyboardInput &input) {
	FindBarKeyboardResult result;
	if (!model.focused) {
		return result;
	}
	// Consume all focused-field keyboard traffic, including releases, so the
	// editor does not also act on the same key.
	result.consumed = true;
	if (!input.pressed) {
		return result;
	}

	if (model.preedit &&
		(input.key != static_cast<Scintilla::Keys>(0) || !input.text.empty())) {
		model.CancelPreedit();
		result.dirty = true;
	}

	const bool ctrlOnly = input.modifiers == Scintilla::KeyMod::Ctrl;
	const bool shiftOnly = input.modifiers == Scintilla::KeyMod::Shift;
	const bool noMod = input.modifiers == Scintilla::KeyMod::Norm;

	if ((noMod || shiftOnly) && input.key == Scintilla::Keys::Return) {
		result.requests.push_back(MakeSearchRequest(shiftOnly ?
			FindBarRequestKind::SearchBackward :
			FindBarRequestKind::SearchForward));
		return result;
	}
	if (noMod && input.key == Scintilla::Keys::Escape) {
		result.requests.push_back(MakeCloseRequest());
		return result;
	}

	if (ctrlOnly && input.key == static_cast<Scintilla::Keys>('A')) {
		model.SelectAll();
		MarkTextInputDirty(model, ApplicationTextChangeCause::Other);
		result.dirty = true;
		return result;
	}
	if (ctrlOnly && input.key == static_cast<Scintilla::Keys>('C')) {
		if (model.HasSelection()) {
			result.requests.push_back(
				MakeClipboardCopy(model, model.SelectedText()));
		}
		return result;
	}
	if (ctrlOnly && input.key == static_cast<Scintilla::Keys>('X')) {
		if (model.HasSelection()) {
			std::string text = model.SelectedText();
			model.DeleteSelection();
			result.queryChanged = true;
			result.dirty = true;
			result.requests.push_back(MakeClipboardCopy(model, std::move(text)));
		}
		return result;
	}
	if (ctrlOnly && input.key == static_cast<Scintilla::Keys>('V')) {
		if (model.pasteAvailable) {
			result.requests.push_back(MakeClipboardPaste(model));
		}
		return result;
	}

	if (noMod && input.key == Scintilla::Keys::Left) {
		model.ClampCaretAndAnchor();
		if (model.HasSelection()) {
			model.caret = model.SelectionStart();
			model.anchor = model.caret;
		} else {
			model.caret = Utf8Prev(model.query, model.caret);
			model.anchor = model.caret;
		}
		MarkTextInputDirty(model, ApplicationTextChangeCause::Other);
		result.dirty = true;
		return result;
	}
	if (noMod && input.key == Scintilla::Keys::Right) {
		model.ClampCaretAndAnchor();
		if (model.HasSelection()) {
			model.caret = model.SelectionEnd();
			model.anchor = model.caret;
		} else {
			model.caret = Utf8Next(model.query, model.caret);
			model.anchor = model.caret;
		}
		MarkTextInputDirty(model, ApplicationTextChangeCause::Other);
		result.dirty = true;
		return result;
	}
	if (noMod && input.key == Scintilla::Keys::Home) {
		model.caret = 0;
		model.anchor = 0;
		MarkTextInputDirty(model, ApplicationTextChangeCause::Other);
		result.dirty = true;
		return result;
	}
	if (noMod && input.key == Scintilla::Keys::End) {
		model.caret = model.query.size();
		model.anchor = model.caret;
		MarkTextInputDirty(model, ApplicationTextChangeCause::Other);
		result.dirty = true;
		return result;
	}
	if (noMod && input.key == Scintilla::Keys::Back) {
		model.ClampCaretAndAnchor();
		if (model.HasSelection()) {
			model.DeleteSelection();
		} else if (model.caret > 0) {
			const std::size_t start = Utf8Prev(model.query, model.caret);
			model.query.erase(start, model.caret - start);
			model.caret = start;
			model.anchor = start;
			MarkQueryChanged(model);
		} else {
			return result;
		}
		result.queryChanged = true;
		result.dirty = true;
		return result;
	}
	if (noMod && input.key == Scintilla::Keys::Delete) {
		model.ClampCaretAndAnchor();
		if (model.HasSelection()) {
			model.DeleteSelection();
		} else if (model.caret < model.query.size()) {
			const std::size_t end = Utf8Next(model.query, model.caret);
			model.query.erase(model.caret, end - model.caret);
			model.anchor = model.caret;
			MarkQueryChanged(model);
		} else {
			return result;
		}
		result.queryChanged = true;
		result.dirty = true;
		return result;
	}

	// Direct UTF-8 text from the keyboard path (no command modifiers).
	const Scintilla::KeyMod commandModifiers = Scintilla::KeyMod::Ctrl |
		Scintilla::KeyMod::Alt | Scintilla::KeyMod::Super |
		Scintilla::KeyMod::Meta;
	if (!input.text.empty() &&
		(input.modifiers & commandModifiers) == Scintilla::KeyMod::Norm) {
		if (model.InsertText(input.text)) {
			result.queryChanged = true;
			result.dirty = true;
		}
		return result;
	}

	return result;
}

FindBarTextInputResult HandleFindBarTextInputBatch(FindBarModel &model,
	const ApplicationTextInputBatch &batch) {
	FindBarTextInputResult result;
	if (batch.cancel) {
		if (model.preedit) {
			model.CancelPreedit();
			result.dirty = true;
		}
		if (batch.refreshState) {
			model.textInputStateDirty = true;
			result.dirty = true;
		}
		return result;
	}
	if (!model.focused) {
		if (batch.refreshState) {
			model.textInputStateDirty = true;
			result.dirty = true;
		}
		return result;
	}

	const bool replacingPreedit = model.preedit.has_value();
	if (replacingPreedit) {
		model.preedit.reset();
		result.dirty = true;
	}
	bool changed = replacingPreedit;

	if (batch.deletion) {
		if (!DeleteSurrounding(model, *batch.deletion)) {
			MarkTextInputDirty(model, ApplicationTextChangeCause::Other);
			result.dirty = true;
			return result;
		}
		if (batch.deletion->beforeLength != 0 ||
			batch.deletion->afterLength != 0) {
			MarkQueryChanged(model);
			result.queryChanged = true;
			changed = true;
		}
	}
	if (batch.commit && !batch.commit->empty()) {
		if (!model.InsertText(*batch.commit)) {
			MarkTextInputDirty(model, ApplicationTextChangeCause::Other);
			result.dirty = true;
			return result;
		}
		// InsertText already cleared status and set dirty cause to Other;
		// IME commits report InputMethod.
		model.textInputChangeCause = ApplicationTextChangeCause::InputMethod;
		result.queryChanged = true;
		changed = true;
	}
	if (batch.preedit && !batch.preedit->text.empty()) {
		if (!UTF8IsValid(batch.preedit->text)) {
			MarkTextInputDirty(model, ApplicationTextChangeCause::Other);
			result.dirty = true;
			return result;
		}
		model.preedit = *batch.preedit;
		MarkTextInputDirty(model, ApplicationTextChangeCause::InputMethod);
		changed = true;
	}

	if (batch.refreshState || changed) {
		model.textInputStateDirty = true;
		if (changed && model.textInputChangeCause !=
			ApplicationTextChangeCause::InputMethod) {
			model.textInputChangeCause = ApplicationTextChangeCause::InputMethod;
		}
		result.dirty = true;
	}
	return result;
}

ApplicationTextInputState BuildFindBarTextInputState(const FindBarModel &model,
	const FindBarLayout &layout, Surface &surface, const Font *font) {
	ApplicationTextInputState state;
	// Surrounding text is the committed query; preedit is reported separately
	// by the platform path and excluded here (matches ApplicationEditor).
	constexpr std::size_t MaximumSurroundingBytes = 4000;
	const std::size_t selectionStart = model.SelectionStart();
	const std::size_t selectionEnd = model.SelectionEnd();
	const std::size_t selectionLength = selectionEnd - selectionStart;
	if (selectionLength <= MaximumSurroundingBytes) {
		const std::size_t spare = MaximumSurroundingBytes - selectionLength;
		std::size_t start = selectionStart > spare / 2 ?
			selectionStart - spare / 2 : 0;
		std::size_t end = std::min(model.query.size(),
			start + MaximumSurroundingBytes);
		if (end < selectionEnd) {
			end = selectionEnd;
			start = end > MaximumSurroundingBytes ?
				end - MaximumSurroundingBytes : 0;
		}
		start = Utf8Floor(model.query, start);
		if (end < model.query.size()) {
			end = Utf8Floor(model.query, end);
		}
		if (start <= selectionStart && end >= selectionEnd) {
			state.surroundingText = model.query.substr(start, end - start);
			state.cursor = static_cast<int32_t>(model.caret - start);
			state.anchor = static_cast<int32_t>(model.anchor - start);
		}
	}

	const auto coordinate = [](XYPOSITION value) {
		const double minimum =
			static_cast<double>(std::numeric_limits<int32_t>::min());
		const double maximum =
			static_cast<double>(std::numeric_limits<int32_t>::max());
		return static_cast<int32_t>(std::clamp<double>(value, minimum, maximum));
	};

	if (NonEmpty(layout.fieldText)) {
		const XYPOSITION scroll = FieldScrollX(surface, font, model, layout);
		const std::string display = DisplayText(model);
		const std::size_t caret = DisplayCaret(model);
		const XYPOSITION caretX = TextWidth(surface, font,
			std::string_view(display).substr(0, caret));
		const int height = std::max(1,
			static_cast<int>(layout.fieldText.Height()));
		state.cursorRectangle = {
			coordinate(layout.fieldText.left + caretX - scroll),
			coordinate(layout.fieldText.top),
			1,
			height,
		};
	} else if (NonEmpty(layout.field)) {
		state.cursorRectangle = {
			coordinate(layout.field.left),
			coordinate(layout.field.top),
			1,
			std::max(1, static_cast<int>(layout.field.Height())),
		};
	}
	return state;
}

bool ApplyFindBarPaste(FindBarModel &model, std::string_view text) {
	if (!model.focused || text.empty()) {
		return false;
	}
	return model.InsertText(text);
}

FindBarPainter::FindBarPainter(const UiStyle &styleIn) : style(styleIn) {
	labelFont = Font::Allocate(FontParameters{
		style.fontName, UiPixelSizeFromPoints(style.chromeLabelPoints)});
}

void FindBarPainter::Paint(Surface &surface, const FindBarLayout &layout,
	const FindBarModel &model) const {
	if (!NonEmpty(layout.band)) {
		return;
	}
	surface.FillRectangle(layout.band, Fill(style.findBarFill));
	DrawInsideFrame(surface, layout.band, style.chromeBorder, 1.0);

	if (NonEmpty(layout.field)) {
		surface.FillRectangle(layout.field, Fill(style.findFieldFill));
		const ColourRGBA border = model.focused ? style.focusBorder :
			style.findFieldBorder;
		const XYPOSITION borderWidth = model.focused ? 2.0 : 1.0;
		DrawInsideFrame(surface, layout.field, border, borderWidth);
	}

	if (NonEmpty(layout.fieldText) && labelFont) {
		const std::string display = DisplayText(model);
		const XYPOSITION scroll = FieldScrollX(surface, labelFont.get(), model,
			layout);
		const std::size_t selStart = model.preedit && !model.preedit->text.empty()
			? std::min(DisplayCaret(model), DisplayAnchor(model))
			: model.SelectionStart();
		const std::size_t selEnd = model.preedit && !model.preedit->text.empty()
			? std::max(DisplayCaret(model), DisplayAnchor(model))
			: model.SelectionEnd();

		// Selection highlight behind the text.
		if (model.focused && selStart != selEnd) {
			const XYPOSITION x0 = TextWidth(surface, labelFont.get(),
				std::string_view(display).substr(0, selStart)) - scroll;
			const XYPOSITION x1 = TextWidth(surface, labelFont.get(),
				std::string_view(display).substr(0, selEnd)) - scroll;
			const XYPOSITION left = layout.fieldText.left + std::min(x0, x1);
			const XYPOSITION right = layout.fieldText.left + std::max(x0, x1);
			const PRectangle selRc(
				std::max(layout.fieldText.left, left),
				layout.fieldText.top,
				std::min(layout.fieldText.right, right),
				layout.fieldText.bottom);
			if (NonEmpty(selRc)) {
				surface.FillRectangle(selRc, Fill(style.findSelectionFill));
			}
		}

		if (!display.empty()) {
			const XYPOSITION ascent = surface.Ascent(labelFont.get());
			const XYPOSITION height = surface.Height(labelFont.get());
			const XYPOSITION ybase = static_cast<XYPOSITION>(static_cast<int>(
				layout.fieldText.top +
				(layout.fieldText.Height() - height) / 2.0 + ascent));
			const XYPOSITION textX = layout.fieldText.left - scroll;
			const PRectangle textRc(textX, layout.fieldText.top,
				textX + TextWidth(surface, labelFont.get(), display) + 1.0,
				layout.fieldText.bottom);
			// Clip to the field text band so scrolled content does not paint
			// under the buttons.
			surface.SetClip(layout.fieldText);
			surface.DrawTextTransparent(textRc, labelFont.get(), ybase, display,
				style.text);
			surface.PopClip();
		}

		// Caret when focused and not hiding for negative preedit cursor.
		const bool hideCaret = model.preedit && model.preedit->cursorBegin < 0;
		if (model.focused && !hideCaret) {
			const std::size_t caret = DisplayCaret(model);
			const XYPOSITION caretX = TextWidth(surface, labelFont.get(),
				std::string_view(display).substr(0, caret)) - scroll;
			const XYPOSITION x = layout.fieldText.left + caretX;
			if (x >= layout.fieldText.left && x < layout.fieldText.right) {
				surface.FillRectangle(
					PRectangle(x, layout.fieldText.top + 2.0, x + 1.0,
						layout.fieldText.bottom - 2.0),
					Fill(style.findCaretInk));
			}
		}
	}

	const std::string_view status = FindBarStatusLabel(model.status);
	if (NonEmpty(layout.status) && !status.empty()) {
		DrawRightAlignedLabel(surface, layout.status, labelFont.get(), status,
			style.mutedText);
	}

	const bool prevPressed = model.pressOrigin == FindBarPressKind::Previous;
	const bool nextPressed = model.pressOrigin == FindBarPressKind::Next;
	const bool closePressed = model.pressOrigin == FindBarPressKind::Close;
	DrawButton(surface, layout.previousButton, labelFont.get(), "Prev",
		prevPressed, false, style);
	DrawButton(surface, layout.nextButton, labelFont.get(), "Next",
		nextPressed, false, style);
	DrawButton(surface, layout.closeButton, labelFont.get(), {},
		closePressed, true, style);
}

}
