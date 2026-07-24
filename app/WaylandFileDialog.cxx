#include "WaylandFileDialog.h"

#include "PortalUri.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>

#include <dbus/dbus.h>
#include <sys/random.h>

namespace Scalpel {
namespace {

constexpr const char *PortalDesktopService = "org.freedesktop.portal.Desktop";
constexpr const char *PortalDesktopPath = "/org/freedesktop/portal/desktop";
constexpr const char *PortalFileChooserInterface =
	"org.freedesktop.portal.FileChooser";
constexpr const char *PortalRequestInterface = "org.freedesktop.portal.Request";

[[nodiscard]] char HexDigit(unsigned int value) noexcept {
	return value < 10 ? static_cast<char>('0' + value) :
		static_cast<char>('a' + value - 10);
}

[[nodiscard]] bool FillRandomBytes(unsigned char *bytes, std::size_t count) {
	std::size_t offset = 0;
	while (offset < count) {
		const ssize_t result = getrandom(bytes + offset, count - offset, 0);
		if (result > 0) {
			offset += static_cast<std::size_t>(result);
			continue;
		}
		if (result < 0 && errno == EINTR) {
			continue;
		}
		return false;
	}
	return true;
}

[[nodiscard]] bool StartPortalService(DBusConnection *connection) {
	DBusError error = DBUS_ERROR_INIT;
	dbus_uint32_t reply = 0;
	if (!dbus_bus_start_service_by_name(
			connection, PortalDesktopService, 0, &reply, &error)) {
		if (dbus_error_is_set(&error)) {
			dbus_error_free(&error);
		}
		return false;
	}
	return reply == DBUS_START_REPLY_SUCCESS ||
		reply == DBUS_START_REPLY_ALREADY_RUNNING;
}

[[nodiscard]] bool GetPortalOwner(DBusConnection *connection,
	std::string &owner) {
	DBusMessage *message = dbus_message_new_method_call(DBUS_SERVICE_DBUS,
		DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, "GetNameOwner");
	if (!message) {
		return false;
	}
	const char *service = PortalDesktopService;
	if (!dbus_message_append_args(
			message, DBUS_TYPE_STRING, &service, DBUS_TYPE_INVALID)) {
		dbus_message_unref(message);
		return false;
	}
	DBusError error = DBUS_ERROR_INIT;
	DBusMessage *reply = dbus_connection_send_with_reply_and_block(
		connection, message, DBUS_TIMEOUT_USE_DEFAULT, &error);
	dbus_message_unref(message);
	if (!reply) {
		if (dbus_error_is_set(&error)) {
			dbus_error_free(&error);
		}
		return false;
	}
	const char *value = nullptr;
	bool ok = false;
	if (dbus_message_get_args(
			reply, &error, DBUS_TYPE_STRING, &value, DBUS_TYPE_INVALID) &&
		value && value[0] == ':') {
		owner = value;
		ok = true;
	} else if (dbus_error_is_set(&error)) {
		dbus_error_free(&error);
	}
	dbus_message_unref(reply);
	return ok;
}

}

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

bool WaylandFileDialogState::Abandon(std::string_view requestPath) noexcept {
	const auto it = Find(requestPath);
	if (it == pending.end()) {
		return false;
	}
	pending.erase(it);
	return true;
}

bool WaylandFileDialogState::Retarget(std::string_view predictedPath,
	std::string actualPath, std::string portalOwner) {
	if (actualPath.empty()) {
		throw std::invalid_argument(
			"WaylandFileDialogState::Retarget requires an actual request path");
	}
	if (portalOwner.empty()) {
		throw std::invalid_argument(
			"WaylandFileDialogState::Retarget requires a portal owner");
	}
	const auto it = Find(predictedPath);
	if (it == pending.end()) {
		return false;
	}
	if (predictedPath != actualPath && Find(actualPath) != pending.end()) {
		throw std::invalid_argument(
			"WaylandFileDialogState::Retarget rejects a duplicate request path");
	}
	it->requestPath = std::move(actualPath);
	it->portalOwner = std::move(portalOwner);
	return true;
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

	if (responseCode == 1) {
		result.status = FileDialogResultStatus::Cancelled;
		results.push_back(std::move(result));
		return;
	}
	if (responseCode != 0) {
		result.status = FileDialogResultStatus::Failed;
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

void FileDialogAppendDictString(DBusMessageIter *dict, const char *key,
	const char *value) {
	DBusMessageIter entry;
	DBusMessageIter variant;
	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
	dbus_message_iter_open_container(
		&entry, DBUS_TYPE_VARIANT, DBUS_TYPE_STRING_AS_STRING, &variant);
	dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
	dbus_message_iter_close_container(&entry, &variant);
	dbus_message_iter_close_container(dict, &entry);
}

void FileDialogAppendDictBool(DBusMessageIter *dict, const char *key, bool value) {
	DBusMessageIter entry;
	DBusMessageIter variant;
	dbus_bool_t dbusValue = value ? TRUE : FALSE;
	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
	dbus_message_iter_open_container(
		&entry, DBUS_TYPE_VARIANT, DBUS_TYPE_BOOLEAN_AS_STRING, &variant);
	dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &dbusValue);
	dbus_message_iter_close_container(&entry, &variant);
	dbus_message_iter_close_container(dict, &entry);
}

void FileDialogAppendDictPath(DBusMessageIter *dict, const char *key,
	std::string_view path) {
	DBusMessageIter entry;
	DBusMessageIter variant;
	DBusMessageIter array;
	// Portal current_folder is a nul-terminated byte array (ay).
	const std::string owned(path);
	const char *bytes = owned.c_str();
	const int byteCount = static_cast<int>(owned.size()) + 1;
	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT,
		DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_BYTE_AS_STRING, &variant);
	dbus_message_iter_open_container(
		&variant, DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE_AS_STRING, &array);
	dbus_message_iter_append_fixed_array(
		&array, DBUS_TYPE_BYTE, &bytes, byteCount);
	dbus_message_iter_close_container(&variant, &array);
	dbus_message_iter_close_container(&entry, &variant);
	dbus_message_iter_close_container(dict, &entry);
}

void FileDialogAppendDictFilters(DBusMessageIter *dict,
	const std::vector<FileDialogFilter> &filters) {
	DBusMessageIter entry;
	DBusMessageIter variant;
	DBusMessageIter filterArray;
	const char *key = "filters";
	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "a(sa(us))", &variant);
	dbus_message_iter_open_container(
		&variant, DBUS_TYPE_ARRAY, "(sa(us))", &filterArray);
	for (const FileDialogFilter &filter : filters) {
		DBusMessageIter filterStruct;
		DBusMessageIter patternArray;
		dbus_message_iter_open_container(
			&filterArray, DBUS_TYPE_STRUCT, nullptr, &filterStruct);
		const char *name = filter.name.c_str();
		dbus_message_iter_append_basic(&filterStruct, DBUS_TYPE_STRING, &name);
		dbus_message_iter_open_container(
			&filterStruct, DBUS_TYPE_ARRAY, "(us)", &patternArray);
		for (const std::string &pattern : filter.patterns) {
			DBusMessageIter patternStruct;
			dbus_message_iter_open_container(
				&patternArray, DBUS_TYPE_STRUCT, nullptr, &patternStruct);
			dbus_uint32_t kind = 0;
			const char *text = pattern.c_str();
			dbus_message_iter_append_basic(
				&patternStruct, DBUS_TYPE_UINT32, &kind);
			dbus_message_iter_append_basic(
				&patternStruct, DBUS_TYPE_STRING, &text);
			dbus_message_iter_close_container(&patternArray, &patternStruct);
		}
		dbus_message_iter_close_container(&filterStruct, &patternArray);
		dbus_message_iter_close_container(&filterArray, &filterStruct);
	}
	dbus_message_iter_close_container(&variant, &filterArray);
	dbus_message_iter_close_container(&entry, &variant);
	dbus_message_iter_close_container(dict, &entry);
}

