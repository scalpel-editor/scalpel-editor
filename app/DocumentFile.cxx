#include "DocumentFile.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Scalpel {
namespace {

[[nodiscard]] DocumentFileStamp StampFromStat(const struct stat &st) noexcept {
	DocumentFileStamp stamp;
	stamp.device = static_cast<std::uint64_t>(st.st_dev);
	stamp.inode = static_cast<std::uint64_t>(st.st_ino);
	if (st.st_size > 0) {
		stamp.sizeBytes = static_cast<std::uint64_t>(st.st_size);
	}
	stamp.modificationSeconds = static_cast<std::int64_t>(st.st_mtim.tv_sec);
	stamp.modificationNanoseconds =
		static_cast<std::int64_t>(st.st_mtim.tv_nsec);
	return stamp;
}

[[nodiscard]] bool DestinationMatches(const std::string &destination,
	const DocumentFileStamp &expected) {
	struct stat st {};
	if (stat(destination.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
		return false;
	}
	return StampFromStat(st) == expected;
}

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

[[nodiscard]] DocumentFileReadResult MakeReadFailure() {
	return {DocumentFileReadStatus::ReadFailure, {}, {}};
}

[[nodiscard]] DocumentFileReadResult MakeTooLarge() {
	return {DocumentFileReadStatus::TooLarge, {}, {}};
}

[[nodiscard]] DocumentFileWriteResult MakeWriteFailure() {
	return {DocumentFileWriteStatus::WriteFailure, {}};
}

[[nodiscard]] DocumentFileWriteResult MakeWriteChanged() {
	return {DocumentFileWriteStatus::Changed, {}};
}

[[nodiscard]] DocumentFileWriteResult MakeWriteSuccess(
	const DocumentFileStamp &stamp) {
	return {DocumentFileWriteStatus::Success, stamp};
}

}

DocumentFileReadResult ReadDocumentFile(const std::string &path,
	std::size_t maximumBytes) {
	// Use POSIX open/read rather than iostream so directories and other
	// non-regular paths return a failure instead of throwing from filebuf.
	if (path.empty()) {
		return MakeReadFailure();
	}
	const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		return MakeReadFailure();
	}
	struct stat before {};
	if (fstat(fd, &before) != 0 || !S_ISREG(before.st_mode)) {
		(void)close(fd);
		return MakeReadFailure();
	}

	// Reject a reported size above the caller's limit before reserving. Exact
	// equality remains allowed; only a greater size is too large.
	if (before.st_size > 0) {
		if (static_cast<std::uintmax_t>(before.st_size) >
			static_cast<std::uintmax_t>(maximumBytes)) {
			(void)close(fd);
			return MakeTooLarge();
		}
	}

	std::string bytes;
	if (before.st_size > 0) {
		bytes.reserve(static_cast<std::size_t>(before.st_size));
	}
	constexpr std::size_t chunkSize = 64U * 1024U;
	char buffer[chunkSize];
	for (;;) {
		// bytes.size() never exceeds maximumBytes, so remaining cannot wrap.
		const std::size_t remaining = maximumBytes - bytes.size();
		if (remaining == 0) {
			// Already at the caller's limit: one more readable byte is too large.
			char extra = 0;
			const ssize_t n = ::read(fd, &extra, 1);
			if (n < 0) {
				if (errno == EINTR) {
					continue;
				}
				(void)close(fd);
				return MakeReadFailure();
			}
			if (n == 0) {
				break;
			}
			(void)close(fd);
			return MakeTooLarge();
		}

		const std::size_t toRead = std::min(chunkSize, remaining);
		const ssize_t n = ::read(fd, buffer, toRead);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			(void)close(fd);
			return MakeReadFailure();
		}
		if (n == 0) {
			break;
		}
		bytes.append(buffer, static_cast<std::size_t>(n));
	}

	// Reject a stamp change during the read so the buffer is not accepted with
	// identity fields that may describe different bytes.
	struct stat after {};
	if (fstat(fd, &after) != 0 || !S_ISREG(after.st_mode)) {
		(void)close(fd);
		return MakeReadFailure();
	}
	const DocumentFileStamp stamp = StampFromStat(before);
	if (stamp != StampFromStat(after)) {
		(void)close(fd);
		return MakeReadFailure();
	}
	if (close(fd) != 0) {
		return MakeReadFailure();
	}
	return {DocumentFileReadStatus::Success, std::move(bytes), stamp};
}

DocumentFileWriteResult WriteDocumentFile(const std::string &path,
	std::string_view text, const DocumentFileStamp *expected) {
	if (path.empty()) {
		return MakeWriteFailure();
	}

	struct stat existing {};
	bool haveExisting = false;
	const std::string destination =
		ResolveWriteDestination(path, existing, haveExisting);

	// Guarded saves compare the followed regular file before expensive
	// temporary-file work so a known conflict never creates a temp file.
	if (expected != nullptr && !DestinationMatches(destination, *expected)) {
		return MakeWriteChanged();
	}

	const std::string directory = DocumentDirectory(destination);
	const std::string tempDirectory = directory.empty() ? "." : directory;
	std::string pattern = tempDirectory + "/.scalpel-XXXXXX";
	std::vector<char> mutablePattern(pattern.begin(), pattern.end());
	mutablePattern.push_back('\0');

	const int fd = mkstemp(mutablePattern.data());
	if (fd < 0) {
		return MakeWriteFailure();
	}
	const std::string temporaryPath(mutablePattern.data());

	// New files keep the mode mkstemp chose (0600). Existing regular files keep
	// their user/group/other bits so a rewrite does not widen access.
	if (haveExisting && S_ISREG(existing.st_mode)) {
		if (fchmod(fd, existing.st_mode & 0777) != 0) {
			(void)close(fd);
			(void)unlink(temporaryPath.c_str());
			return MakeWriteFailure();
		}
	}

	if (!WriteAll(fd, text)) {
		(void)close(fd);
		(void)unlink(temporaryPath.c_str());
		return MakeWriteFailure();
	}
	if (fsync(fd) != 0) {
		(void)close(fd);
		(void)unlink(temporaryPath.c_str());
		return MakeWriteFailure();
	}

	// Stamp the temporary descriptor before close so a later path replacement
	// cannot be mistaken for this save's identity.
	struct stat written {};
	if (fstat(fd, &written) != 0 || !S_ISREG(written.st_mode)) {
		(void)close(fd);
		(void)unlink(temporaryPath.c_str());
		return MakeWriteFailure();
	}
	const DocumentFileStamp stamp = StampFromStat(written);

	if (close(fd) != 0) {
		(void)unlink(temporaryPath.c_str());
		return MakeWriteFailure();
	}

	// Re-check immediately before rename. A mismatch leaves the destination
	// untouched and discards the temporary file.
	if (expected != nullptr && !DestinationMatches(destination, *expected)) {
		(void)unlink(temporaryPath.c_str());
		return MakeWriteChanged();
	}

	if (rename(temporaryPath.c_str(), destination.c_str()) != 0) {
		(void)unlink(temporaryPath.c_str());
		return MakeWriteFailure();
	}

	// Best-effort directory durability for the rename; failure here still leaves
	// the new file reachable under destination.
	const int directoryFd =
		open(tempDirectory.c_str(), O_RDONLY | O_DIRECTORY);
	if (directoryFd >= 0) {
		(void)fsync(directoryFd);
		(void)close(directoryFd);
	}
	return MakeWriteSuccess(stamp);
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
