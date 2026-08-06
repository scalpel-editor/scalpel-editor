// Process invocation contract for interactive launch and one editor path.

#ifndef APPLICATIONCOMMANDLINE_H
#define APPLICATIONCOMMANDLINE_H

#include <string>

namespace Scalpel {

/** How the process was invoked after parsing argv. */
enum class ApplicationInvocationKind {
	/** No path: open an untitled interactive workspace. */
	Interactive,
	/** Exactly one editor path to load as the sole initial document. */
	EditPath,
	/** Help was requested; print usage and exit successfully. */
	Help,
	/** Invalid arguments; print a diagnostic and exit with failure. */
	UsageError,
};

/**
 * Parsed process arguments. path is set only for EditPath. message is set only
 * for UsageError and is suitable for stderr after a "scalpel-editor: " prefix.
 */
struct ApplicationInvocation {
	ApplicationInvocationKind kind = ApplicationInvocationKind::Interactive;
	std::string path;
	std::string message;
};

/**
 * Parse full process arguments (argv[0] is the program name). Accepts no
 * arguments, one positional path, `--` then one path (including paths that
 * begin with `-`), and `-h` / `--help` alone. Does not construct platform
 * objects and does not accept ignored compatibility flags.
 */
[[nodiscard]] ApplicationInvocation ParseApplicationCommandLine(
	int argc, char *const *argv);

/** Concise multi-line usage text for stdout (help) or stderr (usage errors). */
[[nodiscard]] std::string ApplicationCommandLineUsage();

}

#endif
