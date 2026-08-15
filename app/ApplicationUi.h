// Application chrome and overlay selection state for the production editor.
// Owns menu, context menu, tab-strip, scrollbar interaction, modal-card,
// error-queue, hover, press, painters, and which overlay is bound. Holds
// references to ApplicationEditor, DocumentWorkspace, and RecentFiles. Pointer
// and keyboard routing go through HandlePointer and HandleKeyboard with
// explicit owners; focus loss is one transition that clears menu, context
// menu, scrollbar, and press state. Opening a menu, context menu, or modal
// card cancels tentative IME. Permanent-chrome and active-overlay composition
// bind only through ApplicationUi; the context menu is an xdg_popup owned via
// shell effects rather than an in-window overlay. Workspace requests and
// outcomes are consumed here: application-side work (prompt begin, tab
// refresh, recent-file record and persist, file-error queue) is applied
// directly, while typed ApplicationShellEffect values carry portal-dialog,
// window-close, and context-popup work the host must perform. Frame-size
// response, dirty-tab sync, open-menu enablement refresh, post-shell
// interaction cleanup, and exit dismissal also live here so main stays a
// platform pump. ApplicationLayout is the one frame-size snapshot used for
// hit testing and painting within an event or paint pass.

#ifndef APPLICATIONUI_H
#define APPLICATIONUI_H

#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ApplicationClipboard.h"
#include "ApplicationEditor.h"
#include "ApplicationInput.h"
#include "ApplicationTextInput.h"
#include "ContextMenu.h"
#include "DocumentId.h"
#include "DocumentWorkspace.h"
#include "ExternalChangeCard.h"
#include "FileErrorCard.h"
#include "FindBar.h"
#include "Geometry.h"
#include "LargeFileCard.h"
#include "MenuBar.h"
#include "ScrollBar.h"
#include "ScrollMetrics.h"
#include "TabStrip.h"
#include "UnsavedChangesCard.h"

