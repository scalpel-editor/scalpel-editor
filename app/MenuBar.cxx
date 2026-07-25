#include "MenuBar.h"

#include <algorithm>

#include "DocumentFile.h"

namespace Scalpel {

MenuBarItemId MenuBarItemId::RecentFile(std::size_t index) noexcept {
	MenuBarItemId item;
	item.kind = MenuBarItemKind::RecentFile;
	item.recentIndex = index;
	return item;
}

MenuBarItemId MenuBarItemId::ClearRecentFiles() noexcept {
	MenuBarItemId item;
	item.kind = MenuBarItemKind::ClearRecentFiles;
	return item;
}

MenuBarItemId MenuBarItemId::EmptyRecentFiles() noexcept {
	MenuBarItemId item;
	item.kind = MenuBarItemKind::EmptyRecentFiles;
	return item;
}

bool operator==(const MenuBarItemId &left,
	const MenuBarItemId &right) noexcept {
	return left.kind == right.kind && left.action == right.action &&
		left.recentIndex == right.recentIndex;
}

bool operator!=(const MenuBarItemId &left,
	const MenuBarItemId &right) noexcept {
	return !(left == right);
}

bool operator==(const MenuBarItemId &left, ApplicationAction right) noexcept {
	return left.kind == MenuBarItemKind::ApplicationAction &&
		left.action == right;
}

bool operator!=(const MenuBarItemId &left, ApplicationAction right) noexcept {
	return !(left == right);
}

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
constexpr int kRecentHeadingWidth = 68;
constexpr int kItemHeight = 24;
constexpr int kSeparatorHeight = 9;
constexpr int kDropdownPadY = 4;
constexpr int kLabelPadLeft = 12;
constexpr int kShortcutPadRight = 12;
constexpr int kLabelShortcutGap = 24;
constexpr int kShortcutColumnWidth = 100;
// Fits "Save As…" + gap + "Ctrl+Shift+S" at the menu font with padding.
constexpr int kDropdownPreferredWidth = 220;
constexpr int kRecentDropdownPreferredWidth = 440;

std::vector<MenuBarItemId> OrderedItems(const MenuBarModel &model,
	ApplicationMenu menu) {
	std::vector<MenuBarItemId> items;
	if (menu == ApplicationMenu::Recent) {
		if (model.recentFiles.empty()) {
			items.push_back(MenuBarItemId::EmptyRecentFiles());
			return items;
		}
		items.reserve(model.recentFiles.size() + 1);
		for (std::size_t index = 0; index < model.recentFiles.size(); ++index) {
			items.push_back(MenuBarItemId::RecentFile(index));
		}
		items.push_back(MenuBarItemId::ClearRecentFiles());
		return items;
	}

	const ApplicationActionInfo *table = ApplicationActionTable();
	const std::size_t count = ApplicationActionCount();
	for (std::size_t i = 0; i < count; ++i) {
		const ApplicationActionInfo &info = table[i];
		if (info.menu == menu) {
			items.emplace_back(info.action);
		}
	}
	return items;
}

/** First enabled item in menu order for the open or named menu. */
std::optional<MenuBarItemId> FirstEnabledItem(const MenuBarModel &model,
	ApplicationMenu menu) {
	for (const MenuBarItemId item : OrderedItems(model, menu)) {
		if (model.IsEnabled(item)) {
			return item;
		}
	}
	return std::nullopt;
}

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
	case ApplicationMenu::Recent:
		return "Recent";
	}
	return "File";
}

int HeadingWidth(ApplicationMenu menu) noexcept {
	switch (menu) {
	case ApplicationMenu::File:
		return kFileHeadingWidth;
	case ApplicationMenu::Edit:
		return kEditHeadingWidth;
	case ApplicationMenu::Recent:
		return kRecentHeadingWidth;
	}
	return kFileHeadingWidth;
}

int DropdownPreferredWidth(ApplicationMenu menu) noexcept {
	return menu == ApplicationMenu::Recent ?
		kRecentDropdownPreferredWidth : kDropdownPreferredWidth;
}

int DropdownContentHeight(const MenuBarModel &model, ApplicationMenu menu) {
	int height = kDropdownPadY * 2;
	for (const MenuBarItemId item : OrderedItems(model, menu)) {
		if (item.kind == MenuBarItemKind::ClearRecentFiles ||
			(item.kind == MenuBarItemKind::ApplicationAction &&
				InfoFor(item.action).separatorBefore)) {
			height += kSeparatorHeight;
		}
		height += kItemHeight;
	}
	return height;
}

