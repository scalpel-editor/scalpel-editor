// Application chrome and overlay selection state for the production editor.
// Owns menu, tab-strip, scrollbar interaction, modal-card, error-queue, hover,
// press, and which overlay is bound. Holds references to ApplicationEditor,
// DocumentWorkspace, and RecentFiles; free routing functions still apply
// transitions for now.

#ifndef APPLICATIONUI_H
#define APPLICATIONUI_H

#include <deque>
#include <optional>
#include <string>

#include "DocumentId.h"
#include "DocumentWorkspace.h"
#include "MenuBar.h"
#include "ScrollBar.h"
#include "TabStrip.h"
#include "UnsavedChangesCard.h"

namespace Scalpel {

class ApplicationEditor;
class RecentFiles;

/** Which post-paint overlay painter is currently bound to the editor host. */
enum class BoundOverlay {
	None,
	Menu,
	UnsavedChanges,
	FileError,
};

/**
 * Owns permanent-chrome models, scrollbar interaction, modal-card focus,
 * file-error queue, pointer hover and press state, and overlay selection.
 * Editor host, workspace, and recent files remain external injectees.
 */
class ApplicationUi final {
public:
	ApplicationUi(ApplicationEditor &editor,
		DocumentWorkspace &workspace,
		RecentFiles &recent,
		std::string recentStatePath);

	ApplicationUi(const ApplicationUi &) = delete;
	ApplicationUi &operator=(const ApplicationUi &) = delete;

	[[nodiscard]] ApplicationEditor &Editor() noexcept { return *editor; }
	[[nodiscard]] const ApplicationEditor &Editor() const noexcept {
		return *editor;
	}
	[[nodiscard]] DocumentWorkspace &Workspace() noexcept { return *workspace; }
	[[nodiscard]] const DocumentWorkspace &Workspace() const noexcept {
		return *workspace;
	}
	[[nodiscard]] RecentFiles &Recent() noexcept { return *recent; }
	[[nodiscard]] const RecentFiles &Recent() const noexcept {
		return *recent;
	}
	[[nodiscard]] const std::string &RecentStatePath() const noexcept {
		return recentStatePath;
	}

	[[nodiscard]] MenuBarModel &MenuModel() noexcept { return menuModel; }
	[[nodiscard]] const MenuBarModel &MenuModel() const noexcept {
		return menuModel;
	}

	[[nodiscard]] TabStripModel &StripModel() noexcept { return stripModel; }
	[[nodiscard]] const TabStripModel &StripModel() const noexcept {
		return stripModel;
	}

	[[nodiscard]] ScrollBarInteraction &ScrollBars() noexcept {
		return scrollBarInteraction;
	}
	[[nodiscard]] const ScrollBarInteraction &ScrollBars() const noexcept {
		return scrollBarInteraction;
	}

	[[nodiscard]] int &CardFocus() noexcept { return cardFocus; }
	[[nodiscard]] int CardFocus() const noexcept { return cardFocus; }

	[[nodiscard]] BoundOverlay Overlay() const noexcept { return overlay; }
	void SetOverlay(BoundOverlay value) noexcept { overlay = value; }

	[[nodiscard]] bool &PointerOverChrome() noexcept {
		return pointerOverChrome;
	}
	[[nodiscard]] bool PointerOverChrome() const noexcept {
		return pointerOverChrome;
	}

	[[nodiscard]] bool &FileErrorPressHit() noexcept {
		return fileErrorPressHit;
	}
	[[nodiscard]] bool FileErrorPressHit() const noexcept {
		return fileErrorPressHit;
	}

	[[nodiscard]] std::optional<UnsavedCardHit> &PromptPressHit() noexcept {
		return promptPressHit;
	}
	[[nodiscard]] const std::optional<UnsavedCardHit> &PromptPressHit()
		const noexcept {
		return promptPressHit;
	}

	[[nodiscard]] std::deque<DocumentFileError> &FileErrors() noexcept {
		return fileErrors;
	}
	[[nodiscard]] const std::deque<DocumentFileError> &FileErrors()
		const noexcept {
		return fileErrors;
	}

	[[nodiscard]] DocumentId &LastActiveDocument() noexcept {
		return lastActiveDocument;
	}
	[[nodiscard]] DocumentId LastActiveDocument() const noexcept {
		return lastActiveDocument;
	}

private:
	ApplicationEditor *editor = nullptr;
	DocumentWorkspace *workspace = nullptr;
	RecentFiles *recent = nullptr;
	std::string recentStatePath;
	MenuBarModel menuModel;
	TabStripModel stripModel;
	ScrollBarInteraction scrollBarInteraction;
	int cardFocus = 0;
	BoundOverlay overlay = BoundOverlay::None;
	bool pointerOverChrome = false;
	bool fileErrorPressHit = false;
	std::optional<UnsavedCardHit> promptPressHit;
	std::deque<DocumentFileError> fileErrors;
	DocumentId lastActiveDocument = 0;
};

}

#endif
