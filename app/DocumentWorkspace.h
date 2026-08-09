// Multi-document open, save, tab, dirty-prompt, and close workflow.
// Paired with ApplicationEditor (document bytes) and the Wayland runner.
// ApplicationAction dispatches File menu work through this object.

#ifndef DOCUMENTWORKSPACE_H
#define DOCUMENTWORKSPACE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "DocumentFile.h"
#include "DocumentId.h"
#include "UnsavedChangesPrompt.h"

namespace Scalpel {

class ApplicationEditor;

/** Side effects the shell performs (dialogs, window close, prompt chrome). */
enum class DocumentShellRequest {
	ShowOpen,
	ShowSaveAs,
	AcceptClose,
	/** Prompt just became active; reset card focus and press state. */
	PromptBegan,
	/** Tab set, order, labels, or active tab changed. */
	RefreshTabs,
};

enum class DocumentFileOperation {
	Open,
	Save,
};

/** Why a document file operation failed, for user-facing titles and diagnostics. */
enum class DocumentFileErrorReason {
	/** Open or save could not complete (missing path, I/O, permissions, …). */
	Failed,
	/** Open refused because the file exceeds the hard document size limit. */
	TooLarge,
};

/** Open / Cancel for the interactive large-file confirmation. */
enum class LargeFileChoice {
	Open,
	Cancel,
};

/**
 * Overwrite / Reload / Save As / Cancel for a save-time external file change.
 * Overwrite writes without an expected stamp. Reload rereads under the hard
 * limit into the captured tab. Save As queues a dialog for that tab. Cancel
 * leaves the dirty buffer and baseline stamp unchanged.
 */
enum class ExternalChangeChoice {
	Overwrite,
	Reload,
	SaveAs,
	Cancel,
};

/** One failed document read or write for the shell to present to the user. */
struct DocumentFileError {
	DocumentFileOperation operation = DocumentFileOperation::Open;
	std::string path;
	DocumentFileErrorReason reason = DocumentFileErrorReason::Failed;
};

/** Snapshot of one tab for the strip and tests. */
struct DocumentTabInfo {
	DocumentId id = 0;
	std::string path;
	/** Non-zero when the tab is untitled; stable for the tab's life. */
	int untitledNumber = 0;
	std::string label;
	bool dirty = false;
	bool active = false;
};

/**
 * Application-owned identity for one file-dialog intent. This is deliberately
 * distinct from the request ID returned by the platform dialog transport.
 */
struct DocumentDialogId {
	uint64_t value = 0;
};

[[nodiscard]] bool operator==(DocumentDialogId left,
	DocumentDialogId right) noexcept;
[[nodiscard]] bool operator!=(DocumentDialogId left,
	DocumentDialogId right) noexcept;

/**
 * Intent captured when a workspace dialog request crosses into the UI.
 * documentPath selects the initial folder and Save As suggestion.
 */
struct DocumentDialogIntent {
	DocumentDialogId id;
	std::string documentPath;
};

/**
 * Owns ordered tabs, the active tab, paths, untitled numbering, file-dialog
 * intents, and unsaved-changes prompt transitions. Uses ApplicationEditor for
 * document bytes and dirty state. Returns shell requests plus successful path
 * and file-error outcomes; owns no Wayland, persistence, or drawing.
 *
 * A running workspace always has at least one tab. Opening a path creates a
 * tab (or activates the existing lexically normalized path) instead of
 * replacing the buffer. File-dialog results are matched by an application
 * dialog identity; the platform host maps its transport IDs to that identity.
 *
 * Dirty CloseTab prompts remove one tab after Save or Discard. Dirty window
 * close walks dirty tabs in strip order; Save or Discard advances, and Cancel
 * aborts without removing tabs.
 *
 * Interactive Open and Recent first try the 64 MiB warning threshold. A file
 * larger than that threshold queues a large-file confirmation instead of a tab;
 * Open retries under the 256 MiB hard limit, and Cancel drops the path. The
 * large-file decision does not overlap a dirty-close prompt: either modal owns
 * tab, dialog, save, and close actions until it finishes.
 *
 * A Save to a tab's currently bound path compares the on-disk file stamp with
 * the tab's last successful read or write stamp. A mismatch queues an
 * external-change decision (Overwrite, Reload, Save As, Cancel) without writing.
 * Save As to a different path remains an unconditional replacement. Detection
 * runs only at the save boundary, never on a timer or focus change.
 */
class DocumentWorkspace final {
public:
	explicit DocumentWorkspace(ApplicationEditor &editor);

