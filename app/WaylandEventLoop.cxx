#include "WaylandEventLoop.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace Scalpel {

WaylandEventLoop::WaylandEventLoop(bool recoveringBlockedFlush_) noexcept :
	recoveringBlockedFlush(recoveringBlockedFlush_) {
}

std::size_t WaylandEventLoop::AddSource(
	int descriptor, short events, ReadyFunction ready) {
	if (descriptor < 0) {
		throw std::invalid_argument(
			"Wayland event-loop descriptor must be nonnegative");
	}
	if (events == 0) {
		throw std::invalid_argument(
			"Wayland event-loop source requires poll interest");
	}
	sources.push_back({descriptor, events, std::move(ready)});
	return sources.size() - 1;
}

void WaylandEventLoop::AddDeadline(
	std::optional<std::chrono::milliseconds> deadline) noexcept {
	if (deadline && (!shortestDeadline || *deadline < *shortestDeadline)) {
		shortestDeadline = deadline;
	}
}

void WaylandEventLoop::AddEditorDeadline(
	std::optional<std::chrono::milliseconds> deadline) noexcept {
	if (!recoveringBlockedFlush) {
		AddDeadline(deadline);
	}
}

int WaylandEventLoop::TimeoutMilliseconds() const noexcept {
	return shortestDeadline ?
		static_cast<int>(std::clamp<int64_t>(
			shortestDeadline->count(), 0, INT_MAX)) :
		-1;
}

std::vector<pollfd> WaylandEventLoop::PollDescriptors() const {
	std::vector<pollfd> descriptors;
	descriptors.reserve(sources.size());
	for (const Source &source : sources) {
		descriptors.push_back({source.descriptor, source.events, 0});
	}
	return descriptors;
}

void WaylandEventLoop::DispatchReady(
	const std::vector<pollfd> &descriptors) const {
	if (descriptors.size() != sources.size()) {
		throw std::invalid_argument(
			"Wayland event-loop poll result count changed");
	}
	for (std::size_t index = 0; index < sources.size(); ++index) {
		const Source &source = sources[index];
		const pollfd &descriptor = descriptors[index];
		if (descriptor.fd != source.descriptor) {
			throw std::invalid_argument(
				"Wayland event-loop poll source order changed");
		}
		if (descriptor.revents != 0 && source.ready) {
			source.ready(descriptor.revents);
		}
	}
}

WaylandPollOutcome WaylandEventLoop::InterpretPollResult(
	int result, int error) {
	if (result > 0) {
		return WaylandPollOutcome::Ready;
	}
	if (result == 0) {
		return WaylandPollOutcome::TimedOut;
	}
	if (error == EINTR) {
		return WaylandPollOutcome::Interrupted;
	}
	throw std::system_error(error, std::generic_category(),
		"Wayland display poll failed");
}

}
