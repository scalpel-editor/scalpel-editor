// Whole-file document read and write for open and save.

#ifndef DOCUMENTFILE_H
#define DOCUMENTFILE_H

#include <optional>
#include <string>
#include <string_view>

namespace Scalpel {

/**
 * Read every byte from path. Returns nullopt when the file cannot be opened
 * or read. Bytes are not validated as UTF-8.
 */
[[nodiscard]] std::optional<std::string> ReadDocumentFile(
	const std::string &path);

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

/** Final path component, or the whole path when it has no slash. */
[[nodiscard]] std::string DocumentBaseName(std::string_view path);

}

#endif
