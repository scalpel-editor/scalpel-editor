#include "catch.hpp"

#include <string>

#include <dbus/dbus.h>

#include "WaylandFileDialog.h"
#include "WaylandLifecycle.h"

TEST_CASE("Wayland portal parent follows the current export") {
	Scalpel::WaylandPortalParentState parent;
	CHECK_FALSE(parent.ExportActive());
	CHECK(parent.ParentHandle().empty());

	parent.BeginExport(10);
	CHECK(parent.ExportActive());
	parent.DeliverHandle(10, "first");
	CHECK(parent.ParentHandle() == "wayland:first");

	parent.BeginExport(20);
	CHECK(parent.ParentHandle().empty());
	parent.DeliverHandle(10, "stale");
	CHECK(parent.ParentHandle().empty());
	parent.DeliverHandle(20, "second");
	CHECK(parent.ParentHandle() == "wayland:second");

	parent.EndExport(10);
	CHECK(parent.ParentHandle() == "wayland:second");
	parent.EndExport(20);
	CHECK_FALSE(parent.ExportActive());
	CHECK(parent.ParentHandle().empty());
	CHECK_THROWS(parent.BeginExport(0));
}

TEST_CASE("Wayland file dialog state accepts a matching open response") {
	Scalpel::WaylandFileDialogState state;
	const uint64_t id = state.Begin(Scalpel::FileDialogMode::Open,
		"/org/freedesktop/portal/desktop/request/1_2/token", ":1.42");
	CHECK(state.HasPending());
	CHECK(state.HasPending(
		"/org/freedesktop/portal/desktop/request/1_2/token"));

	state.DeliverResponse(
		"/org/freedesktop/portal/desktop/request/1_2/token", ":1.42", 0,
		{"file:///tmp/note%20a.txt", "http://example.com/x"});
	CHECK_FALSE(state.HasPending());

	const std::vector<Scalpel::FileDialogResult> results = state.TakeResults();
	REQUIRE(results.size() == 1);
	CHECK(results.front().id == id);
	CHECK(results.front().mode == Scalpel::FileDialogMode::Open);
	CHECK(results.front().status == Scalpel::FileDialogResultStatus::Accepted);
	REQUIRE(results.front().paths.size() == 1);
	CHECK(results.front().paths.front() == "/tmp/note a.txt");
	CHECK(state.TakeResults().empty());
}

TEST_CASE("Wayland file dialog state rejects a mismatched portal owner") {
	Scalpel::WaylandFileDialogState state;
	(void)state.Begin(Scalpel::FileDialogMode::Save,
		"/org/freedesktop/portal/desktop/request/1_2/token", ":1.42");
	state.DeliverResponse(
		"/org/freedesktop/portal/desktop/request/1_2/token", ":1.99", 0,
		{"file:///tmp/save.txt"});
	CHECK(state.HasPending());
	CHECK(state.TakeResults().empty());
}

TEST_CASE("Wayland file dialog state records cancellation") {
	Scalpel::WaylandFileDialogState state;
	const uint64_t id = state.Begin(Scalpel::FileDialogMode::Open,
		"/path/request", ":1.1");
	state.DeliverResponse("/path/request", ":1.1", 1, {"file:///tmp/x"});
	const auto results = state.TakeResults();
	REQUIRE(results.size() == 1);
	CHECK(results.front().id == id);
	CHECK(results.front().status == Scalpel::FileDialogResultStatus::Cancelled);
	CHECK(results.front().paths.empty());
}

TEST_CASE("Wayland file dialog state records non-cancel portal failure") {
	Scalpel::WaylandFileDialogState state;
	const uint64_t id = state.Begin(Scalpel::FileDialogMode::Open,
		"/path/request", ":1.1");
	state.DeliverResponse("/path/request", ":1.1", 2, {"file:///tmp/x"});
	const auto results = state.TakeResults();
	REQUIRE(results.size() == 1);
	CHECK(results.front().id == id);
	CHECK(results.front().status == Scalpel::FileDialogResultStatus::Failed);
	CHECK(results.front().paths.empty());
}