bool FileDialogParseResponse(DBusMessage *message, std::uint32_t &responseCode,
	std::vector<std::string> &uris) {
	uris.clear();
	DBusMessageIter iter;
	if (!dbus_message_iter_init(message, &iter) ||
		dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UINT32) {
		return false;
	}
	dbus_uint32_t code = 0;
	dbus_message_iter_get_basic(&iter, &code);
	responseCode = code;
	if (code != 0) {
		return true;
	}
	if (!dbus_message_iter_next(&iter) ||
		dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
		return false;
	}
	DBusMessageIter results;
	dbus_message_iter_recurse(&iter, &results);
	while (dbus_message_iter_get_arg_type(&results) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry;
		dbus_message_iter_recurse(&results, &entry);
		const char *key = nullptr;
		if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
			dbus_message_iter_get_basic(&entry, &key);
		}
		if (key && std::strcmp(key, "uris") == 0 &&
			dbus_message_iter_next(&entry) &&
			dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT) {
			DBusMessageIter variant;
			dbus_message_iter_recurse(&entry, &variant);
			if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_ARRAY) {
				DBusMessageIter uriIter;
				dbus_message_iter_recurse(&variant, &uriIter);
				while (dbus_message_iter_get_arg_type(&uriIter) ==
					DBUS_TYPE_STRING) {
					const char *uri = nullptr;
					dbus_message_iter_get_basic(&uriIter, &uri);
					if (uri) {
						uris.emplace_back(uri);
					}
					dbus_message_iter_next(&uriIter);
				}
			}
		}
		dbus_message_iter_next(&results);
	}
	return true;
}

