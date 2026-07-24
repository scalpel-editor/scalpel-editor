// Single-document open, save, dirty-prompt, and close workflow.

#ifndef DOCUMENTWORKSPACE_H
#define DOCUMENTWORKSPACE_H

#include <string>
#include <string_view>
#include <vector>

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
};

/**
 * Owns the document path and unsaved-changes prompt transitions for the
 * single-buffer editor. Uses ApplicationEditor for text, dirty state, and
 * client invalidation. Returns shell requests; owns no Wayland or drawing.
 */
class DocumentWorkspace final {
public:
	explicit DocumentWorkspace(ApplicationEditor &editor);

	[[nodiscard]] const std::string &Path() const noexcept { return path; }
	[[nodiscard]] bool PromptActive() const noexcept { return prompt.Active(); }
	[[nodiscard]] UnsavedPending Pending() const noexcept {
		return prompt.Pending();
	}
	[[nodiscard]] bool AwaitingSaveAs() const noexcept {
		return prompt.AwaitingSaveAs();
	}
	[[nodiscard]] bool BufferModified() const noexcept;

	/** Ctrl+O or equivalent: open dialog, or dirty Open prompt. */
	void RequestOpen();
	/** Ctrl+S: write the known path, or Save As when untitled. */
	void RequestSave();
	/** Ctrl+Shift+S: always Save As. */
	void RequestSaveAs();
	/**
	 * Compositor or user close. Accepts immediately when clean; otherwise
	 * starts the Close prompt. No-op while a prompt is already active.
	 */
	void RequestClose();

	/** Save / Discard / Cancel from the card or its keyboard shortcuts. */
	void Choose(UnsavedChoice choice);

	/**
	 * Portal Open result. accepted is false for cancel, failure, or no path.
	 * Rejects replacement while the buffer is dirty.
	 */
	void HandleOpenResult(bool accepted, std::string_view openedPath);
	/**
	 * Portal Save result. When the prompt is awaiting Save As, success
	 * continues the pending close/open; cancel or write failure keeps it.
	 */
	void HandleSaveResult(bool accepted, std::string_view savedPath);

	[[nodiscard]] std::vector<DocumentShellRequest> TakeRequests();

private:
	void Queue(DocumentShellRequest request);
	void BeginPrompt(UnsavedPending pending);
	void ApplyOutcome(UnsavedOutcome outcome);
	[[nodiscard]] bool SaveToPath(const std::string &destination);

	ApplicationEditor &editor;
	std::string path;
	UnsavedChangesPrompt prompt;
	std::vector<DocumentShellRequest> requests;
};

}

#endif
