#include <array>
#include <chrono>
#include <string>

#include <fcntl.h>
#include <unistd.h>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "WaylandClipboard.h"
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

TEST_CASE("Wayland clipboard state prefers explicit UTF-8 offers") {
	Scalpel::WaylandClipboardState clipboard;
	clipboard.SetManagerAvailable(true);
	clipboard.SetSeatAvailable(true);
	clipboard.AddOffer(10);
	clipboard.OfferMime(10, "image/png");
	clipboard.OfferMime(10, "text/plain");
	clipboard.OfferMime(10, "UTF8_STRING");
	clipboard.OfferMime(10, "text/plain;charset=UTF-8");
	clipboard.SelectOffer(10);

	const Scalpel::WaylandClipboardPasteChoice choice = clipboard.ChoosePaste();
	CHECK(choice.kind == Scalpel::WaylandClipboardPasteChoice::Kind::Receive);
	CHECK(choice.mimeType == "text/plain;charset=UTF-8");
	CHECK(clipboard.CanPaste());

	clipboard.AddOffer(11);
	clipboard.SelectOffer(11);
	CHECK_FALSE(clipboard.CanPaste());
	CHECK(clipboard.ChoosePaste().kind ==
		Scalpel::WaylandClipboardPasteChoice::Kind::NoText);
}

TEST_CASE("Wayland clipboard state requires a service seat and serial to publish") {
	Scalpel::WaylandClipboardState clipboard;
	CHECK_FALSE(clipboard.Publish("unavailable"));
	clipboard.SetManagerAvailable(true);
	CHECK_FALSE(clipboard.Publish("no seat"));
	clipboard.SetSeatAvailable(true);
	CHECK_FALSE(clipboard.Publish("no serial"));
	clipboard.RecordSerial(42);
	REQUIRE(clipboard.Publish("owned text"));
	CHECK(clipboard.CanPaste());
	CHECK(clipboard.ChoosePaste().text == "owned text");

	clipboard.AddOffer(10);
	clipboard.OfferMime(10, "text/plain");
	clipboard.SelectOffer(10);
	CHECK(clipboard.ChoosePaste().kind ==
		Scalpel::WaylandClipboardPasteChoice::Kind::OwnedText);
	CHECK(clipboard.ChoosePaste().text == "owned text");

	clipboard.CancelOwnership();
	CHECK(clipboard.ChoosePaste().kind ==
		Scalpel::WaylandClipboardPasteChoice::Kind::Receive);
	CHECK(clipboard.ChoosePaste().mimeType == "text/plain");

	clipboard.SetSeatAvailable(false);
	CHECK_FALSE(clipboard.CanPaste());
	CHECK_FALSE(clipboard.Serial().has_value());
	clipboard.SetSeatAvailable(true);
	CHECK_FALSE(clipboard.Publish("stale serial"));
}

TEST_CASE("Wayland clipboard validates complete UTF-8 text") {
	CHECK(Scalpel::IsValidClipboardUtf8(""));
	CHECK(Scalpel::IsValidClipboardUtf8("plain"));
	CHECK(Scalpel::IsValidClipboardUtf8("\xE2\x98\x83"));
	CHECK_FALSE(Scalpel::IsValidClipboardUtf8("\xC0\xAF"));
	CHECK_FALSE(Scalpel::IsValidClipboardUtf8("\xE2\x98"));
	CHECK_FALSE(Scalpel::IsValidClipboardUtf8("\xED\xA0\x80"));
}

TEST_CASE("Wayland clipboard reports unavailable operations without protocol objects") {
	Scalpel::WaylandClipboard clipboard;
	clipboard.CopyText(7, "text");
	clipboard.PasteText(8);
	const std::vector<Scalpel::ClipboardResult> results = clipboard.TakeResults();
	REQUIRE(results.size() == 2);
	CHECK(results[0].request == 7);
	CHECK(results[0].status == Scalpel::ClipboardResultStatus::Unavailable);
	CHECK(results[1].request == 8);
	CHECK(results[1].status == Scalpel::ClipboardResultStatus::Unavailable);
}