	[[nodiscard]] const std::string &Path() const noexcept;
	[[nodiscard]] DocumentId ActiveTab() const noexcept { return activeId; }
	[[nodiscard]] std::size_t TabCount() const noexcept { return tabs.size(); }
	[[nodiscard]] std::vector<DocumentTabInfo> Tabs() const;
	[[nodiscard]] bool PromptActive() const noexcept { return prompt.Active(); }
	[[nodiscard]] UnsavedPending Pending() const noexcept {
		return prompt.Pending();
	}
	/** Tab named by the active dirty-close card; zero when no prompt. */
	[[nodiscard]] DocumentId PromptTab() const noexcept {
		return prompt.TabId();
	}
	[[nodiscard]] bool AwaitingSaveAs() const noexcept {
		return prompt.AwaitingSaveAs();
	}
	/** True while Open / Cancel is required for an oversized interactive open. */
	[[nodiscard]] bool LargeFilePromptActive() const noexcept {
		return pendingLargeOpen.has_value();
	}
	/** Path awaiting large-file confirmation; empty when inactive. */
	[[nodiscard]] const std::string &LargeFilePromptPath() const noexcept;
	/**
	 * True while Overwrite / Reload / Save As / Cancel is required for a
	 * save-time external file change.
	 */
	[[nodiscard]] bool ExternalChangePromptActive() const noexcept {
		return pendingExternalChange.has_value();
	}
	/** Path named by the external-change card; empty when inactive. */
	[[nodiscard]] const std::string &ExternalChangePromptPath() const noexcept;
	/** Tab named by the external-change card; zero when inactive. */
	[[nodiscard]] DocumentId ExternalChangePromptTab() const noexcept;
	[[nodiscard]] bool BufferModified() const noexcept;

	/** Create an empty untitled tab and activate it. */
	void NewTab();
	/**
	 * Make id active when it is a retained tab. No-op while a dirty-close,
	 * large-file, or external-change decision owns the workspace.
	 */
	void ActivateTab(DocumentId id);
	/** Cycle the active tab by delta steps (wraps). */
	void CycleTab(int delta);
	/**
	 * Close the tab. Clean tabs close immediately; the last tab is replaced by
	 * a fresh untitled tab. A dirty tab is activated and receives the close
	 * prompt; Save or Discard then removes it.
	 */
	void CloseTab(DocumentId id);

	/** Ctrl+O or equivalent: always show the Open dialog (new tab on accept). */
	void RequestOpen();
	/** Ctrl+S: write the known path, or Save As when untitled. */
	void RequestSave();
	/** Ctrl+Shift+S: always Save As. */
	void RequestSaveAs();
	/**
	 * Compositor or user window close. Accepts immediately when every tab is
	 * clean; otherwise activates the first dirty tab in strip order and starts
	 * a CloseWindow prompt. Save or Discard advances through remaining dirty
	 * tabs; Cancel aborts the sequence without removing tabs. No-op while a
	 * dirty-close, large-file, or external-change decision is already active.
	 */
	void RequestClose();

	/** Save / Discard / Cancel from the card or its keyboard shortcuts. */
	void Choose(UnsavedChoice choice);
	/**
	 * Open or Cancel from the large-file confirmation. Open retries the path
	 * under the hard document size limit; Cancel drops it. Either choice then
	 * continues any remaining multi-path Open entries in order.
	 */
	void ChooseLargeFile(LargeFileChoice choice);
	/**
	 * Overwrite, Reload, Save As, or Cancel from the external-change card.
	 * Acts on the tab and dirty-close prompt generation captured when the
	 * conflict was found.
	 */
	void ChooseExternalChange(ExternalChangeChoice choice);

	/** Capture an Open intent and its current document path. */
	[[nodiscard]] DocumentDialogIntent BeginOpenDialog();
	/**
	 * Capture the tab and prompt generation that queued the next Save As, plus
	 * that tab's path. The returned identity remains valid across tab changes.
	 */
	[[nodiscard]] DocumentDialogIntent BeginSaveAsDialog();
	/**
	 * The platform could not start a captured dialog. Removes its intent and,
	 * for an awaiting dirty-close Save As, keeps the prompt active.
	 */
	void AbandonDialog(DocumentDialogId dialogId);

