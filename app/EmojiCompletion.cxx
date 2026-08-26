#include "EmojiCompletion.h"

#include <algorithm>
#include <string>

#include "UiStyle.h"

namespace Scalpel {

namespace {

using Scintilla::Internal::ColourRGBA;
using Scintilla::Internal::Fill;
using Scintilla::Internal::Font;
using Scintilla::Internal::FontParameters;
using Scintilla::Internal::PRectangle;
using Scintilla::Internal::Point;
using Scintilla::Internal::Surface;

constexpr int kMaxRows = 8;

bool NonEmpty(const PRectangle &rc) noexcept {
	return rc.right > rc.left && rc.bottom > rc.top;
}

bool NonEmptyContains(const PRectangle &rc, Point point) noexcept {
	return NonEmpty(rc) &&
		point.x >= rc.left && point.x < rc.right &&
		point.y >= rc.top && point.y < rc.bottom;
}

std::string ItemLabel(const EmojiMatch &match) {
	std::string label;
	label.reserve(match.emoji.size() + match.alias.size() + 3);
	label.append(match.emoji.data(), match.emoji.size());
	label.append(" :");
	label.append(match.alias.data(), match.alias.size());
	label.push_back(':');
	return label;
}

}

int EmojiCompletionMaxRows() noexcept {
	return kMaxRows;
}

void CloseEmojiCompletion(EmojiCompletionModel &model) noexcept {
	model.open = false;
	model.matches.clear();
	model.selected = 0;
	model.colonPos = 0;
	model.hovered.reset();
	model.pressIndex.reset();
}

EmojiCompletionLayout LayoutEmojiCompletion(const EmojiCompletionModel &model,
	double caretX, double caretY, int lineHeight,
	PRectangle client) noexcept {
	EmojiCompletionLayout layout;
	if (!model.open || model.matches.empty() || !NonEmpty(client) ||
		lineHeight <= 0) {
		return layout;
	}

	const UiStyle &style = DefaultUiStyle();
	const int rows = std::min(static_cast<int>(model.matches.size()), kMaxRows);
	int width = style.menuEditDropdownPreferredWidth;
	const int clientWidth = static_cast<int>(client.Width());
	if (clientWidth > 0) {
		width = std::min(width, clientWidth);
	}
	if (width <= 0) {
		return layout;
	}

	int height = style.menuDropdownPadY * 2 + rows * style.menuItemHeight;
	const int clientHeight = static_cast<int>(client.Height());
	if (clientHeight > 0 && height > clientHeight) {
		const int fitRows = std::max(0,
			(clientHeight - style.menuDropdownPadY * 2) / style.menuItemHeight);
		if (fitRows <= 0) {
			return layout;
		}
		height = style.menuDropdownPadY * 2 + fitRows * style.menuItemHeight;
	}

	int left = static_cast<int>(caretX);
	if (left + width > static_cast<int>(client.right)) {
		left = static_cast<int>(client.right) - width;
	}
	if (left < static_cast<int>(client.left)) {
		left = static_cast<int>(client.left);
	}

	const int belowTop = static_cast<int>(caretY) + lineHeight;
	const int aboveTop = static_cast<int>(caretY) - height;
	const bool above = belowTop + height > static_cast<int>(client.bottom) &&
		aboveTop >= static_cast<int>(client.top);
	int top = above ? aboveTop : belowTop;
	if (top < static_cast<int>(client.top)) {
		top = static_cast<int>(client.top);
	}
	if (top + height > static_cast<int>(client.bottom)) {
		height = static_cast<int>(client.bottom) - top;
	}
	if (width <= 0 || height <= 0) {
		return layout;
	}

	layout.aboveCaret = above;
	layout.panel = PRectangle::FromInts(left, top, left + width, top + height);

	const int visibleRows = std::max(0,
		(height - style.menuDropdownPadY * 2) / style.menuItemHeight);
	int y = top + style.menuDropdownPadY;
	const int count = std::min(visibleRows, rows);
	for (int i = 0; i < count; ++i) {
		const int rowBottom = y + style.menuItemHeight;
		layout.items.push_back(EmojiCompletionItemLayout{
			static_cast<std::size_t>(i),
			PRectangle::FromInts(left, y, left + width, rowBottom),
			ItemLabel(model.matches[static_cast<std::size_t>(i)]),
		});
		y = rowBottom;
	}
	return layout;
}

EmojiCompletionHitResult HitTestEmojiCompletion(
	const EmojiCompletionLayout &layout, Point point) noexcept {
	if (!NonEmptyContains(layout.panel, point)) {
		return {};
	}
	for (const EmojiCompletionItemLayout &item : layout.items) {
		if (NonEmptyContains(item.row, point)) {
			return {EmojiCompletionHit::Item, item.index};
		}
	}
	return {EmojiCompletionHit::Panel, 0};
}

EmojiCompletionKeyboardResult HandleEmojiCompletionKeyboard(
	EmojiCompletionModel &model, const KeyboardInput &input) noexcept {
	EmojiCompletionKeyboardResult result;
	if (!model.open || model.matches.empty()) {
		return result;
	}
	if (!input.pressed) {
		return result;
	}
	if (input.modifiers != Scintilla::KeyMod::Norm) {
		return result;
	}

	if (input.key == Scintilla::Keys::Escape) {
		CloseEmojiCompletion(model);
		result.consumed = true;
		result.dirty = true;
		result.dismissed = true;
		return result;
	}
	if (input.key == Scintilla::Keys::Down) {
		if (model.selected + 1 < model.matches.size()) {
			++model.selected;
			result.dirty = true;
		}
		model.hovered.reset();
		result.consumed = true;
		return result;
	}
	if (input.key == Scintilla::Keys::Up) {
		if (model.selected > 0) {
			--model.selected;
			result.dirty = true;
		}
		model.hovered.reset();
		result.consumed = true;
		return result;
	}
	if (input.key == Scintilla::Keys::Return ||
		input.key == Scintilla::Keys::Tab) {
		if (model.selected < model.matches.size()) {
			result.completed = model.matches[model.selected];
			result.dirty = true;
		}
		result.consumed = true;
		return result;
	}
	return result;
}

EmojiCompletionPointerResult HandleEmojiCompletionPointer(
	EmojiCompletionModel &model, const EmojiCompletionLayout &layout,
	const PointerInput &input) noexcept {
	EmojiCompletionPointerResult result;
	if (!model.open) {
		return result;
	}
	const Point point(input.x, input.y);
	const EmojiCompletionHitResult hit = HitTestEmojiCompletion(layout, point);

	if (input.action == PointerAction::Move) {
		if (hit.kind == EmojiCompletionHit::Item) {
			if (model.hovered != hit.index) {
				model.hovered = hit.index;
				model.selected = hit.index;
				result.dirty = true;
			}
			result.consumed = true;
		} else if (hit.kind == EmojiCompletionHit::Panel) {
			if (model.hovered.has_value()) {
				model.hovered.reset();
				result.dirty = true;
			}
			result.consumed = true;
		} else if (model.hovered.has_value()) {
			model.hovered.reset();
			result.dirty = true;
		}
		return result;
	}

	if (input.action == PointerAction::Press && input.button == 0) {
		if (hit.kind == EmojiCompletionHit::Item) {
			model.pressIndex = hit.index;
			model.hovered = hit.index;
			model.selected = hit.index;
			result.dirty = true;
			result.consumed = true;
			return result;
		}
		if (hit.kind == EmojiCompletionHit::Panel) {
			model.pressIndex.reset();
			result.consumed = true;
			return result;
		}
		CloseEmojiCompletion(model);
		result.dirty = true;
		result.dismissed = true;
		return result;
	}

	if (input.action == PointerAction::Release && input.button == 0) {
		if (!model.pressIndex.has_value()) {
			result.consumed = hit.kind != EmojiCompletionHit::None;
			return result;
		}
		const std::size_t origin = *model.pressIndex;
		model.pressIndex.reset();
		if (hit.kind == EmojiCompletionHit::Item && hit.index == origin &&
			origin < model.matches.size()) {
			result.completed = model.matches[origin];
			result.dirty = true;
			result.consumed = true;
			return result;
		}
		result.consumed = hit.kind != EmojiCompletionHit::None;
		return result;
	}

	result.consumed = hit.kind != EmojiCompletionHit::None;
	return result;
}

EmojiCompletionPainter::EmojiCompletionPainter(const UiStyle &styleIn)
	: style(styleIn) {
	labelFont = Font::Allocate(FontParameters{
		style.fontName, UiPixelSizeFromPoints(style.chromeLabelPoints)});
}

void EmojiCompletionPainter::Paint(Surface &surface,
	const EmojiCompletionLayout &layout,
	const EmojiCompletionModel &model) const {
	if (!NonEmpty(layout.panel)) {
		return;
	}
	surface.FillRectangle(layout.panel, Fill(style.panelFill));
	const Font *font = labelFont.get();
	for (const EmojiCompletionItemLayout &item : layout.items) {
		if (!NonEmpty(item.row)) {
			continue;
		}
		const bool hovered = model.hovered.has_value() &&
			*model.hovered == item.index;
		const bool selected = model.selected == item.index &&
			!model.hovered.has_value();
		if (hovered || selected) {
			surface.FillRectangle(item.row,
				Fill(selected ? style.focusFill : style.menuItemHover));
		}
		if (!font) {
			continue;
		}
		const PRectangle label = PRectangle(
			item.row.left + style.menuLabelPadLeft, item.row.top,
			item.row.right - style.menuShortcutPadRight, item.row.bottom);
		surface.SetClip(label);
		const std::string drawn = TruncateLabel(
			surface, font, item.label, label.Width());
		DrawLeftAlignedLabel(surface, label, font, drawn, style.text);
		surface.PopClip();
	}
	DrawInsideFrame(surface, layout.panel, style.cardButtonBorder, 1.0);
}

}
