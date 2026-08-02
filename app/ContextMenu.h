// Popup-local layout, hit-testing, pointer and keyboard navigation, and opaque
// painting for the fixed editor context menu (Undo, Redo, Cut, Copy, Paste,
// Select All). ApplicationUi owns ContextMenuModel, opens it from a right
// press or Shift+F10, and dispatches activated actions through
// ApplicationAction. Layout, input transitions, and painting stay Wayland-free;
// the shell only creates the xdg_popup surface and paints into it.
// UpdateContextMenuActionState refreshes edit enablement before input and paint.
// Coordinates are always popup-local (panel origin at 0,0) so hit testing does
// not depend on the compositor's parent-relative placement.

#ifndef CONTEXTMENU_H
#define CONTEXTMENU_H

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ApplicationAction.h"
#include "ApplicationInput.h"
#include "Geometry.h"
#include "Platform.h"
#include "UiStyle.h"

namespace Scalpel {

/** What a left-button press started on, for matching release activation. */
enum class ContextMenuPressKind {
	Item,
	/** Outside press that closed the menu; its release is also consumed. */
	Dismissal,
};

struct ContextMenuPressOrigin {
	ContextMenuPressKind kind = ContextMenuPressKind::Item;
	ApplicationAction action = ApplicationAction::Undo;

	ContextMenuPressOrigin() = default;
	ContextMenuPressOrigin(ContextMenuPressKind kind_,
		ApplicationAction action_) noexcept :
		kind(kind_), action(action_) {
	}
};

/**
 * Context menu state: open flag, hover, keyboard focus, press origin, and
 * item enablement. Opening and closing clear transient input fields.
 */
struct ContextMenuModel {
	bool open = false;
	/** Item under the pointer while open. */
	std::optional<ApplicationAction> hoveredItem;
	/** Keyboard-focused enabled item while open. */
	std::optional<ApplicationAction> focusedItem;
	/** True after pointer navigation until keyboard navigation resumes. */
	bool pointerNavigation = false;
	/** Left-button press origin for press/release matching. */
	std::optional<ContextMenuPressOrigin> pressOrigin;

	bool undoEnabled = true;
	bool redoEnabled = true;
	bool cutEnabled = true;
	bool copyEnabled = true;
	bool pasteEnabled = true;
	bool selectAllEnabled = true;