	/**
	 * Route a file-dialog result by application identity. Unknown identities
	 * and Save As results for closed tabs are ignored. accepted is false for
	 * cancel, failure, or an empty path list; Open uses every path, Save As
	 * uses the first.
	 */
	void HandleDialogResult(DocumentDialogId dialogId, bool accepted,
		const std::vector<std::string> &paths);

	/**
	 * Apply open paths without a dialog identity (tests and direct loads).
	 * accepted is false for cancel, failure, or no path. An existing lexically
	 * normalized path selects that tab; otherwise a new tab is created for each
	 * path.
	 */
	void HandleOpenResult(bool accepted, std::string_view openedPath);
	void HandleOpenResult(bool accepted, const std::vector<std::string> &paths);
	/**
	 * Open one path without a portal. Returns true when it selected an existing
	 * tab or loaded a new one. No-op while a dirty-close, large-file, or
	 * external-change decision is active. A path above the warning threshold
	 * queues confirmation and returns false until Open is chosen. Used by the
	 * Recent menu.
	 */
	[[nodiscard]] bool OpenPath(std::string_view path);
	/**
	 * Load an ordered startup path list into the pristine constructor workspace.
	 * Valid only while the workspace still has exactly one initial empty,
	 * unmodified untitled tab and no prompt. Normalizes paths, keeps the first
	 * occurrence of each distinct path as a tab in argument order, reuses the
	 * initial document for the first path, creates sibling documents for the
	 * rest, and activates the tab named by the last supplied path. Reads every
	 * distinct path before mutating tabs under the hard document size limit; any
	 * empty path, read failure, or oversized file leaves the initial workspace
	 * coherent and returns false. Leaves each buffer at its save point, queues
	 * one tab refresh, and does not record recent paths.
	 */
	[[nodiscard]] bool LoadStartupFiles(const std::vector<std::string> &paths);
	/**
	 * Apply a Save As path to the active tab without a dialog identity.
	 * When the prompt is awaiting Save As, success continues the pending close;
	 * cancel or write failure keeps it.
	 */
	void HandleSaveResult(bool accepted, std::string_view savedPath);
	/**
	 * Apply a Save As path to a specific tab. Used for delayed dialog results
	 * when another tab may be active.
	 */
	void HandleSaveResult(DocumentId tabId, bool accepted,
		std::string_view savedPath);

	[[nodiscard]] std::vector<DocumentShellRequest> TakeRequests();
	/** Successful Open and Save As paths, in completion order. */
	[[nodiscard]] std::vector<std::string> TakeRecentPaths();
	/** Failed reads and writes, in occurrence order. */
	[[nodiscard]] std::vector<DocumentFileError> TakeFileErrors();

private:
	struct Tab {
		DocumentId id = 0;
		std::string path;
		int untitledNumber = 0;
		/**
		 * Last successful read or write stamp for path. Empty when the tab is
		 * untitled or has never completed a stamped open or save.
		 */
		std::optional<DocumentFileStamp> stamp;
	};

	enum class DialogIntentKind {
		Open,
		SaveAs,
	};

	struct ActiveDialogIntent {
		DocumentDialogId id;
		DialogIntentKind kind = DialogIntentKind::Open;
		DocumentId tabId = 0;
		/**
		 * Non-zero when this Save As may advance one exact dirty-close prompt.
		 */
		uint64_t promptGeneration = 0;
	};

	struct PendingLargeOpen {
		std::string path;
		std::vector<std::string> remaining;
	};

	struct PendingExternalChange {
		DocumentId tabId = 0;
		std::string path;
		/**
		 * Non-zero when Overwrite, Reload, or a successful Save As may advance
		 * one exact dirty-close prompt generation.
		 */
		uint64_t promptGeneration = 0;
	};

