#include "ApplicationSession.h"

#include <utility>

#include "DocumentWorkspace.h"

namespace Scalpel {

ApplicationSession::ApplicationSession(ApplicationInvocation invocationIn) :
	invocation(std::move(invocationIn)) {
}

ApplicationStartupResult ApplicationSession::Start(
	DocumentWorkspace &workspace) {
	switch (invocation.kind) {
	case ApplicationInvocationKind::Interactive:
		return ApplicationStartupResult::ReadyInteractive;
	case ApplicationInvocationKind::EditPath:
		if (!workspace.LoadStartupFiles(invocation.paths)) {
			return ApplicationStartupResult::FileLoadFailed;
		}
		return ApplicationStartupResult::ReadyEditPath;
	case ApplicationInvocationKind::Help:
	case ApplicationInvocationKind::Version:
	case ApplicationInvocationKind::UsageError:
		return ApplicationStartupResult::InvalidInvocation;
	}
	return ApplicationStartupResult::InvalidInvocation;
}

int ApplicationSession::ProcessStatus(
	ApplicationTerminationReason reason) const noexcept {
	switch (reason) {
	case ApplicationTerminationReason::AcceptedClose:
		return 0;
	case ApplicationTerminationReason::ForcedShutdown:
		// Pathname sessions fail so tools that wait on the editor (Git) abort
		// instead of treating a compositor loss as a successful edit.
		return PathEditorSession() ? 1 : 0;
	case ApplicationTerminationReason::StartupFailure:
	case ApplicationTerminationReason::FatalFailure:
		return 1;
	}
	return 1;
}

}