TEST_CASE("Wayland file dialog state fails when no usable URI remains") {
	Scalpel::WaylandFileDialogState state;
	(void)state.Begin(Scalpel::FileDialogMode::Open, "/path/request", ":1.1");
	state.DeliverResponse("/path/request", ":1.1", 0,
		{"http://example.com/a", "file://host"});
	const auto results = state.TakeResults();
	REQUIRE(results.size() == 1);
	CHECK(results.front().status == Scalpel::FileDialogResultStatus::Failed);
	CHECK(results.front().paths.empty());
}

TEST_CASE("Wayland file dialog state abandons a predicted request path") {
	Scalpel::WaylandFileDialogState state;
	(void)state.Begin(Scalpel::FileDialogMode::Open, "/path/predicted", ":1.1");
	CHECK(state.Abandon("/path/predicted"));
	CHECK_FALSE(state.HasPending());
	CHECK_FALSE(state.Abandon("/path/predicted"));
	state.DeliverResponse("/path/predicted", ":1.1", 0,
		{"file:///tmp/x.txt"});
	CHECK(state.TakeResults().empty());
}

TEST_CASE("Wayland file dialog state retargets a predicted request path") {
	Scalpel::WaylandFileDialogState state;
	const uint64_t id = state.Begin(Scalpel::FileDialogMode::Save,
		"/path/predicted", ":1.1");
	CHECK(state.Retarget("/path/predicted", "/path/actual", ":1.2"));
	CHECK_FALSE(state.HasPending("/path/predicted"));
	CHECK(state.HasPending("/path/actual"));

	state.DeliverResponse("/path/actual", ":1.2", 0, {"file:///tmp/save.txt"});
	const auto results = state.TakeResults();
	REQUIRE(results.size() == 1);
	CHECK(results.front().id == id);
	CHECK(results.front().status == Scalpel::FileDialogResultStatus::Accepted);
	CHECK(results.front().paths.front() == "/tmp/save.txt");
}

TEST_CASE("Wayland file dialog state retarget is a no-op after delivery") {
	Scalpel::WaylandFileDialogState state;
	(void)state.Begin(Scalpel::FileDialogMode::Open, "/path/predicted", ":1.1");
	state.DeliverResponse("/path/predicted", ":1.1", 0,
		{"file:///tmp/chosen.txt"});
	CHECK_FALSE(state.Retarget("/path/predicted", "/path/actual", ":1.1"));
	CHECK_FALSE(state.HasPending());
	REQUIRE(state.TakeResults().size() == 1);
}

TEST_CASE("Wayland file dialog state drops pending work on clear") {
	Scalpel::WaylandFileDialogState state;
	(void)state.Begin(Scalpel::FileDialogMode::Open, "/path/request", ":1.1");
	state.Clear();
	CHECK_FALSE(state.HasPending());
	state.DeliverResponse("/path/request", ":1.1", 0,
		{"file:///tmp/x.txt"});
	CHECK(state.TakeResults().empty());
}

TEST_CASE("Wayland file dialog state rejects invalid begin arguments") {
	Scalpel::WaylandFileDialogState state;
	CHECK_THROWS(state.Begin(Scalpel::FileDialogMode::Open, "", ":1.1"));
	CHECK_THROWS(state.Begin(Scalpel::FileDialogMode::Open, "/path", ""));
	(void)state.Begin(Scalpel::FileDialogMode::Open, "/path", ":1.1");
	CHECK_THROWS(state.Begin(Scalpel::FileDialogMode::Open, "/path", ":1.1"));
}

TEST_CASE("Wayland file dialog request IDs stay unique across concurrent pending") {
	Scalpel::WaylandFileDialogState state;
	const uint64_t openId = state.Begin(Scalpel::FileDialogMode::Open,
		"/path/open", ":1.1");
	const uint64_t saveId = state.Begin(Scalpel::FileDialogMode::Save,
		"/path/save", ":1.1");
	CHECK(openId != saveId);
	CHECK(openId != 0);
	CHECK(saveId != 0);

	state.DeliverResponse("/path/save", ":1.1", 0, {"file:///tmp/save.txt"});
	state.DeliverResponse("/path/open", ":1.1", 0, {"file:///tmp/open.txt"});
	const auto results = state.TakeResults();
	REQUIRE(results.size() == 2);
	CHECK(results[0].id == saveId);
	CHECK(results[0].mode == Scalpel::FileDialogMode::Save);
	CHECK(results[0].paths.front() == "/tmp/save.txt");
	CHECK(results[1].id == openId);
	CHECK(results[1].mode == Scalpel::FileDialogMode::Open);
	CHECK(results[1].paths.front() == "/tmp/open.txt");
}

