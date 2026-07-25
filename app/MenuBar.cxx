#include "MenuBar.h"

#include <algorithm>

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

constexpr int kBarHeight = 24;
constexpr int kHeadingPadX = 12;
constexpr int kFileHeadingWidth = 48;
constexpr int kEditHeadingWidth = 48;
constexpr int kItemHeight = 24;
constexpr int kSeparatorHeight = 9;
constexpr int kDropdownPadY = 4;
constexpr int kLabelPadLeft = 12;
constexpr int kShortcutPadRight = 12;
constexpr int kLabelShortcutGap = 24;
constexpr int kShortcutColumnWidth = 100;
// Fits "Save As…" + gap + "Ctrl+Shift+S" at the menu font with padding.
constexpr int kDropdownPreferredWidth = 220;

// FontParameters.size is device pixels (see FontPlatform FC_PIXEL_SIZE).
constexpr float PixelSizeFromPoints(float points) noexcept {
	return points * 96.0f / 72.0f;
}

bool NonEmpty(const PRectangle &rc) noexcept {
	return rc.right > rc.left && rc.bottom > rc.top;
}

bool NonEmptyContains(const PRectangle &rc, Point point) noexcept {
	// PRectangle::Contains includes right and bottom; keep adjacent hit
	// regions unambiguous with half-open bounds.
	return NonEmpty(rc) &&
		point.x >= rc.left && point.x < rc.right &&
		point.y >= rc.top && point.y < rc.bottom;
}

std::string_view HeadingLabel(ApplicationMenu menu) noexcept {
	switch (menu) {
	case ApplicationMenu::File:
		return "File";
	case ApplicationMenu::Edit:
		return "Edit";
	}
	return "File";
}

int HeadingWidth(ApplicationMenu menu) noexcept {
	switch (menu) {
	case ApplicationMenu::File:
		return kFileHeadingWidth;
	case ApplicationMenu::Edit:
		return kEditHeadingWidth;
	}
	return kFileHeadingWidth;
}

int DropdownContentHeight(ApplicationMenu menu) noexcept {
	int height = kDropdownPadY * 2;
	const ApplicationActionInfo *table = ApplicationActionTable();
	const std::size_t count = ApplicationActionCount();
	for (std::size_t i = 0; i < count; ++i) {
		const ApplicationActionInfo &info = table[i];
		if (info.menu != menu) {
			continue;
		}
		if (info.separatorBefore) {
			height += kSeparatorHeight;
		}
		height += kItemHeight;
	}
	return height;
}

// Near Platform chrome and TabStrip fills.
const ColourRGBA kBarFill(0xe8, 0xe8, 0xe8, 0xff);
const ColourRGBA kBarBorder(0xb8, 0xb8, 0xb8, 0xff);
const ColourRGBA kHeadingHover(0xee, 0xee, 0xee, 0xff);
const ColourRGBA kHeadingOpen(0xd0, 0xe4, 0xf8, 0xff);
const ColourRGBA kDropdownFill(0xfc, 0xfc, 0xfc, 0xff);
const ColourRGBA kDropdownBorder(0xa0, 0xa0, 0xa0, 0xff);
const ColourRGBA kDropdownShadow(0x00, 0x00, 0x00, 0x28);
const ColourRGBA kItemHover(0xe8, 0xf0, 0xf8, 0xff);
const ColourRGBA kItemFocus(0xd0, 0xe4, 0xf8, 0xff);
const ColourRGBA kSeparator(0xd0, 0xd0, 0xd0, 0xff);
const ColourRGBA kText(0x20, 0x20, 0x20, 0xff);
const ColourRGBA kMutedText(0x60, 0x60, 0x60, 0xff);
const ColourRGBA kDisabledText(0xa0, 0xa0, 0xa0, 0xff);

void DrawInsideFrame(Surface &surface, const PRectangle &rc, ColourRGBA colour,
	XYPOSITION thickness) {
	if (rc.Empty() || thickness <= 0.0) {
		return;
	}
	const XYPOSITION t = std::min(thickness, std::min(rc.Width(), rc.Height()) / 2.0);
	const Fill fill(colour);
	surface.FillRectangle(PRectangle(rc.left, rc.top, rc.right, rc.top + t), fill);
	surface.FillRectangle(PRectangle(rc.left, rc.bottom - t, rc.right, rc.bottom), fill);
	surface.FillRectangle(PRectangle(rc.left, rc.top + t, rc.left + t, rc.bottom - t), fill);
	surface.FillRectangle(PRectangle(rc.right - t, rc.top + t, rc.right, rc.bottom - t), fill);
}

