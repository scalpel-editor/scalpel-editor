#include "ApplicationCommandLine.h"

#include <string_view>
#include <utility>

namespace Scalpel {
namespace {

[[nodiscard]] ApplicationInvocation UsageError(std::string message) {
	ApplicationInvocation invocation;
	invocation.kind = ApplicationInvocationKind::UsageError;
	invocation.message = std::move(message);
	return invocation;
}

[[nodiscard]] bool IsHelpOption(std::string_view argument) noexcept {
	return argument == "-h" || argument == "--help";
}

[[nodiscard]] bool LooksLikeOption(std::string_view argument) noexcept {
	return !argument.empty() && argument.front() == '-';
}

}

std::string ApplicationCommandLineUsage() {
	return
		"usage: scalpel-editor [path...]\n"
		"       scalpel-editor -- path...\n"
		"       scalpel-editor -h|--help\n";
}

ApplicationInvocation ParseApplicationCommandLine(int argc, char *const *argv) {
	const int argumentCount = argc > 0 ? argc - 1 : 0;
	char *const *arguments = argc > 0 ? argv + 1 : nullptr;

	if (argumentCount == 0) {
		return {};
	}

	const std::string_view first = arguments[0] != nullptr ? arguments[0] : "";

	if (IsHelpOption(first)) {
		if (argumentCount == 1) {
			ApplicationInvocation invocation;
			invocation.kind = ApplicationInvocationKind::Help;
			return invocation;
		}
		return UsageError("unexpected arguments after help");
	}

	std::vector<std::string> paths;
	bool sawDoubleDash = false;
	for (int i = 0; i < argumentCount; ++i) {
		const std::string_view argument =
			arguments[i] != nullptr ? arguments[i] : "";
		if (!sawDoubleDash && argument == "--") {
			sawDoubleDash = true;
			continue;
		}
		if (!sawDoubleDash && LooksLikeOption(argument)) {
			return UsageError("unknown option: " + std::string(argument));
		}
		if (argument.empty()) {
			return UsageError("path must not be empty");
		}
		paths.emplace_back(argument);
	}

	if (paths.empty()) {
		// The sole argument was the option terminator.
		return UsageError("missing path after --");
	}

	ApplicationInvocation invocation;
	invocation.kind = ApplicationInvocationKind::EditPath;
	invocation.paths = std::move(paths);
	return invocation;
}

}