TEST_CASE("Wayland file dialog request IDs survive retarget") {
	Scalpel::WaylandFileDialogState state;
	const uint64_t id = state.Begin(Scalpel::FileDialogMode::Open,
		"/path/predicted", ":1.1");
	REQUIRE(state.Retarget("/path/predicted", "/path/actual", ":1.9"));
	state.DeliverResponse("/path/actual", ":1.9", 0,
		{"file:///tmp/a.txt", "file:///tmp/b.txt"});
	const auto results = state.TakeResults();
	REQUIRE(results.size() == 1);
	CHECK(results.front().id == id);
	REQUIRE(results.front().paths.size() == 2);
	CHECK(results.front().paths[0] == "/tmp/a.txt");
	CHECK(results.front().paths[1] == "/tmp/b.txt");
}

TEST_CASE("Wayland file dialog request IDs appear on cancel and failure") {
	Scalpel::WaylandFileDialogState state;
	const uint64_t cancelId = state.Begin(Scalpel::FileDialogMode::Open,
		"/path/cancel", ":1.1");
	const uint64_t failId = state.Begin(Scalpel::FileDialogMode::Save,
		"/path/fail", ":1.1");
	state.DeliverResponse("/path/cancel", ":1.1", 1, {});
	state.DeliverResponse("/path/fail", ":1.1", 2, {});
	const auto results = state.TakeResults();
	REQUIRE(results.size() == 2);
	CHECK(results[0].id == cancelId);
	CHECK(results[0].status == Scalpel::FileDialogResultStatus::Cancelled);
	CHECK(results[1].id == failId);
	CHECK(results[1].status == Scalpel::FileDialogResultStatus::Failed);
}

TEST_CASE("Wayland file dialog request IDs option multiple for open") {
	DBusMessage *message = dbus_message_new_method_call(
		"org.freedesktop.portal.Desktop",
		"/org/freedesktop/portal/desktop", "org.freedesktop.portal.FileChooser",
		"OpenFile");
	REQUIRE(message);
	DBusMessageIter iter;
	dbus_message_iter_init_append(message, &iter);
	const char *parent = "";
	const char *title = "Open";
	REQUIRE(dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &parent));
	REQUIRE(dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &title));
	DBusMessageIter options;
	REQUIRE(dbus_message_iter_open_container(
		&iter, DBUS_TYPE_ARRAY, "{sv}", &options));
	Scalpel::FileDialogAppendDictString(&options, "handle_token", "token1");
	Scalpel::FileDialogAppendDictBool(&options, "multiple", true);
	REQUIRE(dbus_message_iter_close_container(&iter, &options));

	DBusMessageIter read;
	REQUIRE(dbus_message_iter_init(message, &read));
	REQUIRE(dbus_message_iter_next(&read));
	REQUIRE(dbus_message_iter_next(&read));
	REQUIRE(dbus_message_iter_get_arg_type(&read) == DBUS_TYPE_ARRAY);
	DBusMessageIter dict;
	dbus_message_iter_recurse(&read, &dict);
	bool foundMultiple = false;
	while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry;
		dbus_message_iter_recurse(&dict, &entry);
		const char *key = nullptr;
		if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING) {
			dbus_message_iter_get_basic(&entry, &key);
		}
		if (key && std::string(key) == "multiple") {
			REQUIRE(dbus_message_iter_next(&entry));
			REQUIRE(dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT);
			DBusMessageIter variant;
			dbus_message_iter_recurse(&entry, &variant);
			REQUIRE(dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_BOOLEAN);
			dbus_bool_t value = FALSE;
			dbus_message_iter_get_basic(&variant, &value);
			CHECK(value == TRUE);
			foundMultiple = true;
		}
		dbus_message_iter_next(&dict);
	}
	CHECK(foundMultiple);
	dbus_message_unref(message);
}

