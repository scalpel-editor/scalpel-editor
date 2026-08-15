// Process invocation contract for interactive launch and pathname editing.

#ifndef APPLICATIONCOMMANDLINE_H
#define APPLICATIONCOMMANDLINE_H

#include <string>
#include <string_view>
#include <vector>

namespace Scalpel {

/** How the process was invoked after parsing argv. */
enum class ApplicationInvocationKind {
	/** No paths: open an untitled interactive workspace. */
	Interactive,
	/** One or more editor paths to load as the initial tab set. */
	EditPath,
	/** Help was requested; print usage and exit successfully. */
	Help,
	/** Version was requested; print the project version and exit successfully. */
	Version,
	/** Invalid arguments; print a diagnostic and exit with failure. */
	UsageError,
};

/**
 * Parsed process arguments. paths is set only for EditPath. message is set only
 * for UsageError and is suitable for stderr after a "scalpel-editor: " prefix.
 */
struct ApplicationInvocation {
	ApplicationInvocationKind kind = ApplicationInvocationKind::Interactive;
	std::vector<std::string> paths;
	std::string message;
};

/**
 * Parse full process arguments (argv[0] is the program name). Accepts no
 * arguments, one or more positional paths, `--` then one or more paths
 * (including paths that begin with `-`), and `-h` / `--help` or `-v` /
 * `--version` alone. Does not construct platform objects and does not accept
 * ignored compatibility flags.
 */
[[nodiscard]] ApplicationInvocation ParseApplicationCommandLine(
	int argc, char *const *argv);

/** Concise multi-line usage text for stdout (help) or stderr (usage errors). */
[[nodiscard]] std::string ApplicationCommandLineUsage();

/**
 * CMake project version without a program prefix or newline.
 * Empty only when the build did not define SCALPEL_EDITOR_VERSION.
 */
[[nodiscard]] std::string_view ApplicationCommandLineVersion() noexcept;

}

#endif
