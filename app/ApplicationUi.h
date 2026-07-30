// Application chrome and overlay selection state for the production editor.
// Owns menu, tab-strip, scrollbar interaction, modal-card, error-queue, hover,
// press, painters, and which overlay is bound. Holds references to
// ApplicationEditor, DocumentWorkspace, and RecentFiles. Pointer and keyboard
// routing go through HandlePointer and HandleKeyboard with explicit owners;
// focus loss is one transition that clears menu, scrollbar, and press state.
// Opening a menu or modal card cancels tentative IME. Permanent-chrome and
// active-overlay composition bind only through ApplicationUi. Workspace
// requests and outcomes are consumed here: application-side work (prompt
// begin, tab refresh, recent-file record and persist, file-error queue) is
// applied directly, while typed ApplicationShellEffect values carry only the
// portal-dialog and window-close work the host must perform. Frame-size
// response, dirty-tab sync, open-menu enablement refresh, post-shell
// interaction cleanup, and exit dismissal also live here so main stays a
// platform pump. ApplicationLayout is the one frame-size snapshot used for
// hit testing and painting within an event or paint pass.

#ifndef APPLICATIONUI_H
#define APPLICATIONUI_H

#include <deque>
#include <optional>
#include <string>
#include <vector>

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
 * Who owns keyboard handling for one event after priority resolution.
 * Order when deciding: file error, unsaved prompt, open menu (including open
 * accelerators while closed), application shortcuts and tab cycling, then
 * editor delivery.
 */
enum class ApplicationKeyboardOwner {
	FileError,
	UnsavedPrompt,
	Menu,
	ApplicationShortcut,
	Editor,
};

/**
 * Keyboard ownership result from ApplicationUi::HandleKeyboard.
 * Application actions, menu transitions, tab cycling, and editor key delivery
 * are applied inside ApplicationUi; the host only observes the owner for tests
 * and shell-request follow-up.
 */
struct ApplicationKeyboardResult {
	ApplicationKeyboardOwner owner = ApplicationKeyboardOwner::Editor;
};

/**
 * Host work produced while draining workspace requests and outcomes.
 */
enum class ApplicationShellEffectKind {
	ShowOpen,
	ShowSaveAs,
	AcceptClose,
};

/**
 * One typed host effect. Dialog effects carry an application-owned identity
 * plus the document path used to choose their initial folder and name. The
 * platform host maps its own request ID to dialogId; transport IDs never enter
 * ApplicationUi or DocumentWorkspace.
 */
struct ApplicationShellEffect {
	ApplicationShellEffectKind kind = ApplicationShellEffectKind::AcceptClose;
	DocumentDialogId dialogId;
	std::string documentPath;
};

/**
 * Owns permanent-chrome models, painters, scrollbar interaction, modal-card
 * focus, file-error queue, pointer hover and press state, and overlay
 * selection and composition. Editor host, workspace, and recent files remain
 * external injectees.
 */
class ApplicationUi final {
public:
	ApplicationUi(ApplicationEditor &editor,
		DocumentWorkspace &workspace,
		RecentFiles &recent,
		std::string recentStatePath);
	~ApplicationUi();

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
	 * Finalize model values that feed the layout, then build and retain the
	 * snapshot shared by all painters in one frame. Refreshes menu action
	 * enablement and clamps tab-strip scroll. Must precede ApplicationEditor
	 * frame painting; EndFrameLayout must follow it on both success and failure.
	 */
	void BeginFrameLayout();
	void EndFrameLayout() noexcept;
	/** The retained frame snapshot; throws when no frame layout is active. */
	[[nodiscard]] const ApplicationLayout &FrameLayout() const;

	/**
	 * Install permanent-chrome and overlay painter callbacks on the editor.
	 * Call once after construction. Overlay selection still runs through
	 * SynchronizeComposition each frame; this only installs the permanent
	 * chrome entry point and seeds the current overlay binding.
	 */
	void BindPainters();

	/**
	 * Select the active overlay by priority (file error, unsaved prompt, open
	 * menu, none), bind or clear the editor overlay painter, close a lower-
	 * priority open menu when a modal card wins, and invalidate the full frame
	 * when the bound overlay appears, changes, or disappears.
	 */
	BoundOverlay SynchronizeComposition();

	/**
	 * Paint permanent chrome (menu bar, tab strip, scrollbars) using the
	 * retained frame layout. Requires BeginFrameLayout.
	 */
	void PaintPermanentChrome(Scintilla::Internal::Surface &surface) const;

	/**
	 * Paint the currently bound overlay, if any, using the retained frame
	 * layout. Requires BeginFrameLayout. No-op when Overlay is None.
	 */
	void PaintActiveOverlay(Scintilla::Internal::Surface &surface) const;

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

	/**
	 * Current platform cursor choice. Retains the last pointer-routing result,
	 * but modal cards force the arrow even when they appear without a pointer
	 * event.
	 */
	[[nodiscard]] ApplicationPointerCursor CurrentPointerCursor()
		const noexcept;

