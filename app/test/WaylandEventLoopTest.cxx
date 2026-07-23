#include <cerrno>
#include <chrono>
#include <limits>
#include <vector>

#include <poll.h>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "WaylandEventLoop.h"

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
	eventLoop.DispatchReady(descriptors);
	CHECK(called == std::vector<int>{10, 11});
}

TEST_CASE("Wayland event loop validates poll snapshots") {
	Scalpel::WaylandEventLoop eventLoop;
	CHECK_THROWS(eventLoop.AddSource(-1, POLLIN));
	CHECK_THROWS(eventLoop.AddSource(4, 0));
	eventLoop.AddSource(4, POLLIN);

	std::vector<pollfd> descriptors = eventLoop.PollDescriptors();
	descriptors[0].fd = 5;
	CHECK_THROWS(eventLoop.DispatchReady(descriptors));
	descriptors.clear();
	CHECK_THROWS(eventLoop.DispatchReady(descriptors));
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