void DrawLeftAlignedLabel(Surface &surface, const PRectangle &rc, const Font *font,
	std::string_view text, ColourRGBA fore) {
	if (!font || text.empty() || rc.Empty()) {
		return;
	}
	const XYPOSITION ascent = surface.Ascent(font);
	const XYPOSITION height = surface.Height(font);
	const XYPOSITION x = static_cast<XYPOSITION>(static_cast<int>(rc.left));
	const XYPOSITION ybase = static_cast<XYPOSITION>(static_cast<int>(
		rc.top + (rc.Height() - height) / 2.0 + ascent));
	const PRectangle textRc(x, rc.top, rc.right, rc.bottom);
	surface.DrawTextTransparent(textRc, font, ybase, text, fore);
}

void DrawRightAlignedLabel(Surface &surface, const PRectangle &rc, const Font *font,
	std::string_view text, ColourRGBA fore) {
	if (!font || text.empty() || rc.Empty()) {
		return;
	}
	const XYPOSITION textWidth = surface.WidthText(font, text);
	const XYPOSITION ascent = surface.Ascent(font);
	const XYPOSITION height = surface.Height(font);
	const XYPOSITION x = static_cast<XYPOSITION>(static_cast<int>(
		std::max(rc.left, rc.right - textWidth)));
	const XYPOSITION ybase = static_cast<XYPOSITION>(static_cast<int>(
		rc.top + (rc.Height() - height) / 2.0 + ascent));
	const PRectangle textRc(x, rc.top, rc.right, rc.bottom);
	surface.DrawTextTransparent(textRc, font, ybase, text, fore);
}

void DrawCenteredLabel(Surface &surface, const PRectangle &rc, const Font *font,
	std::string_view text, ColourRGBA fore) {
	if (!font || text.empty() || rc.Empty()) {
		return;
	}
	const XYPOSITION textWidth = surface.WidthText(font, text);
	const XYPOSITION ascent = surface.Ascent(font);
	const XYPOSITION height = surface.Height(font);
	const XYPOSITION x = static_cast<XYPOSITION>(static_cast<int>(
		rc.left + (rc.Width() - textWidth) / 2.0));
	const XYPOSITION ybase = static_cast<XYPOSITION>(static_cast<int>(
		rc.top + (rc.Height() - height) / 2.0 + ascent));
	const PRectangle textRc(x, rc.top, x + textWidth, rc.bottom);
	surface.DrawTextTransparent(textRc, font, ybase, text, fore);
}

}

bool MenuBarModel::IsEnabled(ApplicationAction action) const noexcept {
	switch (action) {
	case ApplicationAction::NewTab:
	case ApplicationAction::Open:
	case ApplicationAction::Save:
	case ApplicationAction::SaveAs:
	case ApplicationAction::CloseTab:
	case ApplicationAction::Quit:
		return true;
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
	}
	return false;
}

int MenuBarHeight() noexcept {
	return kBarHeight;
}