WaylandFileDialog::~WaylandFileDialog() noexcept {
	Clear();
}

bool WaylandFileDialog::EnsureResponseFilter() {
	if (!connection) {
		return false;
	}
	if (responseFilterInstalled) {
		return true;
	}
	DBusError error = DBUS_ERROR_INIT;
	dbus_bus_add_match(connection,
		"type='signal',interface='org.freedesktop.portal.Request',"
		"member='Response'",
		&error);
	if (dbus_error_is_set(&error)) {
		dbus_error_free(&error);
		return false;
	}
	if (!dbus_connection_add_filter(
			connection, &WaylandFileDialog::ResponseFilter, this, nullptr)) {
		return false;
	}
	responseFilterInstalled = true;
	return true;
}

bool WaylandFileDialog::EnsurePortalReady(std::string &portalOwner) {
	return connection && StartPortalService(connection) &&
		GetPortalOwner(connection, portalOwner);
}

bool WaylandFileDialog::MakeRequestToken(std::string &token,
	std::string &requestPath) {
	if (!connection) {
		return false;
	}
	const char *uniqueName = dbus_bus_get_unique_name(connection);
	if (!uniqueName) {
		return false;
	}
	std::string sender = uniqueName;
	if (!sender.empty() && sender[0] == ':') {
		sender.erase(0, 1);
	}
	for (char &c : sender) {
		if (c == '.') {
			c = '_';
		}
	}
	unsigned char randomBytes[16];
	if (!FillRandomBytes(randomBytes, sizeof(randomBytes))) {
		return false;
	}
	token = "scalpel";
	token.reserve(token.size() + sizeof(randomBytes) * 2);
	for (unsigned char byte : randomBytes) {
		token.push_back(HexDigit(byte >> 4));
		token.push_back(HexDigit(byte & 0x0f));
	}
	requestPath = "/org/freedesktop/portal/desktop/request/" + sender + "/" +
		token;
	return true;
}

void WaylandFileDialog::HandleResponse(DBusMessage *message) {
	const char *path = dbus_message_get_path(message);
	const char *sender = dbus_message_get_sender(message);
	if (!path || !sender) {
		return;
	}
	std::uint32_t responseCode = 1;
	std::vector<std::string> uris;
	if (!FileDialogParseResponse(message, responseCode, uris)) {
		if (state.HasPending(path)) {
			// Treat a malformed success payload as failure by delivering code 0
			// with no usable URIs after a parse failure on the results dict.
			state.DeliverResponse(path, sender, 0, {});
		}
		return;
	}
	state.DeliverResponse(path, sender, responseCode, uris);
}

DBusHandlerResult WaylandFileDialog::ResponseFilter(DBusConnection *,
	DBusMessage *message, void *data) noexcept {
	if (!dbus_message_is_signal(message, PortalRequestInterface, "Response")) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
	try {
		auto &dialog = *static_cast<WaylandFileDialog *>(data);
		const char *path = dbus_message_get_path(message);
		if (!path || !dialog.state.HasPending(path)) {
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
		}
		dialog.HandleResponse(message);
		return DBUS_HANDLER_RESULT_HANDLED;
	} catch (...) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
}

