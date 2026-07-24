#include "DocumentFile.h"

#include <cerrno>
#include <climits>
#include <fstream>
#include <iterator>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Scalpel {
namespace {

[[nodiscard]] std::string ResolveWriteDestination(const std::string &path,
	struct stat &existing, bool &haveExisting) {
	haveExisting = false;
	if (lstat(path.c_str(), &existing) != 0) {
		return path;
	}
	haveExisting = true;
	if (!S_ISLNK(existing.st_mode)) {
		return path;
	}
	// Follow an existing symlink so save updates the linked file and leaves
	// the link itself in place. A dangling link falls back to replacing the
	// path as named.
	char resolved[PATH_MAX];
	if (realpath(path.c_str(), resolved) == nullptr) {
		return path;
	}
	const std::string destination(resolved);
	haveExisting = stat(destination.c_str(), &existing) == 0;
	return destination;
}

[[nodiscard]] bool WriteAll(int fd, std::string_view text) {
	while (!text.empty()) {
		const ssize_t written = write(fd, text.data(), text.size());
		if (written < 0) {
			if (errno == EINTR) {
				continue;
			}
			return false;
		}
		if (written == 0) {
			return false;
		}
		text.remove_prefix(static_cast<std::size_t>(written));
	}
	return true;
}

[[nodiscard]] bool FinishTemporaryFile(int fd) {
	if (fsync(fd) != 0) {
		(void)close(fd);
		return false;
	}
	return close(fd) == 0;
}

}

std::optional<std::string> ReadDocumentFile(const std::string &path) {
	std::ifstream input(path, std::ios::binary);
	if (!input) {
		return std::nullopt;
	}
	std::string bytes{
		std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>()};
	if (input.bad()) {
		return std::nullopt;
	}
	return bytes;
}

bool WriteDocumentFile(const std::string &path, std::string_view text) {
	if (path.empty()) {
		return false;
	}

	struct stat existing {};
	bool haveExisting = false;
	const std::string destination =
		ResolveWriteDestination(path, existing, haveExisting);

	const std::string directory = DocumentDirectory(destination);
	const std::string tempDirectory = directory.empty() ? "." : directory;
	std::string pattern = tempDirectory + "/.scalpel-XXXXXX";
	std::vector<char> mutablePattern(pattern.begin(), pattern.end());
	mutablePattern.push_back('\0');

	const int fd = mkstemp(mutablePattern.data());
	if (fd < 0) {
		return false;
	}
	const std::string temporaryPath(mutablePattern.data());

	// New files keep the mode mkstemp chose (0600). Existing regular files keep
	// their user/group/other bits so a rewrite does not widen access.
	if (haveExisting && S_ISREG(existing.st_mode)) {
		if (fchmod(fd, existing.st_mode & 0777) != 0) {
			(void)close(fd);
			(void)unlink(temporaryPath.c_str());
			return false;
		}
	}

	if (!WriteAll(fd, text) || !FinishTemporaryFile(fd)) {
		(void)unlink(temporaryPath.c_str());
		return false;
	}

	if (rename(temporaryPath.c_str(), destination.c_str()) != 0) {
		(void)unlink(temporaryPath.c_str());
		return false;
	}

	// Best-effort directory durability for the rename; failure here still leaves
	// the new file reachable under destination.
	const int directoryFd =
		open(tempDirectory.c_str(), O_RDONLY | O_DIRECTORY);
	if (directoryFd >= 0) {
		(void)fsync(directoryFd);
		(void)close(directoryFd);
	}
	return true;
}

std::string DocumentDirectory(std::string_view path) {
	const std::size_t slash = path.find_last_of('/');
	if (slash == std::string_view::npos) {
		return {};
	}
	if (slash == 0) {
		return "/";
	}
	return std::string(path.substr(0, slash));
}

std::string DocumentBaseName(std::string_view path) {
	const std::size_t slash = path.find_last_of('/');
	if (slash == std::string_view::npos) {
		return std::string(path);
	}
	return std::string(path.substr(slash + 1));
}

}
