// Modal dirty-buffer decision: Save / Discard / Cancel before close.

#ifndef UNSAVEDCHANGESPROMPT_H
#define UNSAVEDCHANGESPROMPT_H

#include <cstdint>

namespace Scalpel {

enum class UnsavedPending {
	None,
	/** Close one named tab after Save or Discard. */
	CloseTab,
	/** One step of a window-close walk over dirty tabs. */
	CloseWindow,
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
 * file I/O; the host maps outcomes to save, portal Save As, close-tab, or
 * window-close advance. Each active prompt names the tab the card is about.
 */
class UnsavedChangesPrompt final {
public:
	/**
	 * Start a prompt for pending on tabId. No-op (returns false) when already
	 * active, when pending is None, or when tabId is zero.
	 */
	bool TryBegin(UnsavedPending pending, uint64_t tabId) noexcept;

	void Dismiss() noexcept;

	[[nodiscard]] bool Active() const noexcept {
		return pending != UnsavedPending::None;
	}
	[[nodiscard]] UnsavedPending Pending() const noexcept { return pending; }
	/** Tab the card names while active; zero when inactive. */
	[[nodiscard]] uint64_t TabId() const noexcept { return tabId; }
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
	 * active with the same pending and tab so the user can choose again.
	 */
	void NotifySaveIncomplete() noexcept;

private:
	[[nodiscard]] UnsavedOutcome PerformPendingAndClear() noexcept;

	UnsavedPending pending = UnsavedPending::None;
	uint64_t tabId = 0;
	bool awaitingSaveAs = false;
};

}

#endif
