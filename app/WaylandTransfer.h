// Nonblocking byte transfers used by Wayland selections.

#ifndef WAYLANDTRANSFER_H
#define WAYLANDTRANSFER_H

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>

namespace Scalpel {

enum class WaylandTransferDirection {
	Read,
	Write,
};

enum class WaylandTransferStatus {
	Complete,
	Cancelled,
	Failed,
	TooLarge,
	TimedOut,
};

struct WaylandTransferResult {
	WaylandTransferStatus status;
	std::string bytes;
};

/**
 * Owns one nonblocking descriptor until a transfer finishes or is cancelled.
 *
 * ProcessReady may be called after poll reports the desired direction or after
 * a deadline. Completion closes the descriptor and leaves one result for the
 * owner to take.
 */
class WaylandTransfer final {
public:
	using Clock = std::chrono::steady_clock;
	using NowFunction = std::function<Clock::time_point()>;

	static constexpr std::size_t DefaultMaximumBytes = 16U * 1024U * 1024U;
	static constexpr std::chrono::seconds DefaultTimeout{5};

	static WaylandTransfer Read(int descriptor,
		std::size_t maximumBytes = DefaultMaximumBytes,
		std::chrono::milliseconds timeout = DefaultTimeout,
		NowFunction now = Clock::now);
	static WaylandTransfer Write(int descriptor, std::string bytes,
		std::chrono::milliseconds timeout = DefaultTimeout,
		NowFunction now = Clock::now);

	~WaylandTransfer() noexcept;

	WaylandTransfer(const WaylandTransfer &) = delete;
	WaylandTransfer &operator=(const WaylandTransfer &) = delete;
	WaylandTransfer(WaylandTransfer &&other) noexcept;
	WaylandTransfer &operator=(WaylandTransfer &&other) noexcept;

	[[nodiscard]] int Descriptor() const noexcept { return descriptor; }
	[[nodiscard]] WaylandTransferDirection Direction() const noexcept { return direction; }
	[[nodiscard]] bool Pending() const noexcept { return descriptor >= 0; }
	[[nodiscard]] std::optional<std::chrono::milliseconds> TimeUntilDeadline() const;

	void ProcessReady();
	void CheckDeadline();
	void Cancel();
	[[nodiscard]] std::optional<WaylandTransferResult> TakeResult();

private:
	WaylandTransfer(int descriptor, WaylandTransferDirection direction,
		std::string bytes, std::size_t maximumBytes,
		std::chrono::milliseconds timeout, NowFunction now);

	void ProcessRead();
	void ProcessWrite();
	void Finish(WaylandTransferStatus status);
	void Close() noexcept;

	int descriptor = -1;
	WaylandTransferDirection direction = WaylandTransferDirection::Read;
	std::string bytes;
	std::size_t offset = 0;
	std::size_t maximumBytes = DefaultMaximumBytes;
	Clock::time_point deadline;
	NowFunction now;
	std::optional<WaylandTransferResult> result;
};

}

#endif
