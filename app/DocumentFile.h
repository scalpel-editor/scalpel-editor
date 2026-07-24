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
 * Write text bytes to path, replacing any existing file. Returns false when
 * the file cannot be opened or written completely.
 */
[[nodiscard]] bool WriteDocumentFile(const std::string &path,
	std::string_view text);

/** Parent directory of path, or empty when path has no slash. */
[[nodiscard]] std::string DocumentDirectory(std::string_view path);

/** Final path component, or the whole path when it has no slash. */
[[nodiscard]] std::string DocumentBaseName(std::string_view path);

}

#endif
