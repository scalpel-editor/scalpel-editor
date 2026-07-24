// Desktop-portal file dialog request, pending state, and D-Bus calls.

#ifndef WAYLANDFILEDIALOG_H
#define WAYLANDFILEDIALOG_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <dbus/dbus.h>

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
	/**
	 * Drop a pending request without producing a result. Used when the portal
	 * method call fails after the predicted path was already tracked.
	 */
	bool Abandon(std::string_view requestPath) noexcept;
	/**
	 * Replace the predicted request path and portal owner with the values from
	 * the method reply. No-op when the predicted entry is already gone (for
	 * example after a Response arrived during the blocking call).
	 */
	bool Retarget(std::string_view predictedPath, std::string actualPath,
		std::string portalOwner);
	[[nodiscard]] bool HasPending(std::string_view requestPath) const;
	[[nodiscard]] bool HasPending() const noexcept {
		return !pending.empty();
	}
	/**
	 * Match a Response by object path and portal owner. responseCode 0 is
	 * success, 1 is user cancellation, and any other code is failure. URIs are
	 * converted to local paths; acceptance requires at least one usable path.
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

/**
 * Starts xdg-desktop-portal open and save dialogs on a session bus connection.
 *
 * Setup may briefly block on the bus to start the portal and receive the
 * request handle. User choice arrives later as a Request.Response signal while
 * the connection is dispatched from the application event loop.
 */
class WaylandFileDialog final {
public:
	WaylandFileDialog() = default;
	~WaylandFileDialog() noexcept;

	WaylandFileDialog(const WaylandFileDialog &) = delete;
	WaylandFileDialog &operator=(const WaylandFileDialog &) = delete;

	/**
	 * Start an open or save dialog. parentHandle is the xdg-foreign
	 * "wayland:…" token or empty for an unparented dialog. Returns false when
	 * the session bus or portal is unavailable; no result is queued then.
	 */
	[[nodiscard]] bool Show(DBusConnection *connection,
		std::string_view parentHandle, const FileDialogRequest &request);
	[[nodiscard]] std::vector<FileDialogResult> TakeResults() {
		return state.TakeResults();
	}
	/** Drop pending dialogs without acceptance and detach from the bus. */
	void Clear() noexcept;

private:
	[[nodiscard]] bool EnsureResponseFilter();
	[[nodiscard]] bool EnsurePortalReady(std::string &portalOwner);
	[[nodiscard]] bool MakeRequestToken(std::string &token,
		std::string &requestPath);
	void HandleResponse(DBusMessage *message);
	static DBusHandlerResult ResponseFilter(DBusConnection *connection,
		DBusMessage *message, void *data) noexcept;

	WaylandFileDialogState state;
	DBusConnection *connection = nullptr;
	bool responseFilterInstalled = false;
};

/** Append helpers used by Show and covered by unit tests. */
void FileDialogAppendDictString(DBusMessageIter *dict, const char *key,
	const char *value);
void FileDialogAppendDictPath(DBusMessageIter *dict, const char *key,
	std::string_view path);
void FileDialogAppendDictFilters(DBusMessageIter *dict,
	const std::vector<FileDialogFilter> &filters);
[[nodiscard]] bool FileDialogParseResponse(DBusMessage *message,
	std::uint32_t &responseCode, std::vector<std::string> &uris);

}

#endif
