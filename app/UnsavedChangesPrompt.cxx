#include "UnsavedChangesPrompt.h"

namespace Scalpel {

bool UnsavedChangesPrompt::TryBegin(UnsavedPending next, uint64_t nextTabId) noexcept {
	if (pending != UnsavedPending::None) {
		return false;
	}
	if (next == UnsavedPending::None || nextTabId == 0) {
		return false;
	}
	pending = next;
	tabId = nextTabId;
	awaitingSaveAs = false;
	return true;
}

void UnsavedChangesPrompt::Dismiss() noexcept {
	pending = UnsavedPending::None;
	tabId = 0;
	awaitingSaveAs = false;
}

UnsavedOutcome UnsavedChangesPrompt::PerformPendingAndClear() noexcept {
	const UnsavedPending was = pending;
	pending = UnsavedPending::None;
	tabId = 0;
	awaitingSaveAs = false;
	switch (was) {
	case UnsavedPending::CloseTab:
	case UnsavedPending::CloseWindow:
		return UnsavedOutcome::PerformClose;
	case UnsavedPending::None:
		return UnsavedOutcome::None;
	}
	return UnsavedOutcome::None;
}

UnsavedOutcome UnsavedChangesPrompt::Choose(UnsavedChoice choice,
	bool hasDocumentPath) noexcept {
	if (pending == UnsavedPending::None) {
		return UnsavedOutcome::None;
	}
	switch (choice) {
	case UnsavedChoice::Cancel:
		Dismiss();
		return UnsavedOutcome::Dismissed;
	case UnsavedChoice::Discard:
		return PerformPendingAndClear();
	case UnsavedChoice::Save:
		if (hasDocumentPath) {
			// Host writes the known path, then treats this as success.
			return PerformPendingAndClear();
		}
		awaitingSaveAs = true;
		return UnsavedOutcome::NeedSaveAs;
	}
	return UnsavedOutcome::None;
}

UnsavedOutcome UnsavedChangesPrompt::NotifySaved() noexcept {
	if (pending == UnsavedPending::None) {
		return UnsavedOutcome::None;
	}
	// Save with a known path already cleared via Choose; Save As path lands here.
	return PerformPendingAndClear();
}

void UnsavedChangesPrompt::NotifySaveIncomplete() noexcept {
	if (pending == UnsavedPending::None) {
		return;
	}
	// Stay active with the same pending and tab; clear awaiting so the user
	// can choose again.
	awaitingSaveAs = false;
}

}
