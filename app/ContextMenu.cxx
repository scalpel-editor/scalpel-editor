#include "ContextMenu.h"

#include "UiStyle.h"

#include <algorithm>

#include "ApplicationEditor.h"

namespace Scalpel {

namespace {

using Scintilla::Internal::ColourRGBA;
using Scintilla::Internal::Fill;
using Scintilla::Internal::Font;
using Scintilla::Internal::FontParameters;
using Scintilla::Internal::PRectangle;
using Scintilla::Internal::Point;
using Scintilla::Internal::Surface;
using Scintilla::Internal::XYPOSITION;

/** Fixed context-menu order; labels and separators come from ApplicationAction. */
constexpr ApplicationAction kContextActions[] = {
	ApplicationAction::Undo,
	ApplicationAction::Redo,
	ApplicationAction::Cut,
	ApplicationAction::Copy,
	ApplicationAction::Paste,
	ApplicationAction::SelectAll,
};

constexpr std::size_t kContextActionCount =
	sizeof(kContextActions) / sizeof(kContextActions[0]);

bool NonEmpty(const PRectangle &rc) noexcept {
	return rc.right > rc.left && rc.bottom > rc.top;
}

bool NonEmptyContains(const PRectangle &rc, Point point) noexcept {
	// Half-open bounds keep adjacent hit regions unambiguous.
	return NonEmpty(rc) &&
		point.x >= rc.left && point.x < rc.right &&
		point.y >= rc.top && point.y < rc.bottom;
}

int PreferredWidth(const UiStyle &style) noexcept {
	return style.menuEditDropdownPreferredWidth;
}

int PreferredHeight(const UiStyle &style) noexcept {
	int height = style.menuDropdownPadY * 2;
	for (std::size_t i = 0; i < kContextActionCount; ++i) {
		if (InfoFor(kContextActions[i]).separatorBefore) {
			height += style.menuSeparatorHeight;
		}
		height += style.menuItemHeight;
	}
	return height;
}

std::optional<ApplicationAction> FirstEnabledItem(
	const ContextMenuModel &model) noexcept {
	for (ApplicationAction action : kContextActions) {
		if (model.IsEnabled(action)) {
			return action;
		}
	}
	return std::nullopt;
}

std::optional<ApplicationAction> LastEnabledItem(
	const ContextMenuModel &model) noexcept {
	for (std::size_t i = kContextActionCount; i > 0; --i) {
		const ApplicationAction action = kContextActions[i - 1];
		if (model.IsEnabled(action)) {
			return action;
		}
	}
	return std::nullopt;
}

std::optional<ApplicationAction> AdjacentEnabledItem(
	const ContextMenuModel &model,
	std::optional<ApplicationAction> focused, bool forward) noexcept {
	std::optional<std::size_t> focusedIndex;
	for (std::size_t i = 0; i < kContextActionCount; ++i) {
		if (focused.has_value() && kContextActions[i] == *focused &&
			model.IsEnabled(kContextActions[i])) {
			focusedIndex = i;
			break;
		}
	}

	if (!focusedIndex.has_value()) {
		return forward ? FirstEnabledItem(model) : LastEnabledItem(model);
	}

	for (std::size_t step = 1; step <= kContextActionCount; ++step) {
		const std::size_t index = forward ?
			(*focusedIndex + step) % kContextActionCount :
			(*focusedIndex + kContextActionCount - step) % kContextActionCount;
		if (model.IsEnabled(kContextActions[index])) {
			return kContextActions[index];
		}
	}
	return std::nullopt;
}

bool SetOptional(std::optional<ApplicationAction> &slot,
	std::optional<ApplicationAction> next) noexcept {
	if (slot == next) {
		return false;
	}
	slot = next;
	return true;
}

bool ItemEnabled(const ContextMenuLayout &layout,
	ApplicationAction wanted) noexcept {
	for (const ContextMenuItemLayout &item : layout.items) {
		if (item.action == wanted) {
			return item.enabled;
		}
	}
	return false;
}

}

bool ContextMenuModel::IsEnabled(ApplicationAction action) const noexcept {
	switch (action) {
	case ApplicationAction::Undo:
		return undoEnabled;
	case ApplicationAction::Redo:
		return redoEnabled;
	case ApplicationAction::Cut:
		return cutEnabled;
	case ApplicationAction::Copy:
		return copyEnabled;
	case ApplicationAction::Paste:
		return pasteEnabled;
	case ApplicationAction::SelectAll:
		return selectAllEnabled;
	default:
		return false;
	}
}

bool UpdateContextMenuActionState(ContextMenuModel &model,
	ApplicationEditor &editor) {
	const bool undo = ApplicationActionEnabled(ApplicationAction::Undo, editor);
	const bool redo = ApplicationActionEnabled(ApplicationAction::Redo, editor);
	const bool cut = ApplicationActionEnabled(ApplicationAction::Cut, editor);
	const bool copy = ApplicationActionEnabled(ApplicationAction::Copy, editor);
	const bool paste = ApplicationActionEnabled(ApplicationAction::Paste, editor);
	const bool selectAll =
		ApplicationActionEnabled(ApplicationAction::SelectAll, editor);
	const bool changed = model.undoEnabled != undo ||
		model.redoEnabled != redo ||
		model.cutEnabled != cut ||
		model.copyEnabled != copy ||
		model.pasteEnabled != paste ||
		model.selectAllEnabled != selectAll;
	model.undoEnabled = undo;
	model.redoEnabled = redo;
	model.cutEnabled = cut;
	model.copyEnabled = copy;
	model.pasteEnabled = paste;
	model.selectAllEnabled = selectAll;
	if (model.open && model.focusedItem.has_value() &&
		!model.IsEnabled(*model.focusedItem)) {
		model.focusedItem = FirstEnabledItem(model);
	}
	return changed;
}

int ContextMenuPreferredWidth() noexcept {
	return PreferredWidth(DefaultUiStyle());
}

int ContextMenuPreferredHeight() noexcept {
	return PreferredHeight(DefaultUiStyle());
}

ContextMenuLayout LayoutContextMenu(const ContextMenuModel &model,
	int maxWidth, int maxHeight) noexcept {
	const UiStyle &style = DefaultUiStyle();
	const PRectangle empty = PRectangle::FromInts(0, 0, 0, 0);
	ContextMenuLayout layout{empty, PreferredWidth(style),
		PreferredHeight(style), {}};

	if (!model.open) {
		return layout;
	}

	int width = layout.requestedWidth;
	if (maxWidth > 0) {
		width = std::min(width, maxWidth);
	}
	if (width <= 0) {
		return layout;
	}

	int heightLimit = layout.requestedHeight;
	if (maxHeight > 0) {
		heightLimit = std::min(heightLimit, maxHeight);
	}
	if (heightLimit <= 0) {
		return layout;
	}

	const int shortcutWidth = std::min(style.menuShortcutColumnWidth,
		std::max(0, width - style.menuLabelPadLeft - style.menuShortcutPadRight -
			style.menuLabelShortcutGap / 2));

	int y = style.menuDropdownPadY;
	const int innerLeft = 0;
	const int innerRight = width;

	for (ApplicationAction action : kContextActions) {
		const ApplicationActionInfo &info = InfoFor(action);
		const bool separatorBefore = info.separatorBefore;
		PRectangle separator = empty;
		if (separatorBefore) {
			const int sepBottom = y + style.menuSeparatorHeight;
			if (sepBottom > heightLimit) {
				break;
			}
			const int ruleY = y + style.menuSeparatorHeight / 2;
			separator = PRectangle::FromInts(
				innerLeft + style.menuLabelPadLeft, ruleY,
				innerRight - style.menuShortcutPadRight, ruleY + 1);
			y = sepBottom;
		}

		const int rowBottom = y + style.menuItemHeight;
		if (rowBottom > heightLimit) {
			break;
		}
		const PRectangle row = PRectangle::FromInts(
			innerLeft, y, innerRight, rowBottom);
		const std::string labelText(info.label);
		const std::string shortcutText(info.shortcutLabel);
		const int rowShortcutWidth = shortcutText.empty() ? 0 : shortcutWidth;
		const int labelLeft = innerLeft + style.menuLabelPadLeft;
		const int labelRight = std::max(
			labelLeft,
			innerRight - style.menuShortcutPadRight - rowShortcutWidth -
				(rowShortcutWidth > 0 ? style.menuLabelShortcutGap : 0));
		const PRectangle label = PRectangle::FromInts(
			labelLeft, y, labelRight, rowBottom);
		const PRectangle shortcut = rowShortcutWidth > 0 ?
			PRectangle::FromInts(
				innerRight - style.menuShortcutPadRight - rowShortcutWidth, y,
				innerRight - style.menuShortcutPadRight, rowBottom) :
			empty;

		layout.items.push_back(ContextMenuItemLayout{
			action,
			separatorBefore,
			model.IsEnabled(action),
			row,
			separator,
			label,
			shortcut,
			labelText,
			shortcutText,
		});
		y = rowBottom;
	}

	const int contentBottom = y + style.menuDropdownPadY;
	const int panelBottom = std::min(contentBottom, heightLimit);
	layout.panel = PRectangle::FromInts(0, 0, width, panelBottom);
	return layout;
}

ContextMenuHitResult HitTestContextMenu(const ContextMenuLayout &layout,
	Point point) noexcept {
	if (!NonEmptyContains(layout.panel, point)) {
		return {};
	}
	for (const ContextMenuItemLayout &item : layout.items) {
		if (NonEmptyContains(item.row, point)) {
			return {ContextMenuHit::Item, item.action};
		}
	}
	return {ContextMenuHit::Panel, ApplicationAction::Undo};
}

void OpenContextMenu(ContextMenuModel &model) noexcept {
	model.open = true;
	model.hoveredItem.reset();
	model.pressOrigin.reset();
	model.focusedItem = FirstEnabledItem(model);
}

void CloseContextMenu(ContextMenuModel &model) noexcept {
	model.open = false;
	model.hoveredItem.reset();
	model.focusedItem.reset();
	model.pressOrigin.reset();
}

ContextMenuPointerResult HandleContextMenuPointer(ContextMenuModel &model,
	const ContextMenuLayout &layout, const PointerInput &input) noexcept {
	ContextMenuPointerResult result;
	// Closed menus ignore input unless a dismissal press is awaiting release.
	if (!model.open && !model.pressOrigin.has_value()) {
		return result;
	}

	const Point point(input.x, input.y);

	if (input.action == PointerAction::Leave) {
		result.dirty = SetOptional(model.hoveredItem, std::nullopt);
		model.pressOrigin.reset();
		// Surface leave still needs host delivery for toplevel leave handling.
		result.consumed = false;
		return result;
	}

	// After an outside dismissal, only the matching release is handled.
	if (!model.open) {
		if (input.action == PointerAction::Release && input.button == 0 &&
			model.pressOrigin.has_value() &&
			model.pressOrigin->kind == ContextMenuPressKind::Dismissal) {
			model.pressOrigin.reset();
			result.consumed = true;
		}
		return result;
	}

	const ContextMenuHitResult hit = HitTestContextMenu(layout, point);

	if (input.action == PointerAction::Move) {
		if (hit.kind == ContextMenuHit::Item) {
			result.dirty = SetOptional(model.hoveredItem, hit.action);
		} else {
			result.dirty = SetOptional(model.hoveredItem, std::nullopt);
		}
		// Consume moves over the panel so the editor does not show text hover.
		result.consumed = hit.kind != ContextMenuHit::None;
		return result;
	}

	if (input.action == PointerAction::Scroll) {
		result.consumed = hit.kind != ContextMenuHit::None;
		return result;
	}

	if (input.action == PointerAction::Press && input.button == 0) {
		if (hit.kind == ContextMenuHit::Item) {
			model.pressOrigin = ContextMenuPressOrigin{
				ContextMenuPressKind::Item, hit.action};
			result.dirty = SetOptional(model.hoveredItem, hit.action);
			result.consumed = true;
			return result;
		}

		// Press on padding or outside: close and consume so the click cannot
		// move the caret or activate chrome under the popup. Keep a dismissal
		// press origin so the matching release is also consumed.
		model.hoveredItem.reset();
		model.focusedItem.reset();
		model.open = false;
		model.pressOrigin = ContextMenuPressOrigin{
			ContextMenuPressKind::Dismissal, ApplicationAction::Undo};
		result.dirty = true;
		result.consumed = true;
		return result;
	}

	if (input.action == PointerAction::Release && input.button == 0) {
		if (!model.pressOrigin.has_value()) {
			result.consumed = hit.kind != ContextMenuHit::None;
			return result;
		}

		const ContextMenuPressOrigin origin = *model.pressOrigin;
		model.pressOrigin.reset();

		if (origin.kind == ContextMenuPressKind::Dismissal) {
			result.consumed = true;
			return result;
		}

		if (origin.kind == ContextMenuPressKind::Item &&
			hit.kind == ContextMenuHit::Item &&
			hit.action == origin.action &&
			ItemEnabled(layout, hit.action)) {
			CloseContextMenu(model);
			result.activated = hit.action;
			result.dirty = true;
			result.consumed = true;
			return result;
		}

		// Mismatched release or disabled item: no activation; stay open.
		if (origin.kind == ContextMenuPressKind::Item &&
			hit.kind == ContextMenuHit::Item &&
			hit.action == origin.action &&
			!ItemEnabled(layout, hit.action)) {
			result.consumed = true;
			return result;
		}

		result.consumed = hit.kind != ContextMenuHit::None || model.open;
		return result;
	}

	// Other buttons: swallow over the panel only.
	result.consumed = hit.kind != ContextMenuHit::None;
	return result;
}

ContextMenuKeyboardResult HandleContextMenuKeyboard(ContextMenuModel &model,
	const KeyboardInput &input) noexcept {
	ContextMenuKeyboardResult result;
	if (!model.open) {
		return result;
	}

	// Open: the menu owns every key so shortcuts and typing cannot leak.
	result.consumed = true;

	if (!input.pressed) {
		return result;
	}

	if (input.key == Scintilla::Keys::Escape) {
		CloseContextMenu(model);
		result.dirty = true;
		return result;
	}

	if (input.key == Scintilla::Keys::Down ||
		input.key == Scintilla::Keys::Up) {
		if (SetOptional(model.hoveredItem, std::nullopt)) {
			result.dirty = true;
		}
		model.pressOrigin.reset();
		const std::optional<ApplicationAction> next = AdjacentEnabledItem(
			model, model.focusedItem,
			input.key == Scintilla::Keys::Down);
		if (next.has_value() && SetOptional(model.focusedItem, next)) {
			result.dirty = true;
		}
		return result;
	}

	if (input.key == Scintilla::Keys::Home) {
		if (SetOptional(model.hoveredItem, std::nullopt)) {
			result.dirty = true;
		}
		model.pressOrigin.reset();
		if (SetOptional(model.focusedItem, FirstEnabledItem(model))) {
			result.dirty = true;
		}
		return result;
	}

	if (input.key == Scintilla::Keys::End) {
		if (SetOptional(model.hoveredItem, std::nullopt)) {
			result.dirty = true;
		}
		model.pressOrigin.reset();
		if (SetOptional(model.focusedItem, LastEnabledItem(model))) {
			result.dirty = true;
		}
		return result;
	}

	if (input.key == Scintilla::Keys::Return) {
		if (model.focusedItem.has_value() &&
			model.IsEnabled(*model.focusedItem)) {
			result.activated = *model.focusedItem;
			CloseContextMenu(model);
			result.dirty = true;
		}
		// Disabled focus or empty focus: swallow Enter without activating.
		return result;
	}

	// Unrelated keys while open: consumed, no state change.
	return result;
}

ContextMenuPainter::ContextMenuPainter(const UiStyle &styleIn)
	: style(styleIn) {
	labelFont = Font::Allocate(FontParameters{
		style.fontName, UiPixelSizeFromPoints(style.chromeLabelPoints)});
}

void ContextMenuPainter::Paint(Surface &surface, const ContextMenuLayout &layout,
	const ContextMenuModel &model) const {
	if (!NonEmpty(layout.panel)) {
		return;
	}

	surface.FillRectangle(layout.panel, Fill(style.panelFill));

	const Font *font = labelFont.get();
	for (const ContextMenuItemLayout &item : layout.items) {
		if (item.separatorBefore && NonEmpty(item.separator)) {
			surface.FillRectangle(item.separator, Fill(style.menuSeparator));
		}
		if (!NonEmpty(item.row)) {
			continue;
		}

		const bool hovered = model.hoveredItem.has_value() &&
			*model.hoveredItem == item.action;
		const bool focused = model.focusedItem.has_value() &&
			*model.focusedItem == item.action;
		if (item.enabled && (hovered || focused)) {
			surface.FillRectangle(item.row,
				Fill(focused ? style.focusFill : style.menuItemHover));
		}

		const ColourRGBA ink = item.enabled ? style.text : style.disabledText;
		if (!font) {
			continue;
		}
		const ColourRGBA shortcutInk =
			item.enabled ? style.mutedText : style.disabledText;
		const std::string labelText = TruncateLabel(
			surface, font, item.labelText, item.label.Width());
		surface.SetClip(item.label);
		DrawLeftAlignedLabel(surface, item.label, font, labelText, ink);
		surface.PopClip();
		if (NonEmpty(item.shortcut) && !item.shortcutText.empty()) {
			DrawRightAlignedLabel(surface, item.shortcut, font, item.shortcutText,
				shortcutInk);
		}
	}

	// Draw last so row hover and focus fills cannot cover the panel edge.
	DrawInsideFrame(surface, layout.panel, style.cardButtonBorder, 1.0);
}

}
