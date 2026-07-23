#include "WaylandTransfer.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <stdexcept>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

namespace Scalpel {

namespace {

void SetCloseOnExecAndNonblocking(int descriptor) {
	const int descriptorFlags = fcntl(descriptor, F_GETFD);
	if (descriptorFlags < 0 ||
		fcntl(descriptor, F_SETFD, descriptorFlags | FD_CLOEXEC) < 0) {
		throw std::runtime_error("could not make transfer descriptor close-on-exec");
	}
	const int statusFlags = fcntl(descriptor, F_GETFL);
	if (statusFlags < 0 ||
		fcntl(descriptor, F_SETFL, statusFlags | O_NONBLOCK) < 0) {
		throw std::runtime_error("could not make transfer descriptor nonblocking");
	}
}

ssize_t WriteWithoutSigpipe(int descriptor, const char *bytes, std::size_t length) {
	sigset_t sigpipeSet;
	sigset_t oldSignalMask;
	sigset_t pendingSignals;
	sigemptyset(&sigpipeSet);
	sigaddset(&sigpipeSet, SIGPIPE);
	const bool hadPendingSigpipe =
		sigpending(&pendingSignals) == 0 && sigismember(&pendingSignals, SIGPIPE) == 1;
	const bool blockedSigpipe =
		sigprocmask(SIG_BLOCK, &sigpipeSet, &oldSignalMask) == 0;

	const ssize_t written = write(descriptor, bytes, length);

	if (blockedSigpipe) {
		if (!hadPendingSigpipe) {
			const timespec noWait{};
			(void)sigtimedwait(&sigpipeSet, nullptr, &noWait);
		}
		(void)sigprocmask(SIG_SETMASK, &oldSignalMask, nullptr);
	}
	return written;
}

}

WaylandTransfer WaylandTransfer::Read(int descriptor, std::size_t maximumBytes,
	std::chrono::milliseconds timeout, NowFunction now) {
	return WaylandTransfer(descriptor, WaylandTransferDirection::Read, {},
		maximumBytes, timeout, std::move(now));
}

WaylandTransfer WaylandTransfer::Write(int descriptor, std::string bytes,
	std::chrono::milliseconds timeout, NowFunction now) {
	return WaylandTransfer(descriptor, WaylandTransferDirection::Write,
		std::move(bytes), 0, timeout, std::move(now));
}

WaylandTransfer::WaylandTransfer(int descriptor_, WaylandTransferDirection direction_,
	std::string bytes_, std::size_t maximumBytes_, std::chrono::milliseconds timeout,
	NowFunction now_) :
	descriptor(descriptor_),
	direction(direction_),
	bytes(std::move(bytes_)),
	maximumBytes(maximumBytes_),
	now(std::move(now_)) {
	if (descriptor < 0) {
		throw std::invalid_argument("WaylandTransfer requires a descriptor");
	}
	if (!now) {
		throw std::invalid_argument("WaylandTransfer requires a clock");
	}
	if (timeout < std::chrono::milliseconds::zero()) {
		throw std::invalid_argument("WaylandTransfer requires a nonnegative timeout");
	}
	if (direction == WaylandTransferDirection::Read && maximumBytes == 0) {
		throw std::invalid_argument("WaylandTransfer requires a positive read limit");
	}
	try {
		SetCloseOnExecAndNonblocking(descriptor);
	} catch (...) {
		Close();
		throw;
	}
	deadline = now() + timeout;
	if (direction == WaylandTransferDirection::Write && bytes.empty()) {
		Finish(WaylandTransferStatus::Complete);
	}
}

WaylandTransfer::~WaylandTransfer() noexcept {
	Close();
}

WaylandTransfer::WaylandTransfer(WaylandTransfer &&other) noexcept :
	descriptor(std::exchange(other.descriptor, -1)),
	direction(other.direction),
	bytes(std::move(other.bytes)),
	offset(other.offset),
	maximumBytes(other.maximumBytes),
	deadline(other.deadline),
	now(std::move(other.now)),
	result(std::move(other.result)) {
}

WaylandTransfer &WaylandTransfer::operator=(WaylandTransfer &&other) noexcept {
	if (this != &other) {
		Close();
		descriptor = std::exchange(other.descriptor, -1);
		direction = other.direction;
		bytes = std::move(other.bytes);
		offset = other.offset;
		maximumBytes = other.maximumBytes;
		deadline = other.deadline;
		now = std::move(other.now);
		result = std::move(other.result);
	}
	return *this;
}

std::optional<std::chrono::milliseconds> WaylandTransfer::TimeUntilDeadline() const {
	if (!Pending()) {
		return std::nullopt;
	}
	return std::chrono::ceil<std::chrono::milliseconds>(
		std::max(Clock::duration::zero(), deadline - now()));
}

void WaylandTransfer::ProcessReady() {
	if (!Pending()) {
		return;
	}
	if (now() >= deadline) {
		Finish(WaylandTransferStatus::TimedOut);
		return;
	}
	if (direction == WaylandTransferDirection::Read) {
		ProcessRead();
	} else {
		ProcessWrite();
	}
}

void WaylandTransfer::CheckDeadline() {
	if (Pending() && now() >= deadline) {
		Finish(WaylandTransferStatus::TimedOut);
	}
}

void WaylandTransfer::Cancel() {
	if (Pending()) {
		Finish(WaylandTransferStatus::Cancelled);
	}
}

std::optional<WaylandTransferResult> WaylandTransfer::TakeResult() {
	return std::exchange(result, std::nullopt);
}

void WaylandTransfer::ProcessRead() {
	char buffer[4096];
	for (;;) {
		const ssize_t count = read(descriptor, buffer, sizeof(buffer));
		if (count > 0) {
			if (bytes.size() + static_cast<std::size_t>(count) > maximumBytes) {
				Finish(WaylandTransferStatus::TooLarge);
				return;
			}
			bytes.append(buffer, static_cast<std::size_t>(count));
		} else if (count == 0) {
			Finish(WaylandTransferStatus::Complete);
			return;
		} else if (errno == EINTR) {
			continue;
		} else if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return;
		} else {
			Finish(WaylandTransferStatus::Failed);
			return;
		}
	}
}

void WaylandTransfer::ProcessWrite() {
	while (offset < bytes.size()) {
		const ssize_t count = WriteWithoutSigpipe(
			descriptor, bytes.data() + offset, bytes.size() - offset);
		if (count > 0) {
			offset += static_cast<std::size_t>(count);
		} else if (count < 0 && errno == EINTR) {
			continue;
		} else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			return;
		} else {
			Finish(WaylandTransferStatus::Failed);
			return;
		}
	}
	Finish(WaylandTransferStatus::Complete);
}

void WaylandTransfer::Finish(WaylandTransferStatus status) {
	Close();
	result = WaylandTransferResult{status,
		status == WaylandTransferStatus::Complete ? std::move(bytes) : std::string{}};
}

void WaylandTransfer::Close() noexcept {
	if (descriptor >= 0) {
		(void)close(descriptor);
		descriptor = -1;
	}
}

}
