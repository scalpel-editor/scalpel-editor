// Bounded most-recently-used document paths and their XDG state file.

#ifndef RECENTFILES_H
#define RECENTFILES_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Scalpel {

constexpr std::size_t MaximumRecentFiles = 10;

class RecentFiles final {
public:
	/**
	 * Promote path to the front after lexical normalization. Empty paths and
	 * paths containing NUL are rejected. Returns true when the list changed.
	 */
	bool Record(std::string_view path);
	/** Remove one normalized path. Returns true when it was present. */
	bool Remove(std::string_view path);
	/** Empty the list. Returns true when it was non-empty. */
	bool Clear() noexcept;

	[[nodiscard]] const std::vector<std::string> &Paths() const noexcept {
		return paths;
	}

private:
	std::vector<std::string> paths;
};

/**
 * State file location from explicit environment values. An empty result means
 * neither XDG_STATE_HOME nor HOME supplied a usable base directory.
 */
[[nodiscard]] std::string RecentFilesStatePath(
	std::string_view xdgStateHome, std::string_view home);
/** State file location using the current process environment. */
[[nodiscard]] std::string RecentFilesStatePath();

/**
 * Load a versioned NUL-delimited state file. Missing, oversized, or malformed
 * files yield an empty list.
 */
[[nodiscard]] RecentFiles LoadRecentFiles(const std::string &statePath);

/**
 * Atomically replace the state file, creating its parent directory when
 * needed. Returns false when no state path is available or writing fails.
 */
[[nodiscard]] bool SaveRecentFiles(const std::string &statePath,
	const RecentFiles &recent);

}

#endif
