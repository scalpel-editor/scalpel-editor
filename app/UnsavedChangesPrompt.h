// Modal dirty-buffer decision: Save / Discard / Cancel before close.

#ifndef UNSAVEDCHANGESPROMPT_H
#define UNSAVEDCHANGESPROMPT_H

namespace Scalpel {

enum class UnsavedPending {
	None,
	Close,
};

enum class UnsavedChoice {
	Save,
	Discard,
	Cancel,
};

enum class UnsavedOutcome {
	None,
	Dismissed,
	PerformClose,
	NeedSaveAs,
	SaveFailed,
};

/**
 * Pure state machine for the unsaved-changes prompt. Owns no drawing and no
 * file I/O; the host maps outcomes to save, portal Save As, or quit/close-tab.
 */
class UnsavedChangesPrompt final {
public:
	/** Start a prompt for pending. No-op (returns false) when already active. */
	bool TryBegin(UnsavedPending pending) noexcept;

	void Dismiss() noexcept;

	[[nodiscard]] bool Active() const noexcept {
		return pending != UnsavedPending::None;
	}
	[[nodiscard]] UnsavedPending Pending() const noexcept { return pending; }
	[[nodiscard]] bool AwaitingSaveAs() const noexcept { return awaitingSaveAs; }

	/**
	 * Apply a button or key choice. hasDocumentPath is true when the host can
	 * write the existing path without Save As.
	 */
	[[nodiscard]] UnsavedOutcome Choose(UnsavedChoice choice,
		bool hasDocumentPath) noexcept;

	/** Host reports a successful write or accepted Save As while awaiting. */
	[[nodiscard]] UnsavedOutcome NotifySaved() noexcept;

	/**
	 * Host reports write or Save As failure/cancel while awaiting. Stays
	 * active with the same pending so the user can choose again.
	 */
	void NotifySaveIncomplete() noexcept;

private:
	[[nodiscard]] UnsavedOutcome PerformPendingAndClear() noexcept;

	UnsavedPending pending = UnsavedPending::None;
	bool awaitingSaveAs = false;
};

}

#endif
