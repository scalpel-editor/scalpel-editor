// Poll planning for the single-threaded Wayland application loop.

#ifndef WAYLANDEVENTLOOP_H
#define WAYLANDEVENTLOOP_H

#include <chrono>
#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include <poll.h>

namespace Scalpel {

enum class WaylandPollOutcome {
	Ready,
	TimedOut,
	Interrupted,
};

/**
 * Builds one immutable poll snapshot from the application's active sources.
 *
 * Ready callbacks are kept with their descriptors so a concern does not need
 * to recover its result from a shared positional descriptor range.
 */
class WaylandEventLoop final {
public:
	using ReadyFunction = std::function<void(short)>;

	explicit WaylandEventLoop(bool recoveringBlockedFlush = false) noexcept;

	std::size_t AddSource(int descriptor, short events,
		ReadyFunction ready = {});
	void AddDeadline(std::optional<std::chrono::milliseconds> deadline) noexcept;
	void AddEditorDeadline(
		std::optional<std::chrono::milliseconds> deadline) noexcept;

	[[nodiscard]] int TimeoutMilliseconds() const noexcept;
	[[nodiscard]] std::vector<pollfd> PollDescriptors() const;
	void DispatchReady(const std::vector<pollfd> &descriptors) const;

	[[nodiscard]] static WaylandPollOutcome InterpretPollResult(
		int result, int error);

private:
	struct Source {
		int descriptor;
		short events;
		ReadyFunction ready;
	};

	bool recoveringBlockedFlush = false;
	std::vector<Source> sources;
	std::optional<std::chrono::milliseconds> shortestDeadline;
};

}

#endif