bool SeparatorBefore(MenuBarItemId item) noexcept {
	if (item.kind == MenuBarItemKind::ClearRecentFiles) {
		return true;
	}
	return item.kind == MenuBarItemKind::ApplicationAction &&
		InfoFor(item.action).separatorBefore;
}

std::string ItemLabel(const MenuBarModel &model, MenuBarItemId item) {
	switch (item.kind) {
	case MenuBarItemKind::ApplicationAction:
		return std::string(InfoFor(item.action).label);
	case MenuBarItemKind::RecentFile:
		if (item.recentIndex >= model.recentFiles.size()) {
			return {};
		}
		{
			const std::string &path = model.recentFiles[item.recentIndex];
			const std::string name = DocumentBaseName(path);
			const std::string directory = DocumentDirectory(path);
			if (directory.empty() || name.empty()) {
				return path;
			}
			return name + " \xe2\x80\x94 " + directory;
		}
	case MenuBarItemKind::ClearRecentFiles:
		return "Clear Recent Files";
	case MenuBarItemKind::EmptyRecentFiles:
		return "No Recent Files";
	}
	return {};
}

std::string ItemShortcut(MenuBarItemId item) {
	if (item.kind == MenuBarItemKind::ApplicationAction) {
		return std::string(InfoFor(item.action).shortcutLabel);
	}
	return {};
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

bool MenuBarModel::IsEnabled(MenuBarItemId item) const noexcept {
	switch (item.kind) {
	case MenuBarItemKind::ApplicationAction:
		return IsEnabled(item.action);
	case MenuBarItemKind::RecentFile:
		return item.recentIndex < recentFiles.size();
	case MenuBarItemKind::ClearRecentFiles:
		return !recentFiles.empty();
	case MenuBarItemKind::EmptyRecentFiles:
		return false;
	}
	return false;
}

bool UpdateMenuBarActionState(MenuBarModel &model, ApplicationEditor &editor) {
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
	if (model.openMenu.has_value() && model.focusedItem.has_value() &&
		!model.IsEnabled(*model.focusedItem)) {
		model.focusedItem = FirstEnabledItem(model, *model.openMenu);
	}
	return changed;
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
	const ApplicationMenu menus[] = {
		ApplicationMenu::File,
		ApplicationMenu::Edit,
		ApplicationMenu::Recent,
	};
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

	int dropdownWidth = DropdownPreferredWidth(open);
	if (dropdownWidth > frameWidth) {
		dropdownWidth = frameWidth;
	}
	int dropdownLeft = static_cast<int>(openHeading->bounds.left);
	if (dropdownLeft + dropdownWidth > frameWidth) {
		dropdownLeft = std::max(0, frameWidth - dropdownWidth);
	}

	const int contentHeight = DropdownContentHeight(model, open);
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
	for (const MenuBarItemId item : OrderedItems(model, open)) {
		const bool separatorBefore = SeparatorBefore(item);
		PRectangle separator = empty;
		if (separatorBefore) {
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
			item,
			item.action,
			separatorBefore,
			model.IsEnabled(item),
			row,
			separator,
			label,
			shortcut,
			ItemLabel(model, item),
			ItemShortcut(item),
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
				return {
					MenuBarHit::Item,
					layout.dropdownMenu.value_or(ApplicationMenu::File),
					item.item,
					item.action,
				};
			}
		}
		// Separators and padding inside the panel are not actionable.
		return {MenuBarHit::Dropdown,
			layout.dropdownMenu.value_or(ApplicationMenu::File),
			MenuBarItemId{},
			ApplicationAction::NewTab};
	}

	if (!NonEmptyContains(layout.bar, point)) {
		return {};
	}

	for (const MenuBarHeadingLayout &heading : layout.headings) {
		if (NonEmptyContains(heading.bounds, point)) {
			return {MenuBarHit::Heading, heading.menu, MenuBarItemId{},
				ApplicationAction::NewTab};
		}
	}
	return {MenuBarHit::Bar, ApplicationMenu::File, MenuBarItemId{},
		ApplicationAction::NewTab};
}

void CloseMenuBar(MenuBarModel &model) noexcept {
	model.openMenu.reset();
	model.hoveredItem.reset();
	model.focusedItem.reset();
	model.pressOrigin.reset();
}

