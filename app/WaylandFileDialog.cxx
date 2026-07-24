#include "WaylandFileDialog.h"

#include "PortalUri.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Scalpel {

uint64_t WaylandFileDialogState::Begin(FileDialogMode mode,
	std::string requestPath, std::string portalOwner) {
	if (requestPath.empty()) {
		throw std::invalid_argument(
			"WaylandFileDialogState::Begin requires a request path");
	}
	if (portalOwner.empty()) {
		throw std::invalid_argument(
			"WaylandFileDialogState::Begin requires a portal owner");
	}
	if (Find(requestPath) != pending.end()) {
		throw std::invalid_argument(
			"WaylandFileDialogState::Begin rejects a duplicate request path");
	}

	const uint64_t id = nextId++;
	pending.push_back(Pending{id, mode, std::move(requestPath),
		std::move(portalOwner)});
	return id;
}

bool WaylandFileDialogState::HasPending(std::string_view requestPath) const {
	return Find(requestPath) != pending.end();
}

void WaylandFileDialogState::DeliverResponse(std::string_view requestPath,
	std::string_view portalOwner, std::uint32_t responseCode,
	const std::vector<std::string> &uris) {
	const auto it = Find(requestPath);
	if (it == pending.end()) {
		return;
	}
	if (it->portalOwner != portalOwner) {
		return;
	}

	FileDialogResult result;
	result.id = it->id;
	result.mode = it->mode;
	pending.erase(it);

	if (responseCode != 0) {
		result.status = FileDialogResultStatus::Cancelled;
		results.push_back(std::move(result));
		return;
	}

	for (const std::string &uri : uris) {
		std::string path;
		if (PortalUriToLocalPath(uri, path)) {
			result.paths.push_back(std::move(path));
		}
	}
	if (result.paths.empty()) {
		result.status = FileDialogResultStatus::Failed;
	} else {
		result.status = FileDialogResultStatus::Accepted;
	}
	results.push_back(std::move(result));
}

void WaylandFileDialogState::Clear() noexcept {
	pending.clear();
	results.clear();
}

std::vector<FileDialogResult> WaylandFileDialogState::TakeResults() {
	std::vector<FileDialogResult> taken = std::move(results);
	results.clear();
	return taken;
}

std::vector<WaylandFileDialogState::Pending>::iterator
WaylandFileDialogState::Find(std::string_view requestPath) noexcept {
	return std::find_if(pending.begin(), pending.end(),
		[requestPath](const Pending &entry) {
			return entry.requestPath == requestPath;
		});
}

std::vector<WaylandFileDialogState::Pending>::const_iterator
WaylandFileDialogState::Find(std::string_view requestPath) const noexcept {
	return std::find_if(pending.begin(), pending.end(),
		[requestPath](const Pending &entry) {
			return entry.requestPath == requestPath;
		});
}

}