namespace Scalpel {

class RecentFiles;

/** Which post-paint overlay painter is currently bound to the editor host. */
enum class BoundOverlay {
	None,
	Menu,
	UnsavedChanges,
	LargeFile,
	ExternalChange,
	FileError,
};

/**
 * One frame's permanent chrome, client, and card rectangles.
 * Built from frame size, chrome models, and editor scroll metrics plus the
 * editor-owned client rectangle. Hit testing and painting during the same
 * event or paint pass must share this snapshot rather than re-laying out
 * independently. Card layouts depend only on frame size; which card is shown
 * remains overlay selection policy. find is empty when the bar is hidden.
 */
struct ApplicationLayout {
	int frameWidth = 0;
	int frameHeight = 0;
	int topChromeInset = 0;
	MenuBarLayout menu;
	TabStripLayout tabs;
	FindBarLayout find;
	ScrollBarLayout scrollBars;
	/** Scintilla client from ApplicationEditor; not recomputed here. */
	Scintilla::Internal::PRectangle client;
	UnsavedChangesCardLayout unsavedCard;
	LargeFileCardLayout largeFileCard;
	ExternalChangeCardLayout externalChangeCard;
	FileErrorCardLayout fileErrorCard;
};

/**
 * Build the frame layout from explicit size, models, and editor metrics.
 * topChromeInset is MenuBarHeight + TabStripHeight, plus FindBarHeight when
 * findVisible. client must be ApplicationEditor::EditorClientRectangle() (or a
 * test double) so client geometry stays the editor's responsibility. scroll
 * metrics come from ApplicationEditor::Scrollbars(); visibility and ranges are
 * not re-derived.
 */
[[nodiscard]] ApplicationLayout BuildApplicationLayout(int frameWidth,
	int frameHeight, int topChromeInset, const MenuBarModel &menuModel,
	const TabStripModel &stripModel, const ScrollMetrics &scrollMetrics,
	Scintilla::Internal::PRectangle client, bool findVisible = false) noexcept;

/**
 * Who owns the pointer for one event after priority resolution.
 * Order when deciding: file error, external-change confirmation, large-file
 * confirmation, unsaved prompt, active scrollbar drag, editor selection
 * capture, open menu bar, open context menu, permanent chrome (including
 * scrollbar hits), then editor.
 */
enum class ApplicationPointerOwner {
	FileError,
	ExternalChange,
	LargeFile,
	UnsavedPrompt,
	Menu,
	ContextMenu,
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
 * Order when deciding: file error, external-change confirmation, large-file
 * confirmation, unsaved prompt, open context menu, Shift+F10 context-menu open,
 * open menu bar (including open accelerators while closed), global Find
 * (Ctrl+F), a focused find field for editing and field-local clipboard keys,
 * other application shortcuts and tab cycling, then editor delivery. Bare
 * F10 / Menu still opens the menu bar.
 */
enum class ApplicationKeyboardOwner {
	FileError,
	ExternalChange,
	LargeFile,
	UnsavedPrompt,
	ContextMenu,
	Menu,
	ApplicationShortcut,
	FindBar,
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
 * Host work produced while draining workspace requests and outcomes, plus
 * context-menu popup lifecycle for the Wayland shell.
 */
enum class ApplicationShellEffectKind {
	ShowOpen,
	ShowSaveAs,
	AcceptClose,
	/** Create and grab an xdg_popup at anchorX/Y with serial. */
	ShowContextMenu,
	/** Destroy the context-menu popup if the host still owns one. */
	CloseContextMenu,
	/** Repaint the open popup (enablement changed); do not recreate it. */
	InvalidateContextMenu,
};

/**
 * One typed host effect. Dialog effects carry an application-owned identity
 * plus the document path used to choose their initial folder and name. The
 * platform host maps its own request ID to dialogId; transport IDs never enter
 * ApplicationUi or DocumentWorkspace. Context-menu effects use anchorX/Y in
 * parent (toplevel) logical coordinates and serial for xdg_popup.grab.
 */
struct ApplicationShellEffect {
	ApplicationShellEffectKind kind = ApplicationShellEffectKind::AcceptClose;
	DocumentDialogId dialogId;
	std::string documentPath;
	double anchorX = 0;
	double anchorY = 0;
	uint32_t serial = 0;
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
	 * Select the active overlay by priority (file error, large-file
	 * confirmation, unsaved prompt, open menu, none), bind or clear the editor
	 * overlay painter, close a lower-priority open menu when a modal card wins,
	 * and invalidate the full frame when the bound overlay appears, changes, or
	 * disappears.
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
	 * surface leave still need editor delivery). Any document or modal change
	 * leaves scrollbar interaction consistent before this method returns.
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
	 * accelerators, the global Find action, a focused find field, application
	 * shortcuts, tab cycling, and editor delivery. Modal owners and an open menu
	 * consume every key; shortcuts and editor typing apply inside this method.
	 * Any document or modal change leaves scrollbar interaction consistent
	 * before this method returns.
	 */
	[[nodiscard]] ApplicationKeyboardResult HandleKeyboard(
		const KeyboardInput &input);

	/**
	 * Apply keyboard focus gain or loss. Loss is one transition: editor focus
	 * cancel (including tentative IME), close any open menu, cancel scrollbar
	 * interaction, clear modal press state, and blur the find field without
	 * closing the bar or discarding its query.
	 */
	void HandleFocus(bool focused);

	/**
	 * True while a file-error, external-change, large-file, or unsaved card,
	 * open menu bar, or open context menu owns input. The platform host drops
	 * IME batches while this is true; protocol conversion stays in the adapter.
	 */
	[[nodiscard]] bool ChromeOwnsInput() const noexcept;

	/**
	 * True while the application context-menu model is open (shell may still
	 * be creating or painting the xdg_popup).
	 */
	[[nodiscard]] bool ContextMenuOpen() const noexcept {
		return contextMenuModel.open;
	}

	/**
	 * Compositor destroyed the context popup (xdg_popup.popup_done) or the
	 * host failed to create it. Closes the model without queuing another
	 * CloseContextMenu effect.
	 */
	void NotifyContextPopupDone();

	/**
	 * Open the context menu at a parent-relative anchor after any selection
	 * preparation the caller already applied. Cancels IME, blurs find, closes
	 * the menu bar, and queues ShowContextMenu for the host.
	 */
	void OpenApplicationContextMenu(double anchorX, double anchorY,
		uint32_t serial);
	/**
	 * Close the context menu model. When the shell was asked to show a popup
	 * and fromShellDone is false, queues CloseContextMenu.
	 */
	void DismissApplicationContextMenu(bool fromShellDone = false);