MenuBarLayout LayoutMenuBar(int frameWidth, int frameHeight,
	const MenuBarModel &model) noexcept {
	const PRectangle empty = PRectangle::FromInts(0, 0, 0, 0);
	MenuBarLayout layout{empty, {}, empty, std::nullopt, {}};
	if (frameWidth <= 0) {
		return layout;
	}

	const int height = kBarHeight;
	layout.bar = PRectangle::FromInts(0, 0, frameWidth, height);

	// Headings pack left-to-right; clamp each to the remaining bar width.
	int x = 0;
	const ApplicationMenu menus[] = {ApplicationMenu::File, ApplicationMenu::Edit};
	for (ApplicationMenu menu : menus) {
		const int preferred = HeadingWidth(menu);
		const int available = std::max(0, frameWidth - x);
		const int used = std::min(preferred, available);
		if (used <= 0) {
			// Still record a zero-width heading so tests can see menu order
			// when the window is extremely narrow.
			layout.headings.push_back(MenuBarHeadingLayout{
				menu, empty, HeadingLabel(menu)});
			continue;
		}
		layout.headings.push_back(MenuBarHeadingLayout{
			menu,
			PRectangle::FromInts(x, 0, x + used, height),
			HeadingLabel(menu),
		});
		x += used;
	}

	if (!model.openMenu.has_value()) {
		return layout;
	}

	const ApplicationMenu open = *model.openMenu;
	const MenuBarHeadingLayout *openHeading = nullptr;
	for (const MenuBarHeadingLayout &heading : layout.headings) {
		if (heading.menu == open && NonEmpty(heading.bounds)) {
			openHeading = &heading;
			break;
		}
	}
	if (!openHeading) {
		return layout;
	}

	int dropdownWidth = kDropdownPreferredWidth;
	if (dropdownWidth > frameWidth) {
		dropdownWidth = frameWidth;
	}
	int dropdownLeft = static_cast<int>(openHeading->bounds.left);
	if (dropdownLeft + dropdownWidth > frameWidth) {
		dropdownLeft = std::max(0, frameWidth - dropdownWidth);
	}

	const int contentHeight = DropdownContentHeight(open);
	int dropdownTop = height;
	int dropdownBottom = dropdownTop + contentHeight;
	// Keep the panel on-screen when the frame is shorter than the menu.
	if (frameHeight <= height) {
		return layout;
	}
	if (dropdownBottom > frameHeight) {
		dropdownBottom = frameHeight;
	}

	layout.dropdown = PRectangle::FromInts(
		dropdownLeft, dropdownTop, dropdownLeft + dropdownWidth, dropdownBottom);
	layout.dropdownMenu = open;

	const int innerLeft = dropdownLeft;
	const int innerRight = dropdownLeft + dropdownWidth;
	const int shortcutWidth = std::min(kShortcutColumnWidth,
		std::max(0, dropdownWidth - kLabelPadLeft - kShortcutPadRight -
			kLabelShortcutGap / 2));

	int y = dropdownTop + kDropdownPadY;
	const ApplicationActionInfo *table = ApplicationActionTable();
	const std::size_t count = ApplicationActionCount();
	for (std::size_t i = 0; i < count; ++i) {
		const ApplicationActionInfo &info = table[i];
		if (info.menu != open) {
			continue;
		}

		PRectangle separator = empty;
		if (info.separatorBefore) {
			const int sepBottom = y + kSeparatorHeight;
			if (sepBottom > dropdownBottom) {
				break;
			}
			// Horizontal rule centered in the separator band.
			const int ruleY = y + kSeparatorHeight / 2;
			separator = PRectangle::FromInts(
				innerLeft + kLabelPadLeft, ruleY,
				innerRight - kShortcutPadRight, ruleY + 1);
			y = sepBottom;
		}

		const int rowBottom = y + kItemHeight;
		if (rowBottom > dropdownBottom) {
			break;
		}
		const PRectangle row = PRectangle::FromInts(
			innerLeft, y, innerRight, rowBottom);
		const int labelRight = std::max(
			innerLeft + kLabelPadLeft,
			innerRight - kShortcutPadRight - shortcutWidth - kLabelShortcutGap);
		const PRectangle label = PRectangle::FromInts(
			innerLeft + kLabelPadLeft, y, labelRight, rowBottom);
		const PRectangle shortcut = PRectangle::FromInts(
			innerRight - kShortcutPadRight - shortcutWidth, y,
			innerRight - kShortcutPadRight, rowBottom);

		layout.items.push_back(MenuBarItemLayout{
			info.action,
			info.separatorBefore,
			model.IsEnabled(info.action),
			row,
			separator,
			label,
			shortcut,
			info.label,
			info.shortcutLabel,
		});
		y = rowBottom;
	}

	return layout;
}

