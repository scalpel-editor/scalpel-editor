// Production process entry: parse invocation, run the Wayland session, exit.

#include <exception>
#include <iostream>

#include "ApplicationCommandLine.h"
#include "ApplicationSession.h"
#include "WaylandApplicationRunner.h"

int main(int argc, char **argv) {
	const Scalpel::ApplicationInvocation invocation =
		Scalpel::ParseApplicationCommandLine(argc, argv);
	if (invocation.kind == Scalpel::ApplicationInvocationKind::Help) {
		std::cout << Scalpel::ApplicationCommandLineUsage();
		return 0;
	}
	if (invocation.kind == Scalpel::ApplicationInvocationKind::Version) {
		std::cout << "scalpel-editor " << Scalpel::ApplicationCommandLineVersion()
			<< '\n';
		return 0;
	}
	if (invocation.kind == Scalpel::ApplicationInvocationKind::UsageError) {
		std::cerr << "scalpel-editor: " << invocation.message << '\n';
		std::cerr << Scalpel::ApplicationCommandLineUsage();
		return 1;
	}

	Scalpel::ApplicationSession session(invocation);
	try {
		const Scalpel::ApplicationTerminationReason reason =
			Scalpel::RunWaylandApplication(session);
		return session.ProcessStatus(reason);
	} catch (const std::exception &error) {
		std::cerr << "scalpel-editor: " << error.what() << '\n';
		return session.ProcessStatus(
			Scalpel::ApplicationTerminationReason::FatalFailure);
	}
}