namespace {

bool ItemEnabled(const MenuBarLayout &layout, MenuBarItemId wanted) noexcept {
	for (const MenuBarItemLayout &item : layout.items) {
		if (item.item == wanted) {
			return item.enabled;
		}
	}
	return false;
}

bool SetOptional(std::optional<ApplicationMenu> &slot,
	std::optional<ApplicationMenu> next) noexcept {
	if (slot == next) {
		return false;
	}
	slot = next;
	return true;
}

bool SetOptional(std::optional<MenuBarItemId> &slot,
	std::optional<MenuBarItemId> next) noexcept {
	if (slot == next) {
		return false;
	}
	slot = next;
	return true;
}

bool PointerOverMenuChrome(const MenuBarLayout &layout, Point point) noexcept {
	return NonEmptyContains(layout.bar, point) ||
		NonEmptyContains(layout.dropdown, point);
}

}

MenuBarPointerResult HandleMenuBarPointer(MenuBarModel &model,
	const MenuBarLayout &layout, const PointerInput &input,
	bool editorMouseCaptured) noexcept {
	MenuBarPointerResult result;
	const Point point(input.x, input.y);

	if (input.action == PointerAction::Leave) {
		result.barDirty = SetOptional(model.hoveredHeading, std::nullopt);
		const bool itemCleared = SetOptional(model.hoveredItem, std::nullopt);
		if (itemCleared) {
			result.frameDirty = model.openMenu.has_value();
		}
		// Leave does not close the menu; focus loss does that later.
		model.pressOrigin.reset();
		result.pointerOverMenu = false;
		// Surface leave still needs to clear editor hover and capture.
		result.consumed = false;
		return result;
	}

	const MenuBarHitResult hit = HitTestMenuBar(layout, point);
	result.pointerOverMenu = PointerOverMenuChrome(layout, point) ||
		hit.kind != MenuBarHit::None;

	// A selection drag that began in the editor keeps motion and release.
	// The menu must not steal that release or intercept capture motion.
	if (editorMouseCaptured) {
		result.consumed = false;
		return result;
	}

	const bool menuOpen = model.openMenu.has_value();

	if (input.action == PointerAction::Move) {
		if (hit.kind == MenuBarHit::Heading) {
			result.barDirty = SetOptional(model.hoveredHeading, hit.menu);
			// Moving across headings while open switches menus.
			if (menuOpen && *model.openMenu != hit.menu) {
				model.openMenu = hit.menu;
				model.hoveredItem.reset();
				model.focusedItem.reset();
				model.pressOrigin.reset();
				result.frameDirty = true;
			}
			if (SetOptional(model.hoveredItem, std::nullopt) && menuOpen) {
				result.frameDirty = true;
			}
		} else if (hit.kind == MenuBarHit::Item) {
			result.barDirty = SetOptional(model.hoveredHeading, std::nullopt);
			if (SetOptional(model.hoveredItem, hit.item)) {
				result.frameDirty = true;
			}
		} else {
			result.barDirty = SetOptional(model.hoveredHeading, std::nullopt);
			if (SetOptional(model.hoveredItem, std::nullopt) && menuOpen) {
				result.frameDirty = true;
			}
		}
		// Consume moves over the bar or open dropdown so the editor does not
		// show text-cursor hover under chrome.
		result.consumed = menuOpen || hit.kind != MenuBarHit::None;
		return result;
	}

	if (input.action == PointerAction::Scroll) {
		// Wheel over chrome or an open menu must not scroll the document.
		result.consumed = menuOpen || hit.kind != MenuBarHit::None;
		return result;
	}

	if (input.action == PointerAction::Press && input.button == 0) {
		if (hit.kind == MenuBarHit::Heading) {
			if (menuOpen && *model.openMenu == hit.menu) {
				// Toggle: press the open heading again to close.
				CloseMenuBar(model);
				result.barDirty = true;
				result.frameDirty = true;
			} else {
				const bool opening = !menuOpen;
				const bool switching = menuOpen && *model.openMenu != hit.menu;
				model.openMenu = hit.menu;
				model.hoveredItem.reset();
				model.focusedItem.reset();
				model.pressOrigin = MenuBarPressOrigin{
					MenuBarPressKind::Heading, hit.menu, ApplicationAction::NewTab};
				model.hoveredHeading = hit.menu;
				result.barDirty = true;
				result.frameDirty = opening || switching;
			}
			result.consumed = true;
			return result;
		}

		if (menuOpen && hit.kind == MenuBarHit::Item) {
			model.pressOrigin = MenuBarPressOrigin{
				MenuBarPressKind::Item, hit.menu, hit.item};
			model.hoveredItem = hit.item;
			result.frameDirty = true;
			result.consumed = true;
			return result;
		}

		if (menuOpen) {
			// Press on dropdown padding, empty bar, or outside: close and
			// consume so the click cannot activate a tab or move the caret.
			CloseMenuBar(model);
			model.pressOrigin = MenuBarPressOrigin{
				MenuBarPressKind::Dismissal, ApplicationMenu::File,
				ApplicationAction::NewTab};
			result.barDirty = true;
			result.frameDirty = true;
			result.consumed = true;
			return result;
		}

		// Closed: empty bar chrome still swallows presses.
		result.consumed = hit.kind != MenuBarHit::None;
		return result;
	}

	if (input.action == PointerAction::Release && input.button == 0) {
		if (!model.pressOrigin.has_value()) {
			// Outside dismissal closed on press; still consume releases that
			// land on chrome or while a menu was open for this gesture.
			result.consumed = menuOpen || hit.kind != MenuBarHit::None;
			return result;
		}

		const MenuBarPressOrigin origin = *model.pressOrigin;
		model.pressOrigin.reset();

		if (origin.kind == MenuBarPressKind::Dismissal) {
			// The press closed the menu, but its matching release still belongs
			// to that dismissal gesture and must not reach the editor.
			result.consumed = true;
			return result;
		}

		if (origin.kind == MenuBarPressKind::Item &&
			hit.kind == MenuBarHit::Item &&
			hit.item == origin.item &&
			ItemEnabled(layout, hit.item)) {
			// Matching press/release on an enabled item runs the action and
			// closes before the caller dispatches application state changes.
			CloseMenuBar(model);
			result.activated = hit.item;
			result.barDirty = true;
			result.frameDirty = true;
			result.consumed = true;
			return result;
		}

		// Mismatched release, disabled item, or heading press: no activation.
		// Disabled items stay open so the user can try another row.
		if (origin.kind == MenuBarPressKind::Item &&
			hit.kind == MenuBarHit::Item &&
			hit.item == origin.item &&
			!ItemEnabled(layout, hit.item)) {
			result.consumed = true;
			return result;
		}

		result.consumed = menuOpen || hit.kind != MenuBarHit::None;
		return result;
	}

	// Other buttons: swallow over chrome / open menu only.
	result.consumed = menuOpen || hit.kind != MenuBarHit::None;
	return result;
}