	/** Enablement for one of the six context-menu actions. */
	[[nodiscard]] bool IsEnabled(ApplicationAction action) const noexcept;
};

/**
 * Copy ApplicationActionEnabled results into the model. Call when the menu
 * opens and whenever it is about to accept input or paint so history,
 * selection, and clipboard offer stay current. If a live change disables the
 * keyboard-focused item, focus moves to the first enabled row.
 * Returns true when any flag changed (caller may invalidate for repaint).
 */
bool UpdateContextMenuActionState(ContextMenuModel &model,
	ApplicationEditor &editor);

struct ContextMenuItemLayout {
	ApplicationAction action = ApplicationAction::Undo;
	bool separatorBefore = false;
	bool enabled = true;
	/** Full row hit target (excludes the separator band). */
	Scintilla::Internal::PRectangle row;
	/** Thin rule above the row when separatorBefore is true; empty otherwise. */
	Scintilla::Internal::PRectangle separator;
	Scintilla::Internal::PRectangle label;
	Scintilla::Internal::PRectangle shortcut;
	std::string labelText;
	std::string shortcutText;
};

/**
 * Popup-local layout. panel is at (0,0) with the size used for hit testing and
 * paint. requestedWidth / requestedHeight are the unconstrained preferred size
 * for the shell positioner; they stay fixed even when max bounds clip the panel.
 */
struct ContextMenuLayout {
	Scintilla::Internal::PRectangle panel;
	int requestedWidth = 0;
	int requestedHeight = 0;
	std::vector<ContextMenuItemLayout> items;
};

enum class ContextMenuHit {
	None,
	/** Actionable item row. */
	Item,
	/** Separator band or empty panel chrome. */
	Panel,
};

struct ContextMenuHitResult {
	ContextMenuHit kind = ContextMenuHit::None;
	ApplicationAction action = ApplicationAction::Undo;
};

/**
 * Result of applying one pointer event to the context menu model.
 * consumed stops editor and other chrome delivery; dirty flags request repaint;
 * activated is set when an enabled item is confirmed by matching release.
 */
struct ContextMenuPointerResult {
	bool consumed = false;
	bool dirty = false;
	std::optional<ApplicationAction> activated;
};

/**
 * Result of applying one keyboard event to the context menu model.
 * While open, all keys and releases are consumed so they cannot reach the
 * editor; activated is set when Enter confirms the focused enabled item.
 */
struct ContextMenuKeyboardResult {
	bool consumed = false;
	bool dirty = false;
	std::optional<ApplicationAction> activated;
};

/**
 * Preferred panel size for the six fixed rows (independent of placement).
 * Does not depend on open state; used for shell positioner size.
 */
[[nodiscard]] int ContextMenuPreferredWidth() noexcept;
[[nodiscard]] int ContextMenuPreferredHeight() noexcept;

/**
 * Lay out the open menu in popup-local coordinates at (0,0).
 * When model.open is false, returns an empty layout with requested sizes still
 * filled. maxWidth / maxHeight of zero mean preferred size; positive values
 * clamp the panel and drop rows that no longer fit (narrow-size clipping).
 */
[[nodiscard]] ContextMenuLayout LayoutContextMenu(
	const ContextMenuModel &model, int maxWidth = 0,
	int maxHeight = 0) noexcept;

[[nodiscard]] ContextMenuHitResult HitTestContextMenu(
	const ContextMenuLayout &layout,
	Scintilla::Internal::Point point) noexcept;

/**
 * Mark the menu open, clear press/hover, and focus the first enabled item.
 * No-op when already open with the same focus.
 */
void OpenContextMenu(ContextMenuModel &model) noexcept;

/**
 * Close the menu and clear focus, hover, and press origin.
 */
void CloseContextMenu(ContextMenuModel &model) noexcept;

/**
 * Apply one pointer event while the menu owns popup-local coordinates.
 * layout must match the current model and size. Outside press dismisses and
 * is consumed; matching press/release on an enabled row activates and closes.
 */
[[nodiscard]] ContextMenuPointerResult HandleContextMenuPointer(
	ContextMenuModel &model, const ContextMenuLayout &layout,
	const PointerInput &input) noexcept;

/**
 * Apply one keyboard event while the menu is open.
 * Up/Down move among enabled items with wrapping; Home/End jump to the first
 * or last enabled item; Enter activates the focused enabled item; Escape
 * closes. All other keys and releases are consumed while open. When closed,
 * nothing is consumed (opening is ApplicationUi policy).
 */
[[nodiscard]] ContextMenuKeyboardResult HandleContextMenuKeyboard(
	ContextMenuModel &model, const KeyboardInput &input) noexcept;

/**
 * Owns the menu font. Construct once beside the shell and reuse across frames.
 * Paints the opaque panel and rows into a surface whose size matches the
 * popup; does not draw an exterior shadow (the surface is the panel).
 */
class ContextMenuPainter final {
public:
	explicit ContextMenuPainter(const UiStyle &style = DefaultUiStyle());
	~ContextMenuPainter() = default;

	ContextMenuPainter(const ContextMenuPainter &) = delete;
	ContextMenuPainter &operator=(const ContextMenuPainter &) = delete;

	void Paint(Scintilla::Internal::Surface &surface,
		const ContextMenuLayout &layout,
		const ContextMenuModel &model) const;

	[[nodiscard]] const Scintilla::Internal::Font *LabelFont() const noexcept {
		return labelFont.get();
	}

	[[nodiscard]] const UiStyle &Style() const noexcept { return style; }

private:
	UiStyle style;
	std::shared_ptr<Scintilla::Internal::Font> labelFont;
};

}

#endif
