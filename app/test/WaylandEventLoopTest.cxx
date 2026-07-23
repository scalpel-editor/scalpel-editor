#include <cerrno>
#include <chrono>
#include <limits>
#include <vector>

#include <poll.h>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "WaylandEventLoop.h"
#include "WaylandDbus.h"
#include "WaylandLifecycle.h"

using namespace std::chrono_literals;

TEST_CASE("Wayland event loop merges deadlines without busy wait") {
	Scalpel::WaylandEventLoop eventLoop;
	CHECK(eventLoop.PollDescriptors().empty());
	CHECK(eventLoop.TimeoutMilliseconds() == -1);

	eventLoop.AddDeadline(80ms);
	eventLoop.AddDeadline(std::nullopt);
	eventLoop.AddEditorDeadline(20ms);
	eventLoop.AddDeadline(-5ms);
	CHECK(eventLoop.TimeoutMilliseconds() == 0);

	Scalpel::WaylandEventLoop bounded;
	bounded.AddDeadline(std::chrono::milliseconds{
		std::numeric_limits<int64_t>::max()});
	CHECK(bounded.TimeoutMilliseconds() == std::numeric_limits<int>::max());
}

TEST_CASE("Wayland event loop suppresses editor deadlines during flush recovery") {
	Scalpel::WaylandEventLoop eventLoop(true);
	eventLoop.AddEditorDeadline(0ms);
	eventLoop.AddDeadline(35ms);
	CHECK(eventLoop.TimeoutMilliseconds() == 35);
}

TEST_CASE("Wayland event loop dispatches every ready source fairly") {
	std::vector<int> called;
	Scalpel::WaylandEventLoop eventLoop;
	eventLoop.AddSource(10, POLLIN,
		[&called](short events) {
			CHECK(events == POLLIN);
			called.push_back(10);
		});
	eventLoop.AddSource(11, POLLOUT,
		[&called](short events) {
			CHECK(events == (POLLOUT | POLLHUP));
			called.push_back(11);
		});
	eventLoop.AddSource(12, POLLIN,
		[&called](short) { called.push_back(12); });

	std::vector<pollfd> descriptors = eventLoop.PollDescriptors();
	REQUIRE(descriptors.size() == 3);
	descriptors[0].revents = POLLIN;
	descriptors[1].revents = POLLOUT | POLLHUP;
	CHECK(eventLoop.DispatchReady(descriptors));
	CHECK(called == std::vector<int>{10, 11});
}

TEST_CASE("Wayland event loop validates poll snapshots") {
	Scalpel::WaylandEventLoop eventLoop;
	CHECK_THROWS(eventLoop.AddSource(-1, POLLIN));
	CHECK_THROWS(eventLoop.AddSource(4, 0));
	eventLoop.AddSource(4, POLLIN);

	std::vector<pollfd> descriptors = eventLoop.PollDescriptors();
	descriptors[0].fd = 5;
	CHECK_THROWS((void)eventLoop.DispatchReady(descriptors));
	descriptors.clear();
	CHECK_THROWS((void)eventLoop.DispatchReady(descriptors));
}

TEST_CASE("Wayland event loop distinguishes poll outcomes") {
	CHECK(Scalpel::WaylandEventLoop::InterpretPollResult(2, 0) ==
		Scalpel::WaylandPollOutcome::Ready);
	CHECK(Scalpel::WaylandEventLoop::InterpretPollResult(0, 0) ==
		Scalpel::WaylandPollOutcome::TimedOut);
	CHECK(Scalpel::WaylandEventLoop::InterpretPollResult(-1, EINTR) ==
		Scalpel::WaylandPollOutcome::Interrupted);
	CHECK_THROWS(
		Scalpel::WaylandEventLoop::InterpretPollResult(-1, EBADF));
}

TEST_CASE("Wayland D-Bus state exposes only enabled watches") {
	Scalpel::WaylandDbusState state;
	state.AddWatch(1, 10, POLLIN, true);
	state.AddWatch(2, 11, POLLOUT, false);
	state.AddWatch(3, 12, 0, true);

	CHECK(state.EnabledWatches() ==
		std::vector<Scalpel::WaylandDbusWatch>{{1, 10, POLLIN, true}});
	state.UpdateWatch(1, 13, POLLOUT, false);
	state.UpdateWatch(2, 14, POLLIN | POLLOUT, true);
	CHECK(state.EnabledWatches() ==
		std::vector<Scalpel::WaylandDbusWatch>{
			{2, 14, POLLIN | POLLOUT, true}});

	state.RemoveWatch(2);
	CHECK_FALSE(state.HasWatch(2));
	CHECK(state.EnabledWatches().empty());
	CHECK_THROWS(state.AddWatch(0, 4, POLLIN, true));
	CHECK_THROWS(state.AddWatch(4, -1, POLLIN, true));
}

TEST_CASE("Wayland D-Bus state updates and removes controlled timeouts") {
	Scalpel::WaylandDbusState::Clock::time_point current{};
	Scalpel::WaylandDbusState state([&current] { return current; });
	state.AddTimeout(1, 20ms, true);
	state.AddTimeout(2, 5ms, false);
	CHECK(state.TimeUntilTimeout() == 20ms);
	CHECK(state.DueTimeouts().empty());

	current += 20ms;
	CHECK(state.TimeUntilTimeout() == 0ms);
	CHECK(state.DueTimeouts() == std::vector<uintptr_t>{1});
	state.RearmTimeout(1);
	CHECK(state.TimeUntilTimeout() == 20ms);

	state.UpdateTimeout(1, 8ms, false);
	state.UpdateTimeout(2, 7ms, true);
	CHECK(state.TimeUntilTimeout() == 7ms);
	current += 7ms;
	CHECK(state.DueTimeouts() == std::vector<uintptr_t>{2});

	state.RemoveTimeout(2);
	CHECK_FALSE(state.HasTimeout(2));
	CHECK_FALSE(state.TimeUntilTimeout().has_value());
	CHECK_THROWS(state.AddTimeout(0, 1ms, true));
	CHECK_THROWS(state.AddTimeout(3, -1ms, true));
}

TEST_CASE("Wayland D-Bus state clears callbacks before owner teardown") {
	Scalpel::WaylandDbusState state;
	state.AddWatch(1, 10, POLLIN, true);
	state.AddTimeout(2, 10ms, true);
	state.Clear();
	CHECK_FALSE(state.HasWatch(1));
	CHECK_FALSE(state.HasTimeout(2));
	CHECK(state.EnabledWatches().empty());
	CHECK_FALSE(state.TimeUntilTimeout().has_value());
}

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
