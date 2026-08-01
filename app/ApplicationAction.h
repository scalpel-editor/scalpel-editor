// Fixed File / Edit / Font application actions: metadata, shortcut match,
// enablement, and one dispatcher shared by keyboard shortcuts and the menu bar.
// ApplicationUi routes matched shortcuts and menu activations into
// DocumentWorkspace and ApplicationEditor; the list is compile-time-known and
// does not support registration or runtime menu construction.

#ifndef APPLICATIONACTION_H
#define APPLICATIONACTION_H

#include <cstddef>
#include <optional>
#include <string_view>

#include "ApplicationInput.h"
#include "EditorInputTypes.h"

namespace Scalpel {

class ApplicationEditor;
class DocumentWorkspace;

/** Compile-time-known application commands exposed by File, Edit, and Font. */
enum class ApplicationAction {
	NewTab,
	Open,
	Save,
	SaveAs,
	CloseTab,
	Quit,
	Undo,
	Redo,
	Cut,
	Copy,
	Paste,
	SelectAll,
	Find,
	ConvertLineEndingsToLf,
	ConvertLineEndingsToCrLf,
	FontMonospace,
	FontSerif,
	FontSans,
	FontSystem,
};

/** Which permanent menu bar heading owns an item. */
enum class ApplicationMenu {
	File,
	Edit,
	Font,
	Recent,
};

/**
 * Static description of one menu row (or the action behind a shortcut).
 * separatorBefore draws a rule above this item when the menu is painted.
 */
struct ApplicationActionInfo {
	ApplicationAction action = ApplicationAction::NewTab;
	ApplicationMenu menu = ApplicationMenu::File;
	std::string_view label;
	std::string_view shortcutLabel;
	Scintilla::Keys key = static_cast<Scintilla::Keys>(0);
	Scintilla::KeyMod modifiers = Scintilla::KeyMod::Norm;
	bool separatorBefore = false;
};

/** Ordered File then Edit items; separators are flags on following items. */
[[nodiscard]] const ApplicationActionInfo *ApplicationActionTable() noexcept;
[[nodiscard]] std::size_t ApplicationActionCount() noexcept;
[[nodiscard]] const ApplicationActionInfo &InfoFor(
	ApplicationAction action) noexcept;

/**
 * Map a key press to an application action. Requires pressed and an exact
 * key+modifier match on a bound shortcut; menu-only actions (key zero) never
 * match. Returns nullopt for tab cycling and unbound keys.
 */
[[nodiscard]] std::optional<ApplicationAction> MatchApplicationAction(
	const KeyboardInput &input) noexcept;

/**
 * Whether the action may run against the current editor and clipboard offer.
 * File and Font actions are always enabled; edit enablement follows document
 * history, selection, write state, and ClipboardPasteAvailable.
 */
[[nodiscard]] bool ApplicationActionEnabled(ApplicationAction action,
	ApplicationEditor &editor);

/**
 * Run one action through DocumentWorkspace or ApplicationEditor. Disabled edit
 * actions are ignored. File actions call the workspace methods that already
 * no-op while a dirty prompt is active.
 */
void DispatchApplicationAction(ApplicationAction action,
	DocumentWorkspace &workspace, ApplicationEditor &editor);

}

#endif