	/**
	 * Open the find bar (or refocus it), seed an empty query from a single-line
	 * editor selection when valid, capture the incremental origin, and expand
	 * the top chrome inset. Shared by Ctrl+F and Edit > Find.
	 */
	void OpenFindBar();

	/** Hide the find bar, restore the base inset, and leave the last match selected. */
	void CloseFindBar();

	[[nodiscard]] bool FindBarVisible() const noexcept { return findBarVisible; }
	[[nodiscard]] bool FindBarFocused() const noexcept {
		return findBarVisible && findBarModel.focused;
	}
	[[nodiscard]] FindBarModel &FindModel() noexcept { return findBarModel; }
	[[nodiscard]] const FindBarModel &FindModel() const noexcept {
		return findBarModel;
	}

	/**
	 * Deliver a platform text-input batch. Modal or open-menu ownership drops
	 * batches (and cancels find preedit). A focused find field receives the
	 * batch; otherwise the editor does.
	 */
	void HandleTextInputBatch(const ApplicationTextInputBatch &batch);
	/**
	 * Drain dirty text-input client state for the current owner (find field or
	 * editor). Returns nullopt when neither has a pending update.
	 */
	[[nodiscard]] std::optional<ApplicationTextInputState> TakeTextInputState();

	/**
	 * Mirror platform clipboard offer availability into the editor and find field.
	 */
	void SetClipboardPasteAvailable(bool available) noexcept;
	/**
	 * Drain shell-facing clipboard requests from the editor and find field.
	 * Local owner request IDs are remapped to unique shell IDs.
	 */
	[[nodiscard]] std::vector<ApplicationClipboardRequest> TakeClipboardRequests();
	/**
	 * Deliver a clipboard result by shell-facing request ID. Editor pastes map
	 * back to the editor local ID; find pastes apply only while the field is
	 * still focused for the same request generation.
	 */
	void HandleClipboardResult(uint64_t shellId,
		ApplicationClipboardOperation operation,
		ApplicationClipboardStatus status, std::string text = {});
	/** Drain reported clipboard statuses for host logging. */
	[[nodiscard]] std::vector<ApplicationClipboardResult> TakeClipboardResults();

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
	 * Active tab label, including the dirty " *" suffix. The Wayland runner
	 * copies this to xdg_toplevel.set_title.
	 */
	[[nodiscard]] std::string CurrentWindowTitle() const;

	/**
	 * Route a platform window-close request into workspace close policy.
	 * Resulting portal-dialog or accept-close work is returned by the next
	 * TakeShellEffects call.
	 */
	void RequestClose();

	/**
	 * Respond to a logical frame size or scale change already applied to the
	 * editor. Cancels scrollbar interaction, clears armed menu/find/modal
	 * press origins, dismisses the context menu, refreshes tab layout, and
	 * invalidates chrome; full-frame when a modal card or open menu is visible.
	 */
	void HandleFrameSizeChange();

	/**
	 * While a menu bar or context menu is open, refresh action enablement
	 * (clipboard Paste and similar). The menu bar invalidates the toplevel
	 * frame; the context menu queues InvalidateContextMenu for the popup.
	 */
	void RefreshOpenMenuActionState();

	/**
	 * Close any open menu bar and context menu, and cancel scrollbar
	 * interaction before the process exits (force-close or accept-close).
	 */
	void PrepareForExit();

	/**
	 * Consume DocumentWorkspace shell requests and outcomes, plus queued
	 * context-menu popup effects. Applies prompt begin, tab refresh,
	 * recent-file record/persist, and file-error queueing here. Returns
	 * portal-dialog, accept-close, and context-popup work for the host. A
	 * second call with no new work returns empty.
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
	 * Leaves interaction state consistent but does not drain shell effects;
	 * call TakeShellEffects afterward.
	 */
	void NotifyDialogResult(DocumentDialogId dialogId, bool accepted,
		const std::vector<std::string> &paths);

	[[nodiscard]] MenuBarModel &MenuModel() noexcept { return menuModel; }
	[[nodiscard]] const MenuBarModel &MenuModel() const noexcept {
		return menuModel;
	}

