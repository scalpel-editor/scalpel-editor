// Fixed logical layout, hit-testing, and opaque painting for the File / Edit menu bar.
// main owns MenuBarModel, converts input into model transitions, and paints via
// ApplicationEditor permanent chrome and the overlay slot for open dropdowns.
// Layout and hit-testing stay Wayland-free.

#ifndef MENUBAR_H
#define MENUBAR_H

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "ApplicationAction.h"
#include "Geometry.h"
#include "Platform.h"

namespace Scalpel {

/** Fixed menu bar height in logical client pixels. */
[[nodiscard]] int MenuBarHeight() noexcept;

/**
 * Transient menu bar state: open dropdown, hover, keyboard focus, and item
 * enablement. Pointer press tracking is added with input handling.
 */
struct MenuBarModel {
	/** Open dropdown; nullopt means closed. */
	std::optional<ApplicationMenu> openMenu;
	/** Heading under the pointer (File or Edit). */
	std::optional<ApplicationMenu> hoveredHeading;
	/** Item under the pointer while a dropdown is open. */
	std::optional<ApplicationAction> hoveredItem;
	/** Keyboard-focused actionable item while a dropdown is open. */
	std::optional<ApplicationAction> focusedItem;

	bool undoEnabled = true;
	bool redoEnabled = true;
	bool cutEnabled = true;
	bool copyEnabled = true;
	bool pasteEnabled = true;
	bool selectAllEnabled = true;

	/** File actions are always enabled; edit flags follow the fields above. */
	[[nodiscard]] bool IsEnabled(ApplicationAction action) const noexcept;
};

struct MenuBarHeadingLayout {
	ApplicationMenu menu = ApplicationMenu::File;
	Scintilla::Internal::PRectangle bounds;
	std::string_view label;
};

struct MenuBarItemLayout {
	ApplicationAction action = ApplicationAction::NewTab;
	bool separatorBefore = false;
	bool enabled = true;
	/** Full row hit target for the action (excludes the separator band). */
	Scintilla::Internal::PRectangle row;
	/** Thin rule above the row when separatorBefore is true; empty otherwise. */
	Scintilla::Internal::PRectangle separator;
	Scintilla::Internal::PRectangle label;
	Scintilla::Internal::PRectangle shortcut;
	std::string_view labelText;
	std::string_view shortcutText;
};

struct MenuBarLayout {
	Scintilla::Internal::PRectangle bar;
	std::vector<MenuBarHeadingLayout> headings;
	/** Dropdown panel while a menu is open; empty when closed. */
	Scintilla::Internal::PRectangle dropdown;
	/** Menu represented by dropdown; nullopt when no dropdown fits. */
	std::optional<ApplicationMenu> dropdownMenu;
	std::vector<MenuBarItemLayout> items;
};

enum class MenuBarHit {
	None,
	/** File or Edit heading. */
	Heading,
	/** Actionable item row inside the open dropdown. */
	Item,
	/** Separator band or empty chrome inside the open dropdown. */
	Dropdown,
	/** Empty permanent bar chrome (not a heading). */
	Bar,
};

struct MenuBarHitResult {
	MenuBarHit kind = MenuBarHit::None;
	ApplicationMenu menu = ApplicationMenu::File;
	ApplicationAction action = ApplicationAction::NewTab;
};

/**
 * Lay out the permanent bar for frameWidth logical pixels at y = 0.
 * When model.openMenu is set, places the dropdown under that heading and
 * clamps it into the frame. Zero or negative width yields an empty layout.
 * frameHeight clamps the dropdown bottom so it stays on-screen when short.
 */
[[nodiscard]] MenuBarLayout LayoutMenuBar(int frameWidth, int frameHeight,
	const MenuBarModel &model) noexcept;

[[nodiscard]] MenuBarHitResult HitTestMenuBar(const MenuBarLayout &layout,
	Scintilla::Internal::Point point) noexcept;

/**
 * Owns the menu font. Construct once beside the shell and reuse across frames.
 * Paints the permanent bar; paints the open dropdown when layout.dropdown is
 * non-empty (caller may route that through the overlay painter).
 */
class MenuBarPainter final {
public:
	MenuBarPainter();
	~MenuBarPainter() = default;

	MenuBarPainter(const MenuBarPainter &) = delete;
	MenuBarPainter &operator=(const MenuBarPainter &) = delete;

	/** Permanent File / Edit strip only (no dropdown). */
	void PaintBar(Scintilla::Internal::Surface &surface,
		const MenuBarLayout &layout,
		const MenuBarModel &model) const;

	/** Open dropdown panel and its items; no-op when the menu is closed. */
	void PaintDropdown(Scintilla::Internal::Surface &surface,
		const MenuBarLayout &layout,
		const MenuBarModel &model) const;

	/** Bar then dropdown; convenient for offscreen tests. */
	void Paint(Scintilla::Internal::Surface &surface,
		const MenuBarLayout &layout,
		const MenuBarModel &model) const;

	[[nodiscard]] const Scintilla::Internal::Font *LabelFont() const noexcept {
		return labelFont.get();
	}

private:
	std::shared_ptr<Scintilla::Internal::Font> labelFont;
};

}

#endif
