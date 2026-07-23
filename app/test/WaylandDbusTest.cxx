#include <chrono>
#include <vector>

#include <poll.h>

#include "catch.hpp"

#include "WaylandDbus.h"

using namespace std::chrono_literals;

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
