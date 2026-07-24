// Desktop-portal file dialog request and pending-response state.

#ifndef WAYLANDFILEDIALOG_H
#define WAYLANDFILEDIALOG_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Scalpel {

enum class FileDialogMode {
	Open,
	Save,
};

enum class FileDialogResultStatus {
	Accepted,
	Cancelled,
	Failed,
	Unavailable,
};

struct FileDialogFilter {
	std::string name;
	std::vector<std::string> patterns;
};

struct FileDialogRequest {
	FileDialogMode mode = FileDialogMode::Open;
	std::string title;
	/** Optional starting directory as a local filesystem path. */
	std::string currentFolder;
	/** Save mode only: the default file name shown in the dialog. */
	std::string suggestedName;
	std::vector<FileDialogFilter> filters;
};

struct FileDialogResult {
	uint64_t id = 0;
	FileDialogMode mode = FileDialogMode::Open;
	FileDialogResultStatus status = FileDialogResultStatus::Failed;
	std::vector<std::string> paths;
};

/**
 * Tracks in-flight portal file dialogs without owning D-Bus connections.
 *
 * A pending entry is keyed by the portal Request object path and the unique
 * bus name of the portal that accepted the call. Results are delivered once
 * when a matching Response arrives; teardown drops pending work without
 * inventing acceptance.
 */
class WaylandFileDialogState final {
public:
	[[nodiscard]] uint64_t Begin(FileDialogMode mode, std::string requestPath,
		std::string portalOwner);
	[[nodiscard]] bool HasPending(std::string_view requestPath) const;
	[[nodiscard]] bool HasPending() const noexcept {
		return !pending.empty();
	}
	/**
	 * Match a Response by object path and portal owner. responseCode 0 is
	 * success; any other code is cancellation. URIs are converted to local
	 * paths; acceptance requires at least one usable path.
	 */
	void DeliverResponse(std::string_view requestPath,
		std::string_view portalOwner, std::uint32_t responseCode,
		const std::vector<std::string> &uris);
	void Clear() noexcept;
	[[nodiscard]] std::vector<FileDialogResult> TakeResults();

private:
	struct Pending {
		uint64_t id = 0;
		FileDialogMode mode = FileDialogMode::Open;
		std::string requestPath;
		std::string portalOwner;
	};

	[[nodiscard]] std::vector<Pending>::iterator Find(
		std::string_view requestPath) noexcept;
	[[nodiscard]] std::vector<Pending>::const_iterator Find(
		std::string_view requestPath) const noexcept;

	uint64_t nextId = 1;
	std::vector<Pending> pending;
	std::vector<FileDialogResult> results;
};

}

#endif
