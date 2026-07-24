#include "DocumentFile.h"

#include <fstream>
#include <iterator>

namespace Scalpel {

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
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	if (!output) {
		return false;
	}
	if (!text.empty()) {
		output.write(text.data(), static_cast<std::streamsize>(text.size()));
	}
	output.flush();
	return static_cast<bool>(output);
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
