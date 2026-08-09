// Whole-file document read and write for open and save.

#ifndef DOCUMENTFILE_H
#define DOCUMENTFILE_H

#include <cstddef>
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

enum class DocumentFileReadStatus {
	Success,
	ReadFailure,
	TooLarge,
};

/** Outcome of a bounded whole-file read. bytes is set only on Success. */
struct DocumentFileReadResult {
	DocumentFileReadStatus status = DocumentFileReadStatus::ReadFailure;
	std::string bytes;
};

/**
 * Read every byte from a regular file at path, stopping if the file exceeds
 * maximumBytes. Returns Success with the file bytes when the path is a
 * readable regular file no larger than maximumBytes. Returns TooLarge when
 * fstat reports a larger size or more than maximumBytes are available while
 * reading. Returns ReadFailure when the path is empty, missing, not a regular
 * file (directory, device, fifo, and so on), or cannot be opened or read.
 * Symlinks to regular files are followed. Bytes are not validated as UTF-8.
 * Never appends past maximumBytes.
 */
[[nodiscard]] DocumentFileReadResult ReadDocumentFile(const std::string &path,
	std::size_t maximumBytes);

/**
 * Write text bytes to path by finishing a temporary file in the same directory
 * and renaming it into place. Returns false when the temporary file cannot be
 * created, written, finished, or renamed; the previous destination contents
 * remain when rename never runs.
 *
 * Symlinks: an existing symlink at path is followed when its target resolves,
 * so the linked file is replaced and the link stays. A dangling symlink is
 * replaced by the new regular file at path.
 *
 * Permissions: a rewrite of an existing regular file keeps that file's mode
 * bits. A newly created file uses the mode from mkstemp (0600).
 */
[[nodiscard]] bool WriteDocumentFile(const std::string &path,
	std::string_view text);

/** Parent directory of path, or empty when path has no slash. */
[[nodiscard]] std::string DocumentDirectory(std::string_view path);

/** Final path component, or the whole path when path has no slash. */
[[nodiscard]] std::string DocumentBaseName(std::string_view path);

}

#endif
