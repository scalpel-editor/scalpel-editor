#include "catch.hpp"

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
