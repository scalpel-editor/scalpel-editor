#include "WaylandApplicationAdapter.h"

namespace Scalpel {

void ApplyFileDialogResults(const std::vector<FileDialogResult> &results,
	ApplicationUi &ui, ActiveFileDialogs &activeFileDialogs) {
	for (const FileDialogResult &result : results) {
		const auto intent = activeFileDialogs.find(result.id);
		if (intent == activeFileDialogs.end()) {
			continue;
		}
		const DocumentDialogId dialogId = intent->second;
		activeFileDialogs.erase(intent);
		const bool accepted =
			result.status == FileDialogResultStatus::Accepted &&
			!result.paths.empty();
		ui.NotifyDialogResult(dialogId, accepted, result.paths);
	}
}

void ApplySessionShellEffects(
	const std::vector<ApplicationShellEffect> &effects,
	ApplicationUi &ui,
	ActiveFileDialogs &activeFileDialogs,
	bool &quitAccepted,
	const StartPortalDialogFn &startPortalDialog) {
	for (const ApplicationShellEffect &effect : effects) {
		switch (effect.kind) {
		case ApplicationShellEffectKind::ShowOpen:
		case ApplicationShellEffectKind::ShowSaveAs:
			if (const std::optional<uint64_t> requestId =
					startPortalDialog(effect)) {
				activeFileDialogs[*requestId] = effect.dialogId;
			} else {
				ui.NotifyDialogFailed(effect.dialogId);
			}
			break;
		case ApplicationShellEffectKind::AcceptClose:
			quitAccepted = true;
			break;
		case ApplicationShellEffectKind::ShowContextMenu:
		case ApplicationShellEffectKind::CloseContextMenu:
		case ApplicationShellEffectKind::InvalidateContextMenu:
			break;
		}
	}
}

ApplicationTerminationReason SessionLoopTerminationReason(
	bool quitAccepted) noexcept {
	return quitAccepted ?
		ApplicationTerminationReason::AcceptedClose :
		ApplicationTerminationReason::ForcedShutdown;
}

}