namespace {

/** Next enabled item in menu order for keyboard Up/Down wrapping. */
std::optional<MenuBarItemId> AdjacentEnabledItem(const MenuBarModel &model,
	ApplicationMenu menu, std::optional<MenuBarItemId> focused,
	bool forward) {
	const std::vector<MenuBarItemId> items = OrderedItems(model, menu);
	const std::size_t count = items.size();
	std::optional<std::size_t> focusedIndex;
	for (std::size_t i = 0; i < count; ++i) {
		if (focused.has_value() && items[i] == *focused &&
			model.IsEnabled(items[i])) {
			focusedIndex = i;
			break;
		}
	}

	if (!focusedIndex.has_value()) {
		if (forward) {
			return FirstEnabledItem(model, menu);
		}
		for (std::size_t i = count; i > 0; --i) {
			if (model.IsEnabled(items[i - 1])) {
				return items[i - 1];
			}
		}
		return std::nullopt;
	}

	for (std::size_t step = 1; step <= count; ++step) {
		const std::size_t index = forward ?
			(*focusedIndex + step) % count :
			(*focusedIndex + count - step) % count;
		if (model.IsEnabled(items[index])) {
			return items[index];
		}
	}
	return std::nullopt;
}

void OpenMenuFromKeyboard(MenuBarModel &model, ApplicationMenu menu,
	MenuBarKeyboardResult &result) noexcept {
	const bool wasOpen = model.openMenu.has_value();
	const bool sameMenu = wasOpen && *model.openMenu == menu;
	const std::optional<MenuBarItemId> first = FirstEnabledItem(model, menu);
	const bool focusChanged = model.focusedItem != first;
	const bool hoverChanged = model.hoveredItem.has_value();
	model.openMenu = menu;
	model.focusedItem = first;
	model.hoveredItem.reset();
	model.pressOrigin.reset();
	result.barDirty = true;
	result.frameDirty = !wasOpen || !sameMenu || focusChanged || hoverChanged;
	result.consumed = true;
}

bool IsBareMenuKey(const KeyboardInput &input) noexcept {
	return input.key == Scintilla::Keys::Menu &&
		input.modifiers == Scintilla::KeyMod::Norm;
}

bool IsAltLetter(const KeyboardInput &input, char letter) noexcept {
	return input.modifiers == Scintilla::KeyMod::Alt &&
		input.key == static_cast<Scintilla::Keys>(letter);
}

ApplicationMenu AdjacentMenu(ApplicationMenu menu, bool forward) noexcept {
	const ApplicationMenu menus[] = {
		ApplicationMenu::File,
		ApplicationMenu::Edit,
		ApplicationMenu::Recent,
	};
	std::size_t index = 0;
	for (std::size_t candidate = 0; candidate < 3; ++candidate) {
		if (menus[candidate] == menu) {
			index = candidate;
			break;
		}
	}
	index = forward ? (index + 1) % 3 : (index + 2) % 3;
	return menus[index];
}

}