MenuBarHitResult HitTestMenuBar(const MenuBarLayout &layout, Point point) noexcept {
	// Prefer the open dropdown over the bar so item hits win under the panel.
	if (NonEmptyContains(layout.dropdown, point)) {
		for (const MenuBarItemLayout &item : layout.items) {
			if (NonEmptyContains(item.row, point)) {
				const ApplicationActionInfo &info = InfoFor(item.action);
				return {MenuBarHit::Item, info.menu, item.action};
			}
		}
		// Separators and padding inside the panel are not actionable.
		return {MenuBarHit::Dropdown,
			layout.dropdownMenu.value_or(ApplicationMenu::File),
			ApplicationAction::NewTab};
	}

	if (!NonEmptyContains(layout.bar, point)) {
		return {};
	}

	for (const MenuBarHeadingLayout &heading : layout.headings) {
		if (NonEmptyContains(heading.bounds, point)) {
			return {MenuBarHit::Heading, heading.menu, ApplicationAction::NewTab};
		}
	}
	return {MenuBarHit::Bar, ApplicationMenu::File, ApplicationAction::NewTab};
}

MenuBarPainter::MenuBarPainter() {
	labelFont = Font::Allocate(FontParameters{"system-ui", PixelSizeFromPoints(12.0f)});
}

void MenuBarPainter::PaintBar(Surface &surface, const MenuBarLayout &layout,
	const MenuBarModel &model) const {
	if (layout.bar.Empty()) {
		return;
	}

	surface.FillRectangle(layout.bar, Fill(kBarFill));
	// Bottom edge separates the menu bar from chrome below.
	surface.FillRectangle(
		PRectangle(layout.bar.left, layout.bar.bottom - 1.0,
			layout.bar.right, layout.bar.bottom),
		Fill(kBarBorder));

	const Font *font = labelFont.get();
	for (const MenuBarHeadingLayout &heading : layout.headings) {
		if (!NonEmpty(heading.bounds)) {
			continue;
		}
		const bool open = model.openMenu.has_value() &&
			*model.openMenu == heading.menu;
		const bool hovered = model.hoveredHeading.has_value() &&
			*model.hoveredHeading == heading.menu;
		if (open) {
			surface.FillRectangle(heading.bounds, Fill(kHeadingOpen));
		} else if (hovered) {
			surface.FillRectangle(heading.bounds, Fill(kHeadingHover));
		}
		if (font) {
			// Inset so centered labels do not sit on the heading edge.
			const PRectangle textRc(
				heading.bounds.left + kHeadingPadX / 4.0,
				heading.bounds.top,
				heading.bounds.right - kHeadingPadX / 4.0,
				heading.bounds.bottom);
			DrawCenteredLabel(surface, textRc, font, heading.label, kText);
		}
	}
}

void MenuBarPainter::PaintDropdown(Surface &surface, const MenuBarLayout &layout,
	const MenuBarModel &model) const {
	if (!NonEmpty(layout.dropdown)) {
		return;
	}

	// Soft shadow offset one pixel down-right; translucent fill is fine for
	// the brief overlay path.
	const PRectangle shadow(
		layout.dropdown.left + 2.0, layout.dropdown.top + 2.0,
		layout.dropdown.right + 2.0, layout.dropdown.bottom + 2.0);
	surface.FillRectangle(shadow, Fill(kDropdownShadow));

	surface.FillRectangle(layout.dropdown, Fill(kDropdownFill));

	const Font *font = labelFont.get();
	for (const MenuBarItemLayout &item : layout.items) {
		if (item.separatorBefore && NonEmpty(item.separator)) {
			surface.FillRectangle(item.separator, Fill(kSeparator));
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
				Fill(focused ? kItemFocus : kItemHover));
		}

		if (!font) {
			continue;
		}
		const ColourRGBA ink = item.enabled ? kText : kDisabledText;
		const ColourRGBA shortcutInk = item.enabled ? kMutedText : kDisabledText;
		DrawLeftAlignedLabel(surface, item.label, font, item.labelText, ink);
		DrawRightAlignedLabel(surface, item.shortcut, font, item.shortcutText,
			shortcutInk);
	}

	// Draw last so row hover and focus fills cannot cover the panel edge.
	DrawInsideFrame(surface, layout.dropdown, kDropdownBorder, 1.0);
}

void MenuBarPainter::Paint(Surface &surface, const MenuBarLayout &layout,
	const MenuBarModel &model) const {
	PaintBar(surface, layout, model);
	PaintDropdown(surface, layout, model);
}

}
