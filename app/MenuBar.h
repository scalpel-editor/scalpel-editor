// Fixed logical layout, hit-testing, open-menu pointer and keyboard navigation,
// and opaque painting for the File / Edit menu bar. main owns MenuBarModel,
// refreshes edit enablement via UpdateMenuBarActionState before open and paint,
// converts input into model transitions via HandleMenuBarPointer and
// HandleMenuBarKeyboard, dispatches returned actions, and paints the permanent
// bar plus the overlay slot for open dropdowns. Layout, hit-testing, and input
// transitions stay Wayland-free.

#ifndef MENUBAR_H
#define MENUBAR_H

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "ApplicationAction.h"
#include "ApplicationInput.h"
#include "Geometry.h"
#include "Platform.h"

namespace Scalpel {

/** Fixed menu bar height in logical client pixels. */
[[nodiscard]] int MenuBarHeight() noexcept;

/** What a left-button press started on, for matching release activation. */
enum class MenuBarPressKind {
	Heading,
	Item,
	/** Outside press that closed an open menu; its release is also consumed. */
	Dismissal,
};

struct MenuBarPressOrigin {
	MenuBarPressKind kind = MenuBarPressKind::Heading;
	ApplicationMenu menu = ApplicationMenu::File;
	ApplicationAction action = ApplicationAction::NewTab;
};

/**
 * Transient menu bar state: open dropdown, hover, keyboard focus, press origin,
 * and item enablement.
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
	/** Left-button press origin for press/release matching. */
	std::optional<MenuBarPressOrigin> pressOrigin;

	bool undoEnabled = true;
	bool redoEnabled = true;
	bool cutEnabled = true;
	bool copyEnabled = true;
	bool pasteEnabled = true;
	bool selectAllEnabled = true;

	/** File actions are always enabled; edit flags follow the fields above. */
	[[nodiscard]] bool IsEnabled(ApplicationAction action) const noexcept;
};

/**
 * Copy ApplicationActionEnabled results into the model edit flags.
 * Call when a menu opens and whenever the open dropdown is about to paint so
 * active-document history, selection, and clipboard offer stay current. If a
 * live change disables the keyboard-focused item, focus moves to the first
 * enabled item in the open menu.
 * Returns true when any flag changed (caller may invalidate for repaint).
 */
bool UpdateMenuBarActionState(MenuBarModel &model,
	ApplicationEditor &editor);

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
 * Result of applying one pointer event to the menu bar model.
 * main uses consumed to stop tab-strip and editor delivery, dirty flags for
 * repaint, activated for the shared action dispatcher, and pointerOverMenu for
 * the arrow cursor over headings and the open dropdown.
 */
struct MenuBarPointerResult {
	/** True when the event must not reach the tab strip or editor. */
	bool consumed = false;
	/** Permanent bar paint needs InvalidateTopChrome (hover / open heading). */
	bool barDirty = false;
	/** Open, close, or dropdown hover needs full-frame invalidation. */
	bool frameDirty = false;
	/** Enabled item activated by a matching press and release. */
	std::optional<ApplicationAction> activated;
	/** Pointer is over the bar band or the open dropdown panel. */
	bool pointerOverMenu = false;
};

/**
 * Result of applying one keyboard event to the menu bar model.
 * Matches MenuBarPointerResult dirty and activation fields; consumed stops
 * delivery to the editor (and while open, all keys including releases).
 */
struct MenuBarKeyboardResult {
	bool consumed = false;
	bool barDirty = false;
	bool frameDirty = false;
	std::optional<ApplicationAction> activated;
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
 * Close the open dropdown and clear focus, item hover, and press origin.
 * Leaves heading hover alone so the bar can still show hover after close.
 */
void CloseMenuBar(MenuBarModel &model) noexcept;

/**
 * Apply one pointer event to the menu model.
 * layout must match the current openMenu and frame size. When
 * editorMouseCaptured is true, the menu does not consume motion or release so
 * Scintilla can finish an in-progress selection drag.
 */
[[nodiscard]] MenuBarPointerResult HandleMenuBarPointer(MenuBarModel &model,
	const MenuBarLayout &layout, const PointerInput &input,
	bool editorMouseCaptured) noexcept;

/**
 * Apply one keyboard event to the menu model.
 * F10 / Keys::Menu open File with the first enabled item focused. Alt+F and
 * Alt+E open the named menu. While open: Left/Right switch menus, Up/Down move
 * among enabled items with wrapping, Enter activates the focused enabled item,
 * Escape closes, and all other keys plus releases are consumed so they cannot
 * reach the editor. When closed, only the open accelerators are consumed.
 */
[[nodiscard]] MenuBarKeyboardResult HandleMenuBarKeyboard(MenuBarModel &model,
	const KeyboardInput &input) noexcept;

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
