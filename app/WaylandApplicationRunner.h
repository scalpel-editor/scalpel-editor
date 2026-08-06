// Compositor-backed construction and event loop for one process session.

#ifndef WAYLANDAPPLICATIONRUNNER_H
#define WAYLANDAPPLICATIONRUNNER_H

#include "ApplicationSession.h"

namespace Scalpel {

/**
 * Build the Wayland editor, apply session startup to the workspace, run the
 * foreground event loop, and return why the session ended. Does not map the
 * result to a process exit status; the caller uses ApplicationSession for that.
 * Returns StartupFailure when the session cannot load a pathname path list.
 */
[[nodiscard]] ApplicationTerminationReason RunWaylandApplication(
	ApplicationSession &session);

}

#endif
