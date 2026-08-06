// Multi-document open, save, tab, dirty-prompt, and close workflow.
// Paired with ApplicationEditor (document bytes) and main (Wayland adapter).
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

/** One failed document read or write for the shell to present to the user. */
struct DocumentFileError {
	DocumentFileOperation operation = DocumentFileOperation::Open;
	std::string path;
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
	[[nodiscard]] bool BufferModified() const noexcept;

	/** Create an empty untitled tab and activate it. */
	void NewTab();
	/** Make id active when it is a retained tab. No-op while a prompt is active. */
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
	 * prompt is already active.
	 */
	void RequestClose();

	/** Save / Discard / Cancel from the card or its keyboard shortcuts. */
	void Choose(UnsavedChoice choice);

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
	 * tab or loaded a new one. No-op while a dirty-close prompt is active.
	 * Used by the Recent menu.
	 */
	[[nodiscard]] bool OpenPath(std::string_view path);
	/**
	 * Load path into the sole initial untitled document at process startup.
	 * Valid only while the workspace still has exactly that one initial
	 * untitled tab and no prompt. Reuses the existing document id, leaves the
	 * buffer at its save point, refreshes the tab label, and does not queue a
	 * recent-path outcome. Returns false for an empty path, a read failure, or
	 * when the workspace is no longer in its startup state; the initial tab
	 * remains coherent.
	 */
	[[nodiscard]] bool LoadStartupFile(std::string_view path);
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

	void Queue(DocumentShellRequest request);
	void QueueShowSaveAs();
	[[nodiscard]] DocumentDialogId NextDialogId();
	void BeginPrompt(UnsavedPending pending, DocumentId tabId);
	void ClearDirtyCloseState() noexcept;
	void ApplyOutcome(UnsavedOutcome outcome, UnsavedPending completedKind,
		DocumentId completedTabId);
	void AdvanceOrAcceptWindowClose();
	[[nodiscard]] std::optional<DocumentId> NextWindowCloseDirty() const;
	[[nodiscard]] bool SaveToPath(DocumentId tabId, const std::string &destination);
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
	[[nodiscard]] bool ApplyOpenPaths(const std::vector<std::string> &paths);
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
	std::vector<DocumentShellRequest> requests;
	std::vector<std::string> recentPaths;
	std::vector<DocumentFileError> fileErrors;
};

}

#endif