MenuBarKeyboardResult HandleMenuBarKeyboard(MenuBarModel &model,
	const KeyboardInput &input) noexcept {
	MenuBarKeyboardResult result;
	const bool menuOpen = model.openMenu.has_value();

	// Releases never change menu state, but an open menu still owns them so
	// they cannot fall through into the editor as text or command keys.
	if (!input.pressed) {
		result.consumed = menuOpen;
		return result;
	}

	// Closed: only the open accelerators are handled.
	if (!menuOpen) {
		if (IsBareMenuKey(input) || IsAltLetter(input, 'F')) {
			OpenMenuFromKeyboard(model, ApplicationMenu::File, result);
			return result;
		}
		if (IsAltLetter(input, 'E')) {
			OpenMenuFromKeyboard(model, ApplicationMenu::Edit, result);
			return result;
		}
		if (IsAltLetter(input, 'R')) {
			OpenMenuFromKeyboard(model, ApplicationMenu::Recent, result);
			return result;
		}
		result.consumed = false;
		return result;
	}

	// Open: the menu owns every key so shortcuts and typing cannot leak.
	result.consumed = true;

	if (input.key == Scintilla::Keys::Escape) {
		CloseMenuBar(model);
		result.barDirty = true;
		result.frameDirty = true;
		return result;
	}

	// F10 / Menu re-opens File and focuses its first enabled item.
	if (IsBareMenuKey(input)) {
		OpenMenuFromKeyboard(model, ApplicationMenu::File, result);
		return result;
	}
	if (IsAltLetter(input, 'F')) {
		OpenMenuFromKeyboard(model, ApplicationMenu::File, result);
		return result;
	}
	if (IsAltLetter(input, 'E')) {
		OpenMenuFromKeyboard(model, ApplicationMenu::Edit, result);
		return result;
	}
	if (IsAltLetter(input, 'R')) {
		OpenMenuFromKeyboard(model, ApplicationMenu::Recent, result);
		return result;
	}

	if (input.key == Scintilla::Keys::Left ||
		input.key == Scintilla::Keys::Right) {
		const ApplicationMenu next = AdjacentMenu(*model.openMenu,
			input.key == Scintilla::Keys::Right);
		OpenMenuFromKeyboard(model, next, result);
		return result;
	}

	if (input.key == Scintilla::Keys::Down ||
		input.key == Scintilla::Keys::Up) {
		if (SetOptional(model.hoveredItem, std::nullopt)) {
			result.frameDirty = true;
		}
		model.pressOrigin.reset();
		const std::optional<MenuBarItemId> next = AdjacentEnabledItem(
			model, *model.openMenu, model.focusedItem,
			input.key == Scintilla::Keys::Down);
		if (next.has_value() && SetOptional(model.focusedItem, next)) {
			result.frameDirty = true;
		}
		return result;
	}

	if (input.key == Scintilla::Keys::Return) {
		if (model.focusedItem.has_value() &&
			model.IsEnabled(*model.focusedItem)) {
			result.activated = *model.focusedItem;
			CloseMenuBar(model);
			result.barDirty = true;
			result.frameDirty = true;
		}
		// Disabled focus or empty focus: swallow Enter without activating.
		return result;
	}

	// Unrelated keys while open: consumed, no state change.
	return result;
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
			*model.hoveredItem == item.item;
		const bool focused = model.focusedItem.has_value() &&
			*model.focusedItem == item.item;
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
