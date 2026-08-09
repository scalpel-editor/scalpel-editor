#include "RecentFiles.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <system_error>

#include "DocumentFile.h"

namespace Scalpel {

namespace {

constexpr std::string_view kHeader = "scalpel-recent-files-v1";
constexpr std::size_t kMaximumStateBytes = 1024 * 1024;

std::string NormalizePath(std::string_view path) {
	return std::filesystem::path(std::string(path)).lexically_normal().string();
}

std::string EnvironmentValue(const char *name) {
	const char *value = std::getenv(name);
	return value ? std::string(value) : std::string{};
}

}

bool RecentFiles::Record(std::string_view path) {
	if (path.empty() || path.find('\0') != std::string_view::npos) {
		return false;
	}
	const std::string normalized = NormalizePath(path);
	if (normalized.empty()) {
		return false;
	}

	const auto existing = std::find(paths.begin(), paths.end(), normalized);
	if (existing != paths.end() && existing == paths.begin()) {
		return false;
	}
	if (existing != paths.end()) {
		paths.erase(existing);
	}
	paths.insert(paths.begin(), normalized);
	if (paths.size() > MaximumRecentFiles) {
		paths.resize(MaximumRecentFiles);
	}
	return true;
}

bool RecentFiles::Remove(std::string_view path) {
	if (path.empty() || path.find('\0') != std::string_view::npos) {
		return false;
	}
	const std::string normalized = NormalizePath(path);
	const auto existing = std::find(paths.begin(), paths.end(), normalized);
	if (existing == paths.end()) {
		return false;
	}
	paths.erase(existing);
	return true;
}

bool RecentFiles::Clear() noexcept {
	if (paths.empty()) {
		return false;
	}
	paths.clear();
	return true;
}

std::string RecentFilesStatePath(std::string_view xdgStateHome,
	std::string_view home) {
	std::string base;
	if (!xdgStateHome.empty()) {
		base = std::string(xdgStateHome);
	} else if (!home.empty()) {
		base = std::string(home) + "/.local/state";
	} else {
		return {};
	}
	return base + "/scalpel-editor/recent-files";
}

std::string RecentFilesStatePath() {
	return RecentFilesStatePath(
		EnvironmentValue("XDG_STATE_HOME"), EnvironmentValue("HOME"));
}

RecentFiles LoadRecentFiles(const std::string &statePath) {
	RecentFiles recent;
	if (statePath.empty()) {
		return recent;
	}
	// Enforce the 1 MiB state-file limit during the read so a corrupt or
	// hostile file cannot allocate more than the stated maximum.
	const DocumentFileReadResult bytes =
		ReadDocumentFile(statePath, kMaximumStateBytes);
	if (bytes.status != DocumentFileReadStatus::Success) {
		return recent;
	}

	const std::string prefix = std::string(kHeader) + '\0';
	if (bytes.bytes.size() < prefix.size() ||
		bytes.bytes.compare(0, prefix.size(), prefix) != 0 ||
		(!bytes.bytes.empty() && bytes.bytes.back() != '\0')) {
		return recent;
	}

	std::vector<std::string_view> stored;
	std::size_t start = prefix.size();
	while (start < bytes.bytes.size() && stored.size() < MaximumRecentFiles) {
		const std::size_t end = bytes.bytes.find('\0', start);
		if (end == std::string::npos) {
			return RecentFiles{};
		}
		if (end > start) {
			stored.emplace_back(bytes.bytes.data() + start, end - start);
		}
		start = end + 1;
	}
	for (auto iterator = stored.rbegin(); iterator != stored.rend(); ++iterator) {
		(void)recent.Record(*iterator);
	}
	return recent;
}

bool SaveRecentFiles(const std::string &statePath, const RecentFiles &recent) {
	if (statePath.empty()) {
		return false;
	}
	const std::filesystem::path filePath(statePath);
	const std::filesystem::path parent = filePath.parent_path();
	if (!parent.empty()) {
		std::error_code error;
		std::filesystem::create_directories(parent, error);
		if (error) {
			return false;
		}
	}

	std::string bytes(kHeader);
	bytes.push_back('\0');
	for (const std::string &path : recent.Paths()) {
		bytes.append(path);
		bytes.push_back('\0');
	}
	return WriteDocumentFile(statePath, bytes);
}

}