TEST_CASE("Wayland file dialog appends filters and path options") {
	DBusMessage *message = dbus_message_new_method_call("org.freedesktop.portal.Desktop",
		"/org/freedesktop/portal/desktop", "org.freedesktop.portal.FileChooser",
		"OpenFile");
	REQUIRE(message);
	DBusMessageIter iter;
	dbus_message_iter_init_append(message, &iter);
	const char *parent = "";
	const char *title = "Open";
	REQUIRE(dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &parent));
	REQUIRE(dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &title));
	DBusMessageIter options;
	REQUIRE(dbus_message_iter_open_container(
		&iter, DBUS_TYPE_ARRAY, "{sv}", &options));
	Scalpel::FileDialogAppendDictString(&options, "handle_token", "token1");
	Scalpel::FileDialogAppendDictPath(&options, "current_folder", "/tmp/docs");
	Scalpel::FileDialogAppendDictFilters(&options, {
		{"Text", {"*.txt", "*.md"}},
		{"All files", {"*"}},
	});
	REQUIRE(dbus_message_iter_close_container(&iter, &options));

	DBusMessageIter read;
	REQUIRE(dbus_message_iter_init(message, &read));
	CHECK(dbus_message_iter_get_arg_type(&read) == DBUS_TYPE_STRING);
	REQUIRE(dbus_message_iter_next(&read));
	CHECK(dbus_message_iter_get_arg_type(&read) == DBUS_TYPE_STRING);
	REQUIRE(dbus_message_iter_next(&read));
	CHECK(dbus_message_iter_get_arg_type(&read) == DBUS_TYPE_ARRAY);
	DBusMessageIter dict;
	dbus_message_iter_recurse(&read, &dict);
	int entryCount = 0;
	while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
		++entryCount;
		dbus_message_iter_next(&dict);
	}
	CHECK(entryCount == 3);
	dbus_message_unref(message);
}

TEST_CASE("Wayland file dialog parse response extracts URIs") {
	DBusMessage *message = dbus_message_new_signal(
		"/org/freedesktop/portal/desktop/request/1_2/token",
		"org.freedesktop.portal.Request", "Response");
	REQUIRE(message);
	DBusMessageIter iter;
	dbus_message_iter_init_append(message, &iter);
	dbus_uint32_t code = 0;
	REQUIRE(dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &code));
	DBusMessageIter results;
	REQUIRE(dbus_message_iter_open_container(
		&iter, DBUS_TYPE_ARRAY, "{sv}", &results));
	DBusMessageIter entry;
	DBusMessageIter variant;
	DBusMessageIter uris;
	const char *key = "uris";
	REQUIRE(dbus_message_iter_open_container(
		&results, DBUS_TYPE_DICT_ENTRY, nullptr, &entry));
	REQUIRE(dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key));
	REQUIRE(dbus_message_iter_open_container(
		&entry, DBUS_TYPE_VARIANT, "as", &variant));
	REQUIRE(dbus_message_iter_open_container(
		&variant, DBUS_TYPE_ARRAY, "s", &uris));
	const char *uri = "file:///tmp/chosen.txt";
	REQUIRE(dbus_message_iter_append_basic(&uris, DBUS_TYPE_STRING, &uri));
	REQUIRE(dbus_message_iter_close_container(&variant, &uris));
	REQUIRE(dbus_message_iter_close_container(&entry, &variant));
	REQUIRE(dbus_message_iter_close_container(&results, &entry));
	REQUIRE(dbus_message_iter_close_container(&iter, &results));

	std::uint32_t parsedCode = 99;
	std::vector<std::string> parsedUris;
	REQUIRE(Scalpel::FileDialogParseResponse(message, parsedCode, parsedUris));
	CHECK(parsedCode == 0);
	REQUIRE(parsedUris.size() == 1);
	CHECK(parsedUris.front() == "file:///tmp/chosen.txt");
	dbus_message_unref(message);
}