std::optional<uint64_t> WaylandFileDialog::Show(DBusConnection *busConnection,
	std::string_view parentHandle, const FileDialogRequest &request) {
	if (!busConnection) {
		return std::nullopt;
	}
	connection = busConnection;

	std::string portalOwner;
	if (!EnsurePortalReady(portalOwner)) {
		return std::nullopt;
	}
	if (!EnsureResponseFilter()) {
		return std::nullopt;
	}

	std::string token;
	std::string requestPath;
	if (!MakeRequestToken(token, requestPath)) {
		return std::nullopt;
	}

	const char *method = request.mode == FileDialogMode::Save ?
		"SaveFile" :
		"OpenFile";
	DBusMessage *message = dbus_message_new_method_call(PortalDesktopService,
		PortalDesktopPath, PortalFileChooserInterface, method);
	if (!message) {
		return std::nullopt;
	}

	DBusMessageIter iter;
	dbus_message_iter_init_append(message, &iter);
	// Materialize an owned string so c_str() is always valid for libdbus.
	const std::string parentOwned(parentHandle);
	const char *parent = parentOwned.c_str();
	const char *title = request.title.c_str();
	if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &parent) ||
		!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &title)) {
		dbus_message_unref(message);
		return std::nullopt;
	}

	DBusMessageIter options;
	if (!dbus_message_iter_open_container(
			&iter, DBUS_TYPE_ARRAY, "{sv}", &options)) {
		dbus_message_unref(message);
		return std::nullopt;
	}
	FileDialogAppendDictString(&options, "handle_token", token.c_str());
	if (request.mode == FileDialogMode::Open && request.multiple) {
		// Contract: org.freedesktop.portal.FileChooser OpenFile option
		// "multiple" (b); read from the local interface description.
		FileDialogAppendDictBool(&options, "multiple", true);
	}
	if (request.mode == FileDialogMode::Save && !request.suggestedName.empty()) {
		FileDialogAppendDictString(
			&options, "current_name", request.suggestedName.c_str());
	}
	if (!request.currentFolder.empty()) {
		FileDialogAppendDictPath(
			&options, "current_folder", request.currentFolder);
	}
	if (!request.filters.empty()) {
		FileDialogAppendDictFilters(&options, request.filters);
	}
	if (!dbus_message_iter_close_container(&iter, &options)) {
		dbus_message_unref(message);
		return std::nullopt;
	}

	// Track the predicted Request path before the method call so a Response
	// that arrives during the blocking send still matches.
	const std::string predictedPath = requestPath;
	const std::string predictedOwner = portalOwner;
	const uint64_t requestId =
		state.Begin(request.mode, predictedPath, predictedOwner);

	DBusError error = DBUS_ERROR_INIT;
	DBusMessage *reply = dbus_connection_send_with_reply_and_block(
		connection, message, DBUS_TIMEOUT_USE_DEFAULT, &error);
	dbus_message_unref(message);
	if (!reply) {
		if (dbus_error_is_set(&error)) {
			dbus_error_free(&error);
		}
		(void)state.Abandon(predictedPath);
		return std::nullopt;
	}

	const char *replySender = dbus_message_get_sender(reply);
	if (replySender && replySender[0] == ':') {
		portalOwner = replySender;
	}

	DBusMessageIter replyIter;
	if (!dbus_message_iter_init(reply, &replyIter) ||
		dbus_message_iter_get_arg_type(&replyIter) != DBUS_TYPE_OBJECT_PATH) {
		dbus_message_unref(reply);
		(void)state.Abandon(predictedPath);
		return std::nullopt;
	}
	const char *handle = nullptr;
	dbus_message_iter_get_basic(&replyIter, &handle);
	dbus_message_unref(reply);
	if (!handle || handle[0] == '\0') {
		(void)state.Abandon(predictedPath);
		return std::nullopt;
	}

	// Retarget is a no-op when a Response already completed the predicted path
	// during the method call.
	if (predictedPath != handle || predictedOwner != portalOwner) {
		(void)state.Retarget(predictedPath, handle, std::move(portalOwner));
	}
	return requestId;
}

void WaylandFileDialog::Clear() noexcept {
	if (connection && responseFilterInstalled) {
		dbus_connection_remove_filter(
			connection, &WaylandFileDialog::ResponseFilter, this);
		responseFilterInstalled = false;
	}
	connection = nullptr;
	state.Clear();
}

}
