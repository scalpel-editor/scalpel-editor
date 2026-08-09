// Whole-file document read and write for open and save.

#ifndef DOCUMENTFILE_H
#define DOCUMENTFILE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Scalpel {

/** 1 MiB in bytes. Shared unit for document and recent-state limits. */
constexpr std::size_t DocumentFileMebibyteBytes = 1024U * 1024U;

/**
 * Interactive open warns before loading more than this many bytes. Files of
 * exactly this size open without a warning.
 */
constexpr std::size_t DocumentFileWarningThresholdBytes =
	64U * DocumentFileMebibyteBytes;

/**
 * Hard maximum bytes for a whole-file document load. Files of exactly this
 * size remain eligible after confirmation; larger files never load.
 */
constexpr std::size_t DocumentFileHardLimitBytes =
	256U * DocumentFileMebibyteBytes;

/**
 * Identity of a followed regular file used for save-time conflict detection.
 * Fields are device, inode, byte size, and nanosecond modification time from
 * fstat on the descriptor that supplied or received the bytes.
 */
struct DocumentFileStamp {
	std::uint64_t device = 0;
	std::uint64_t inode = 0;
	std::uint64_t sizeBytes = 0;
	std::int64_t modificationSeconds = 0;
	std::int64_t modificationNanoseconds = 0;

	friend constexpr bool operator==(const DocumentFileStamp &left,
		const DocumentFileStamp &right) noexcept {
		return left.device == right.device && left.inode == right.inode &&
			left.sizeBytes == right.sizeBytes &&
			left.modificationSeconds == right.modificationSeconds &&
			left.modificationNanoseconds == right.modificationNanoseconds;
	}

	friend constexpr bool operator!=(const DocumentFileStamp &left,
		const DocumentFileStamp &right) noexcept {
		return !(left == right);
	}
};

enum class DocumentFileReadStatus {
	Success,
	ReadFailure,
	TooLarge,
};

/**
 * Outcome of a bounded whole-file read. bytes and stamp are set only on
 * Success. stamp describes the descriptor that supplied those bytes.
 */
struct DocumentFileReadResult {
	DocumentFileReadStatus status = DocumentFileReadStatus::ReadFailure;
	std::string bytes;
	DocumentFileStamp stamp{};
};

/**
 * Read every byte from a regular file at path, stopping if the file exceeds
 * maximumBytes. Returns Success with the file bytes and a stamp when the path
 * is a readable regular file no larger than maximumBytes and the stamp fields
 * are stable for the whole read. Returns TooLarge when fstat reports a larger
 * size or more than maximumBytes are available while reading. Returns
 * ReadFailure when the path is empty, missing, not a regular file (directory,
 * device, fifo, and so on), cannot be opened or read, or the stamp fields
 * change while reading. Symlinks to regular files are followed. Bytes are not
 * validated as UTF-8. Never appends past maximumBytes.
 */
[[nodiscard]] DocumentFileReadResult ReadDocumentFile(const std::string &path,
	std::size_t maximumBytes);

enum class DocumentFileWriteStatus {
	Success,
	Changed,
	WriteFailure,
};

/**
 * Outcome of an atomic whole-file write. stamp is set only on Success and
 * describes the temporary-file descriptor that became the destination.
 */
struct DocumentFileWriteResult {
	DocumentFileWriteStatus status = DocumentFileWriteStatus::WriteFailure;
	DocumentFileStamp stamp{};
};

/**
 * Write text bytes to path by finishing a temporary file in the same directory
 * and renaming it into place. Returns Success with a stamp for the written
 * file when the temporary file is created, written, finished, and renamed.
 * Returns WriteFailure when any of those steps fail; the previous destination
 * contents remain when rename never runs. Returns Changed when expected is
 * non-null and the followed destination does not match that stamp before
 * temporary-file work or immediately before rename; in that case no temporary
 * file remains and the destination is left untouched.
 *
 * Symlinks: an existing symlink at path is followed when its target resolves,
 * so the linked file is replaced and the link stays. A dangling symlink is
 * replaced by the new regular file at path. expected must describe the
 * followed regular file, matching ReadDocumentFile stamps.
 *
 * Permissions: a rewrite of an existing regular file keeps that file's mode
 * bits. A newly created file uses the mode from mkstemp (0600).
 *
 * The final destination stat and rename are not a perfect cross-process
 * compare-and-swap on ordinary POSIX files; another writer can still race the
 * last check.
 */
[[nodiscard]] DocumentFileWriteResult WriteDocumentFile(
	const std::string &path, std::string_view text,
	const DocumentFileStamp *expected = nullptr);

/** Parent directory of path, or empty when path has no slash. */
[[nodiscard]] std::string DocumentDirectory(std::string_view path);

/** Final path component, or the whole path when path has no slash. */
[[nodiscard]] std::string DocumentBaseName(std::string_view path);

}

#endif
