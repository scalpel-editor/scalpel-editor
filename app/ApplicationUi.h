// Application chrome and overlay selection state for the production editor.
// Owns menu, tab-strip, scrollbar interaction, modal-card, error-queue, hover,
// press, and which overlay is bound. Holds references to ApplicationEditor,
// DocumentWorkspace, and RecentFiles. Pointer routing goes through
// HandlePointer with an explicit owner; keyboard and shell-effect routing still
// live in the host for now. ApplicationLayout is the one frame-size snapshot
// used for hit testing and painting within an event or paint pass.

#ifndef APPLICATIONUI_H
#define APPLICATIONUI_H

#include <deque>
#include <optional>
#include <string>

#include "ApplicationInput.h"
#include "DocumentId.h"
#include "DocumentWorkspace.h"
#include "FileErrorCard.h"
#include "Geometry.h"
#include "MenuBar.h"
#include "ScrollBar.h"
#include "ScrollMetrics.h"
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
 * One frame's permanent chrome, client, and card rectangles.
 * Built from frame size, chrome models, and editor scroll metrics plus the
 * editor-owned client rectangle. Hit testing and painting during the same
 * event or paint pass must share this snapshot rather than re-laying out
 * independently. Card layouts depend only on frame size; which card is shown
 * remains overlay selection policy.
 */
struct ApplicationLayout {
	int frameWidth = 0;
	int frameHeight = 0;
	int topChromeInset = 0;
	MenuBarLayout menu;
	TabStripLayout tabs;
	ScrollBarLayout scrollBars;
	/** Scintilla client from ApplicationEditor; not recomputed here. */
	Scintilla::Internal::PRectangle client;
	UnsavedChangesCardLayout unsavedCard;
	FileErrorCardLayout fileErrorCard;
};

/**
 * Build the frame layout from explicit size, models, and editor metrics.
 * topChromeInset is MenuBarHeight + TabStripHeight in production. client must
 * be ApplicationEditor::EditorClientRectangle() (or a test double) so client
 * geometry stays the editor's responsibility. scroll metrics come from
 * ApplicationEditor::Scrollbars(); visibility and ranges are not re-derived.
 */
[[nodiscard]] ApplicationLayout BuildApplicationLayout(int frameWidth,
	int frameHeight, int topChromeInset, const MenuBarModel &menuModel,
	const TabStripModel &stripModel, const ScrollMetrics &scrollMetrics,
	Scintilla::Internal::PRectangle client) noexcept;

/**
 * Who owns the pointer for one event after priority resolution.
 * Order when deciding: file error, unsaved prompt, active scrollbar drag,
 * editor selection capture, open menu, permanent chrome (including scrollbar
 * hits), then editor.
 */
enum class ApplicationPointerOwner {
	FileError,
	UnsavedPrompt,
	Menu,
	ScrollBarDrag,
	EditorCapture,
	PermanentChrome,
	Editor,
};

/** Cursor policy after a pointer event; Editor defers to WindowState().cursor. */
enum class ApplicationPointerCursor {
	Arrow,
	Editor,
};

/**
 * Pointer ownership and host delivery result from ApplicationUi::HandlePointer.
 * consumed stops delivery to ApplicationEditor. cursor is the only
 * pointer-specific choice returned to the platform host; application actions,
 * model transitions, and editor invalidation are applied inside ApplicationUi.
 */
struct ApplicationPointerResult {
	bool consumed = false;
	ApplicationPointerOwner owner = ApplicationPointerOwner::Editor;
	ApplicationPointerCursor cursor = ApplicationPointerCursor::Editor;
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

	/**
	 * Layout snapshot from the current editor frame, chrome models, and
	 * editor scroll metrics and client rectangle.
	 */
	[[nodiscard]] ApplicationLayout Layout() const;
	/**
	 * Build and retain the snapshot shared by all painters in one frame.
	 * BeginFrameLayout must precede ApplicationEditor frame painting, and
	 * EndFrameLayout must follow it on both success and failure.
	 */
	void BeginFrameLayout();
	void EndFrameLayout() noexcept;
	/** The retained frame snapshot; throws when no frame layout is active. */
	[[nodiscard]] const ApplicationLayout &FrameLayout() const;

	/**
	 * Route one pointer event through modal cards, menu, permanent chrome,
	 * and scrollbar interaction. Builds one layout snapshot for all hit tests.
	 * Applies model transitions, editor invalidation, scroll requests, menu
	 * item activation, and tab operations. When consumed is false the host
	 * must deliver the same event to ApplicationEditor (selection capture and
	 * surface leave still need editor delivery).
	 */
	[[nodiscard]] ApplicationPointerResult HandlePointer(
		const PointerInput &input);

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
	std::optional<ApplicationLayout> frameLayout;
	DocumentId lastActiveDocument = 0;
};

}

#endif