	/**
	 * Route one keyboard event through modal cards, menu navigation, open
	 * accelerators, application shortcuts, tab cycling, and editor delivery.
	 * Modal owners and an open menu consume every key; shortcuts and editor
	 * typing apply inside this method.
	 */
	[[nodiscard]] ApplicationKeyboardResult HandleKeyboard(
		const KeyboardInput &input);

	/**
	 * Apply keyboard focus gain or loss. Loss is one transition: editor focus
	 * cancel (including tentative IME), close any open menu, cancel scrollbar
	 * interaction, and clear modal press state.
	 */
	void HandleFocus(bool focused);

	/**
	 * True while a file-error card, unsaved prompt, or open menu owns input.
	 * The platform host drops IME batches while this is true; protocol
	 * conversion stays in the adapter.
	 */
	[[nodiscard]] bool ChromeOwnsInput() const noexcept;

	/**
	 * Unsaved-prompt card became active. Closes the menu, cancels tentative
	 * IME and scrollbar interaction, and resets card focus and press state.
	 * Also applied automatically when TakeShellEffects consumes PromptBegan.
	 */
	void NotifyPromptBegan();

	/**
	 * Append file errors to the application-owned queue. When the queue was
	 * empty, activates the card in one transition: close the menu, cancel
	 * tentative IME and scrollbar interaction, clear lower-priority prompt
	 * press state, and invalidate the full frame. Also applied automatically
	 * when TakeShellEffects consumes workspace file-error outcomes.
	 */
	void AppendFileErrors(std::vector<DocumentFileError> errors);

	/**
	 * Rebuild the tab strip from the workspace. revealActive scrolls the
	 * active tab into view. Returns true when strip display state changed.
	 * Used for dirty-marker sync and resize without a workspace request;
	 * TakeShellEffects calls this when RefreshTabs is queued.
	 */
	bool SynchronizeTabs(bool revealActive = false);

	/**
	 * After editor pending work may have flipped dirty markers: rebuild the
	 * tab strip and invalidate top chrome when display state changed.
	 */
	void SynchronizeDirtyTabs();

	/**
	 * After shell effects, dialog results, or input that may change the active
	 * document or modal ownership: cancel scrollbar interaction when the
	 * active document changed, a file-error or unsaved card is up, or a menu
	 * is open.
	 */
	void SynchronizeInteraction();

	/**
	 * Respond to a logical frame size or scale change already applied to the
	 * editor. Cancels scrollbar interaction, refreshes tab layout, and
	 * invalidates chrome; full-frame when a modal card or open menu is visible.
	 */
	void HandleFrameSizeChange();

	/**
	 * While a menu is open, refresh action enablement (clipboard Paste and
	 * similar) and invalidate the frame when a row changes.
	 */
	void RefreshOpenMenuActionState();

	/**
	 * Close any open menu and cancel scrollbar interaction before the process
	 * exits (force-close or accept-close).
	 */
	void PrepareForExit();

	/**
	 * Consume DocumentWorkspace shell requests and outcomes. Applies prompt
	 * begin, tab refresh, recent-file record/persist, and file-error queueing
	 * here. Returns only portal-dialog and accept-close work for the host. A
	 * second call with no new workspace work returns empty.
	 */
	[[nodiscard]] std::vector<ApplicationShellEffect> TakeShellEffects();

	/**
	 * The host failed to start a dialog. Clears its captured intent and keeps
	 * an awaiting dirty-close prompt active when appropriate.
	 */
	void NotifyDialogFailed(DocumentDialogId dialogId);

	/**
	 * Route a file-dialog result by the application identity carried in its
	 * shell effect. Unknown identities and Save As results for closed tabs are
	 * ignored. accepted is false for cancel, failure, or an empty path list.
	 * Does not drain shell effects; call TakeShellEffects afterward.
	 */
	void NotifyDialogResult(DocumentDialogId dialogId, bool accepted,
		const std::vector<std::string> &paths);

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

	/** Overlay currently bound through SynchronizeComposition. */
	[[nodiscard]] BoundOverlay Overlay() const noexcept { return overlay; }

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
	[[nodiscard]] BoundOverlay DesiredOverlay() const noexcept;

	ApplicationEditor *editor = nullptr;
	DocumentWorkspace *workspace = nullptr;
	RecentFiles *recent = nullptr;
	std::string recentStatePath;
	MenuBarModel menuModel;
	TabStripModel stripModel;
	ScrollBarInteraction scrollBarInteraction;
	MenuBarPainter menuPainter;
	TabStripPainter stripPainter;
	UnsavedChangesCardPainter cardPainter;
	FileErrorCardPainter fileErrorPainter;
	int cardFocus = 0;
	BoundOverlay overlay = BoundOverlay::None;
	ApplicationPointerCursor pointerCursor = ApplicationPointerCursor::Editor;
	bool pointerOverChrome = false;
	bool fileErrorPressHit = false;
	std::optional<UnsavedCardHit> promptPressHit;
	std::deque<DocumentFileError> fileErrors;
	std::optional<ApplicationLayout> frameLayout;
	DocumentId lastActiveDocument = 0;
	bool ownsEditorPainters = false;
};

}

#endif
