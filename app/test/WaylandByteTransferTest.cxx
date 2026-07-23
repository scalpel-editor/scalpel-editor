#include <array>
#include <chrono>
#include <string>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

#include "catch.hpp"

#include "WaylandTransfer.h"

namespace {

struct Pipe {
	std::array<int, 2> descriptors{-1, -1};

	Pipe() {
		REQUIRE(pipe(descriptors.data()) == 0);
	}
	~Pipe() {
		for (int descriptor : descriptors) {
			if (descriptor >= 0) {
				(void)close(descriptor);
			}
		}
	}

	int TakeRead() {
		return std::exchange(descriptors[0], -1);
	}
	int TakeWrite() {
		return std::exchange(descriptors[1], -1);
	}
};

}

TEST_CASE("Wayland byte transfer reads through EAGAIN and EOF") {
	Pipe pipe;
	Scalpel::WaylandTransfer transfer = Scalpel::WaylandTransfer::Read(pipe.TakeRead());

	transfer.ProcessReady();
	CHECK(transfer.Pending());
	CHECK_FALSE(transfer.TakeResult().has_value());

	REQUIRE(write(pipe.descriptors[1], "partial", 7) == 7);
	transfer.ProcessReady();
	CHECK(transfer.Pending());

	REQUIRE(write(pipe.descriptors[1], " text", 5) == 5);
	(void)close(std::exchange(pipe.descriptors[1], -1));
	transfer.ProcessReady();

	REQUIRE_FALSE(transfer.Pending());
	const std::optional<Scalpel::WaylandTransferResult> result = transfer.TakeResult();
	REQUIRE(result);
	CHECK(result->status == Scalpel::WaylandTransferStatus::Complete);
	CHECK(result->bytes == "partial text");
	CHECK_FALSE(transfer.TakeResult().has_value());
}

TEST_CASE("Wayland byte transfer enforces the read limit") {
	Pipe pipe;
	Scalpel::WaylandTransfer transfer = Scalpel::WaylandTransfer::Read(pipe.TakeRead(), 3);
	REQUIRE(write(pipe.descriptors[1], "four", 4) == 4);
	transfer.ProcessReady();

	const std::optional<Scalpel::WaylandTransferResult> result = transfer.TakeResult();
	REQUIRE(result);
	CHECK(result->status == Scalpel::WaylandTransferStatus::TooLarge);
	CHECK(result->bytes.empty());
}

TEST_CASE("Wayland byte transfer writes incrementally without blocking") {
	Pipe pipe;
	const int capacity = fcntl(pipe.descriptors[1], F_GETPIPE_SZ);
	REQUIRE(capacity > 0);
	std::string bytes(static_cast<std::size_t>(capacity) + 1024U, 'x');
	Scalpel::WaylandTransfer transfer =
		Scalpel::WaylandTransfer::Write(pipe.TakeWrite(), bytes);

	transfer.ProcessReady();
	CHECK(transfer.Pending());

	std::string received;
	std::array<char, 4096> buffer{};
	while (transfer.Pending()) {
		const ssize_t count = read(pipe.descriptors[0], buffer.data(), buffer.size());
		REQUIRE(count > 0);
		received.append(buffer.data(), static_cast<std::size_t>(count));
		transfer.ProcessReady();
	}
	for (;;) {
		const ssize_t count = read(pipe.descriptors[0], buffer.data(), buffer.size());
		if (count <= 0) {
			break;
		}
		received.append(buffer.data(), static_cast<std::size_t>(count));
	}

	const std::optional<Scalpel::WaylandTransferResult> result = transfer.TakeResult();
	REQUIRE(result);
	CHECK(result->status == Scalpel::WaylandTransferStatus::Complete);
	CHECK(received == bytes);
}

TEST_CASE("Wayland byte transfer reports peer closure") {
	Pipe pipe;
	(void)close(std::exchange(pipe.descriptors[0], -1));
	Scalpel::WaylandTransfer transfer =
		Scalpel::WaylandTransfer::Write(pipe.TakeWrite(), "text");
	transfer.ProcessReady();

	const std::optional<Scalpel::WaylandTransferResult> result = transfer.TakeResult();
	REQUIRE(result);
	CHECK(result->status == Scalpel::WaylandTransferStatus::Failed);
}

TEST_CASE("Wayland byte transfer cancels and expires against its clock") {
	using namespace std::chrono_literals;
	Scalpel::WaylandTransfer::Clock::time_point now{};
	Pipe cancelledPipe;
	Scalpel::WaylandTransfer cancelled = Scalpel::WaylandTransfer::Read(
		cancelledPipe.TakeRead(), 32, 5s, [&now] { return now; });
	cancelled.Cancel();
	REQUIRE(cancelled.TakeResult()->status == Scalpel::WaylandTransferStatus::Cancelled);

	Pipe timedPipe;
	Scalpel::WaylandTransfer timed = Scalpel::WaylandTransfer::Read(
		timedPipe.TakeRead(), 32, 5s, [&now] { return now; });
	CHECK(timed.TimeUntilDeadline() == 5s);
	now += 5s;
	timed.CheckDeadline();
	REQUIRE(timed.TakeResult()->status == Scalpel::WaylandTransferStatus::TimedOut);
}
