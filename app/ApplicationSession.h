// Process-lifetime startup and exit policy without Wayland transport.

#ifndef APPLICATIONSESSION_H
#define APPLICATIONSESSION_H

#include "ApplicationCommandLine.h"

namespace Scalpel {

class DocumentWorkspace;

/** Outcome of ApplicationSession::Start before the platform loop runs. */
enum class ApplicationStartupResult {
	/** No paths; the initial untitled workspace is ready. */
	ReadyInteractive,
	/** The supplied paths were loaded as the initial tab set. */
	ReadyEditPath,
	/** Path launch could not load every path; do not enter the event loop. */
	FileLoadFailed,
	/** Invocation was Help, Version, or UsageError and is not a session start. */
	InvalidInvocation,
};

/** Why the process is ending, chosen by the host or by startup policy. */
enum class ApplicationTerminationReason {
	/** User or application accepted window close. */
	AcceptedClose,
	/** Required globals were lost or the shell forced a shutdown. */
	ForcedShutdown,
	/** Startup could not prepare a workspace (including load failure). */
	StartupFailure,
	/** Uncaught failure after the session was ready. */
	FatalFailure,
};

/**
 * Application-owned process session policy. Accepts a parsed invocation,
 * initializes the workspace for pathname launch, and maps termination reasons
 * to process status. Independent of WaylandWindow, EGL, and the compositor.
 */
class ApplicationSession final {
public:
	explicit ApplicationSession(ApplicationInvocation invocation);

	/**
	 * Apply startup policy. Interactive launch leaves the workspace alone.
	 * EditPath calls DocumentWorkspace::LoadStartupFiles with the full path
	 * list. Help, Version, and UsageError return InvalidInvocation without
	 * touching the workspace.
	 */
	[[nodiscard]] ApplicationStartupResult Start(DocumentWorkspace &workspace);

	/**
	 * Process exit status for a termination reason under this session.
	 * Accepted close is always success. Startup and fatal failures are always
	 * failure. Forced shutdown is failure for a pathname editor session so Git
	 * aborts, and success for interactive launch to match historical behavior.
	 */
	[[nodiscard]] int ProcessStatus(
		ApplicationTerminationReason reason) const noexcept;

	[[nodiscard]] const ApplicationInvocation &Invocation() const noexcept {
		return invocation;
	}

	/** True when the invocation asked to edit one or more paths. */
	[[nodiscard]] bool PathEditorSession() const noexcept {
		return invocation.kind == ApplicationInvocationKind::EditPath;
	}

private:
	ApplicationInvocation invocation;
};

}

#endif