	void Queue(DocumentShellRequest request);
	void QueueShowSaveAs();
	[[nodiscard]] DocumentDialogId NextDialogId();
	void BeginPrompt(UnsavedPending pending, DocumentId tabId);
	void ClearDirtyCloseState() noexcept;
	void ApplyOutcome(UnsavedOutcome outcome, UnsavedPending completedKind,
		DocumentId completedTabId);
	void AdvanceOrAcceptWindowClose();
	[[nodiscard]] std::optional<DocumentId> NextWindowCloseDirty() const;
	/**
	 * Write destination for tabId. When unconditional is false and destination
	 * is the tab's currently bound path with a remembered stamp, the write is
	 * guarded. A Changed result queues an external-change decision with
	 * conflictPromptGeneration and returns false without writing.
	 */
	[[nodiscard]] bool SaveToPath(DocumentId tabId, const std::string &destination,
		bool unconditional = false, uint64_t conflictPromptGeneration = 0);
	void BeginExternalChange(DocumentId tabId, std::string path,
		uint64_t promptGeneration);
	[[nodiscard]] bool ContinueDirtyCloseAfterExternalChange(
		const PendingExternalChange &pending, UnsavedChoice choice);
	[[nodiscard]] std::size_t IndexOf(DocumentId id) const;
	[[nodiscard]] std::optional<std::size_t> FindIndex(DocumentId id) const;
	[[nodiscard]] std::optional<std::size_t> FindIndexByPath(
		std::string_view path) const;
	[[nodiscard]] Tab &ActiveTabRecord();
	[[nodiscard]] const Tab &ActiveTabRecord() const;
	[[nodiscard]] const std::string &PathOf(DocumentId tabId) const noexcept;
	[[nodiscard]] std::string LabelFor(const Tab &tab) const;
	/** Append a new untitled tab record using an already-created document. */
	Tab &AppendUntitled(DocumentId id);
	/** Remove the tab at index; keeps at least one tab via a fresh untitled. */
	void RemoveTabAt(std::size_t index);
	void EnsureActiveMatchesEditor();
	/** True while dirty-close, large-file, or external-change owns decisions. */
	[[nodiscard]] bool DecisionActive() const noexcept;
	void BeginLargeFilePrompt(std::string path,
		std::vector<std::string> remaining);
	[[nodiscard]] bool ApplyOpenPaths(const std::vector<std::string> &paths);
	/**
	 * Process interactive open paths in order. May queue large-file confirmation
	 * and leave remaining paths for after Open or Cancel.
	 */
	[[nodiscard]] bool OpenPathList(const std::vector<std::string> &paths);
	/** Create and activate a tab for a successfully read path. */
	DocumentId LoadOpenedDocument(const std::string &pathString,
		std::string text, const DocumentFileStamp &stamp);
	void ApplySaveResult(DocumentId tabId, bool accepted,
		std::string_view savedPath, uint64_t promptGeneration);

	ApplicationEditor &editor;
	std::vector<Tab> tabs;
	DocumentId activeId = 0;
	int nextUntitledNumber = 1;
	/**
	 * Tab named by the active dirty-close card. Survives the prompt clear
	 * inside Choose so Save As and ApplyOutcome still know the target.
	 */
	DocumentId dirtyCloseTabId = 0;
	/**
	 * Tabs discarded (without save) during the current window-close walk so
	 * they are not prompted again while still modified.
	 */
	std::unordered_set<DocumentId> windowCloseResolved;
	/**
	 * Tab that should receive the next Save As dialog result. Set when
	 * ShowSaveAs is queued; consumed by BeginSaveAsDialog.
	 */
	std::optional<DocumentId> pendingSaveAsTab;
	/** Exact prompt that the next captured Save As may continue, or zero. */
	uint64_t pendingSaveAsPromptGeneration = 0;
	/** Monotonic identity of the currently visible dirty-close prompt. */
	uint64_t activePromptGeneration = 0;
	uint64_t lastPromptGeneration = 0;
	uint64_t lastDialogId = 0;
	std::vector<ActiveDialogIntent> activeDialogs;
	UnsavedChangesPrompt prompt;
	/** Interactive path waiting on large-file Open / Cancel, if any. */
	std::optional<PendingLargeOpen> pendingLargeOpen;
	/** Save conflict waiting on Overwrite / Reload / Save As / Cancel. */
	std::optional<PendingExternalChange> pendingExternalChange;
	std::vector<DocumentShellRequest> requests;
	std::vector<std::string> recentPaths;
	std::vector<DocumentFileError> fileErrors;
};

}

#endif
