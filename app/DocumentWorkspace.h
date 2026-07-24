// Multi-document open, save, tab, dirty-prompt, and close workflow.

#ifndef DOCUMENTWORKSPACE_H
#define DOCUMENTWORKSPACE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ApplicationEditor.h"
#include "UnsavedChangesPrompt.h"

namespace Scalpel {

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
 * Owns ordered tabs, the active tab, paths, untitled numbering, portal request
 * intents, and unsaved-changes prompt transitions. Uses ApplicationEditor for
 * document bytes and dirty state. Returns shell requests; owns no Wayland or
 * drawing.
 *
 * A running workspace always has at least one tab. Opening a path creates a
 * tab (or activates the existing exact path) instead of replacing the buffer.
 * Portal results are matched by the stable request ID returned from Show, not
 * by dialog order alone.
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
	 * Compositor or user window close. Accepts immediately when the active
	 * buffer is clean; otherwise starts the Close prompt. No-op while a prompt
	 * is already active. Multi-tab dirty window close is a later step.
	 */
	void RequestClose();

	/** Save / Discard / Cancel from the card or its keyboard shortcuts. */
	void Choose(UnsavedChoice choice);

	/**
	 * Record a portal Open request ID returned from Show so the later result
	 * is applied as multi-path open rather than ignored.
	 */
	void RegisterOpenRequest(uint64_t requestId);
	/**
	 * Record a portal Save As request ID for the tab that initiated the
	 * dialog (captured when ShowSaveAs was queued).
	 */
	void RegisterSaveAsRequest(uint64_t requestId);
	/**
	 * The Show for a queued Save As failed before a request ID existed. Clears
	 * the pending target and, when the prompt is awaiting Save As, keeps the
	 * card active.
	 */
	void NoteSaveAsDialogFailed();

	/**
	 * Route a portal file-dialog result by its stable request ID. Unknown IDs
	 * and Save As results for closed tabs are ignored. accepted is false for
	 * cancel, failure, or an empty path list; Open uses every path, Save As
	 * uses the first.
	 */
	void HandlePortalResult(uint64_t requestId, bool accepted,
		const std::vector<std::string> &paths);

	/**
	 * Apply open paths without a portal request ID (tests and direct loads).
	 * accepted is false for cancel, failure, or no path. An existing exact path
	 * selects that tab; otherwise a new tab is created for each path.
	 */
	void HandleOpenResult(bool accepted, std::string_view openedPath);
	void HandleOpenResult(bool accepted, const std::vector<std::string> &paths);
	/**
	 * Apply a Save As path to the active tab without a portal request ID.
	 * When the prompt is awaiting Save As, success continues the pending close;
	 * cancel or write failure keeps it.
	 */
	void HandleSaveResult(bool accepted, std::string_view savedPath);
	/**
	 * Apply a Save As path to a specific tab. Used for delayed portal results
	 * when another tab may be active.
	 */
	void HandleSaveResult(DocumentId tabId, bool accepted,
		std::string_view savedPath);

	[[nodiscard]] std::vector<DocumentShellRequest> TakeRequests();

private:
	struct Tab {
		DocumentId id = 0;
		std::string path;
		int untitledNumber = 0;
	};

	enum class PortalIntentKind {
		Open,
		SaveAs,
	};

	struct PortalIntent {
		PortalIntentKind kind = PortalIntentKind::Open;
		DocumentId tabId = 0;
		/** True when this Save As should advance the dirty-close prompt. */
		bool continuePrompt = false;
	};

	void Queue(DocumentShellRequest request);
	void QueueShowSaveAs();
	void BeginPrompt(UnsavedPending pending);
	void ApplyOutcome(UnsavedOutcome outcome);
	[[nodiscard]] bool SaveToPath(DocumentId tabId, const std::string &destination);
	[[nodiscard]] std::size_t IndexOf(DocumentId id) const;
	[[nodiscard]] std::optional<std::size_t> FindIndex(DocumentId id) const;
	[[nodiscard]] std::optional<std::size_t> FindIndexByPath(
		std::string_view path) const;
	[[nodiscard]] Tab &ActiveTabRecord();
	[[nodiscard]] const Tab &ActiveTabRecord() const;
	[[nodiscard]] std::string LabelFor(const Tab &tab) const;
	/** Append a new untitled tab record using an already-created document. */
	Tab &AppendUntitled(DocumentId id);
	/** Remove the tab at index; keeps at least one tab via a fresh untitled. */
	void RemoveTabAt(std::size_t index);
	void EnsureActiveMatchesEditor();
	void ApplyOpenPaths(const std::vector<std::string> &paths);
	void ApplySaveResult(DocumentId tabId, bool accepted,
		std::string_view savedPath, bool continuePrompt);

	ApplicationEditor &editor;
	std::vector<Tab> tabs;
	DocumentId activeId = 0;
	int nextUntitledNumber = 1;
	/** Set while the close prompt is for CloseTab rather than window close. */
	std::optional<DocumentId> closingTabId;
	/**
	 * Tab that should receive the next Save As dialog result. Set when
	 * ShowSaveAs is queued; consumed by RegisterSaveAsRequest or failure.
	 */
	std::optional<DocumentId> pendingSaveAsTab;
	/** Whether the next registered Save As continues the dirty-close prompt. */
	bool pendingSaveAsContinuesPrompt = false;
	std::unordered_map<uint64_t, PortalIntent> portalIntents;
	UnsavedChangesPrompt prompt;
	std::vector<DocumentShellRequest> requests;
};

}

#endif
