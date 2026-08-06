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
		"usage: scalpel-editor [path]\n"
		"       scalpel-editor -- path\n"
		"       scalpel-editor -h|--help\n";
}

ApplicationInvocation ParseApplicationCommandLine(int argc, char *const *argv) {
	const int argumentCount = argc > 0 ? argc - 1 : 0;
	char *const *arguments = argc > 0 ? argv + 1 : nullptr;

	if (argumentCount == 0) {
		return {};
	}

	const std::string_view first = arguments[0] != nullptr ? arguments[0] : "";

	if (argumentCount == 1) {
		if (IsHelpOption(first)) {
			ApplicationInvocation invocation;
			invocation.kind = ApplicationInvocationKind::Help;
			return invocation;
		}
		if (LooksLikeOption(first)) {
			if (first == "--") {
				return UsageError("missing path after --");
			}
			return UsageError("unknown option: " + std::string(first));
		}
		if (first.empty()) {
			return UsageError("path must not be empty");
		}
		ApplicationInvocation invocation;
		invocation.kind = ApplicationInvocationKind::EditPath;
		invocation.path = std::string(first);
		return invocation;
	}

	if (argumentCount == 2 && first == "--") {
		const std::string_view path =
			arguments[1] != nullptr ? arguments[1] : "";
		if (path.empty()) {
			return UsageError("path must not be empty");
		}
		ApplicationInvocation invocation;
		invocation.kind = ApplicationInvocationKind::EditPath;
		invocation.path = std::string(path);
		return invocation;
	}

	if (IsHelpOption(first)) {
		return UsageError("unexpected arguments after help");
	}
	if (first == "--") {
		return UsageError("expected exactly one path after --");
	}
	if (LooksLikeOption(first)) {
		return UsageError("unknown option: " + std::string(first));
	}
	return UsageError("expected at most one path");
}

}