	[[nodiscard]] ContextMenuModel &ContextModel() noexcept {
		return contextMenuModel;
	}
	[[nodiscard]] const ContextMenuModel &ContextModel() const noexcept {
		return contextMenuModel;
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

	[[nodiscard]] std::optional<LargeFileCardHit> &LargeFilePressHit() noexcept {
		return largeFilePressHit;
	}
	[[nodiscard]] const std::optional<LargeFileCardHit> &LargeFilePressHit()
		const noexcept {
		return largeFilePressHit;
	}

	[[nodiscard]] std::optional<ExternalChangeCardHit> &ExternalChangePressHit()
		noexcept {
		return externalChangePressHit;
	}
	[[nodiscard]] const std::optional<ExternalChangeCardHit> &
	ExternalChangePressHit() const noexcept {
		return externalChangePressHit;
	}

	[[nodiscard]] const std::deque<DocumentFileError> &FileErrors()
		const noexcept {
		return fileErrors;
	}

private:
	[[nodiscard]] BoundOverlay DesiredOverlay() const noexcept;
	void SynchronizeInteraction();
	[[nodiscard]] int BaseTopChromeInset() const noexcept;
	[[nodiscard]] int CurrentTopChromeInset() const noexcept;
	void ApplyTopChromeInset();
	void CaptureFindOrigin();
	void ApplyFindOutcome(ApplicationFindOutcome outcome);
	void ApplyFindBarRequests(const std::vector<FindBarRequest> &requests);
	void RunIncrementalFind();
	void RunFindForward();
	void RunFindBackward();
	void BlurFindField();
	/** Activate a matched action; Find is UI-local, everything else dispatches. */
	void ActivateAction(ApplicationAction action);
	void QueueShellEffect(ApplicationShellEffect effect);

	ApplicationEditor *editor = nullptr;
	DocumentWorkspace *workspace = nullptr;
	RecentFiles *recent = nullptr;
	std::string recentStatePath;
	MenuBarModel menuModel;
	ContextMenuModel contextMenuModel;
	/** True after ShowContextMenu until Close or NotifyContextPopupDone. */
	bool contextMenuShellOpen = false;
	std::vector<ApplicationShellEffect> pendingShellEffects;
	TabStripModel stripModel;
	FindBarModel findBarModel;
	bool findBarVisible = false;
	struct FindOrigin {
		DocumentId document = 0;
		Scintilla::Position position = 0;
	};
	std::optional<FindOrigin> findOrigin;
	enum class ClipboardRequestOwner {
		Editor,
		FindBar,
	};
	struct ShellClipboardMapping {
		ClipboardRequestOwner owner = ClipboardRequestOwner::Editor;
		uint64_t localId = 0;
		uint64_t findGeneration = 0;
		ApplicationClipboardOperation operation =
			ApplicationClipboardOperation::Copy;
	};
	uint64_t nextShellClipboardId = 1;
	std::vector<ApplicationClipboardRequest> shellClipboardRequests;
	std::vector<ApplicationClipboardResult> shellClipboardResults;
	std::unordered_map<uint64_t, ShellClipboardMapping> shellClipboardMap;
	ScrollBarInteraction scrollBarInteraction;
	MenuBarPainter menuPainter;
	TabStripPainter stripPainter;
	FindBarPainter findBarPainter;
	UnsavedChangesCardPainter cardPainter;
	LargeFileCardPainter largeFilePainter;
	ExternalChangeCardPainter externalChangePainter;
	FileErrorCardPainter fileErrorPainter;
	int cardFocus = 0;
	BoundOverlay overlay = BoundOverlay::None;
	ApplicationPointerCursor pointerCursor = ApplicationPointerCursor::Editor;
	bool pointerOverChrome = false;
	bool fileErrorPressHit = false;
	std::optional<UnsavedCardHit> promptPressHit;
	std::optional<LargeFileCardHit> largeFilePressHit;
	std::optional<ExternalChangeCardHit> externalChangePressHit;
	std::deque<DocumentFileError> fileErrors;
	std::optional<ApplicationLayout> frameLayout;
	DocumentId lastActiveDocument = 0;
	bool ownsEditorPainters = false;
};

}

#endif
