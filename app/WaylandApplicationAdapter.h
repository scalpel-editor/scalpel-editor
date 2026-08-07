// Deterministic host-side mapping for the Wayland application runner.
//
// Portal request IDs, accept-close, and dialog startup failure stay free of
// WaylandWindow, EGL, and the compositor so tests can cover the production
// adapter without opening a display.

#ifndef WAYLANDAPPLICATIONADAPTER_H
#define WAYLANDAPPLICATIONADAPTER_H

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ApplicationSession.h"
#include "ApplicationUi.h"
#include "WaylandFileDialog.h"

namespace Scalpel {

/** Platform portal request ID to application dialog identity. */
using ActiveFileDialogs =
	std::unordered_map<uint64_t, DocumentDialogId>;

/**
 * Deliver portal results that still match a live mapping. Unknown or already
 * drained request IDs are ignored so a late response cannot invent a target.
 */
void ApplyFileDialogResults(const std::vector<FileDialogResult> &results,
	ApplicationUi &ui, ActiveFileDialogs &activeFileDialogs);

/**
 * Start a portal dialog for a ShowOpen or ShowSaveAs effect. Returns the
 * platform request ID on success, or nullopt when the dialog is unavailable.
 */
using StartPortalDialogFn = std::function<std::optional<uint64_t>(
	const ApplicationShellEffect &effect)>;

/**
 * Apply session shell effects that the host owns: open/save dialogs and
 * accept-close. Context-menu effects are left for the caller. When a portal
 * start fails, NotifyDialogFailed is called for that application dialog id.
 */
void ApplySessionShellEffects(
	const std::vector<ApplicationShellEffect> &effects,
	ApplicationUi &ui,
	ActiveFileDialogs &activeFileDialogs,
	bool &quitAccepted,
	const StartPortalDialogFn &startPortalDialog);

/**
 * Termination reason after the foreground loop ends. Accept-close wins when
 * the application accepted exit; otherwise the loop ended by force close.
 */
[[nodiscard]] ApplicationTerminationReason SessionLoopTerminationReason(
	bool quitAccepted) noexcept;

}

#endif
