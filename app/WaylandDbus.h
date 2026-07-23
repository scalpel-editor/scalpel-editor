// Session D-Bus ownership and event-loop integration.

#ifndef WAYLANDDBUS_H
#define WAYLANDDBUS_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

struct DBusConnection;
struct DBusTimeout;
struct DBusWatch;

namespace Scalpel {

class WaylandEventLoop;

struct WaylandDbusWatch {
	uintptr_t token = 0;
	int descriptor = -1;
	short events = 0;
	bool enabled = false;

	friend constexpr bool operator==(const WaylandDbusWatch &left,
		const WaylandDbusWatch &right) noexcept {
		return left.token == right.token &&
			left.descriptor == right.descriptor &&
			left.events == right.events &&
			left.enabled == right.enabled;
	}
};

/** Plain D-Bus watch and timeout state driven by libdbus callbacks. */
class WaylandDbusState final {
public:
	using Clock = std::chrono::steady_clock;
	using NowFunction = std::function<Clock::time_point()>;

	explicit WaylandDbusState(NowFunction now = Clock::now);

	void AddWatch(uintptr_t token, int descriptor, short events, bool enabled);
	void UpdateWatch(uintptr_t token, int descriptor, short events, bool enabled);
	void RemoveWatch(uintptr_t token) noexcept;
	[[nodiscard]] bool HasWatch(uintptr_t token) const noexcept;
	[[nodiscard]] std::vector<WaylandDbusWatch> EnabledWatches() const;

	void AddTimeout(uintptr_t token, std::chrono::milliseconds interval,
		bool enabled);
	void UpdateTimeout(uintptr_t token, std::chrono::milliseconds interval,
		bool enabled);
	void RemoveTimeout(uintptr_t token) noexcept;
	[[nodiscard]] bool HasTimeout(uintptr_t token) const noexcept;
	[[nodiscard]] std::optional<std::chrono::milliseconds>
		TimeUntilTimeout() const;
	[[nodiscard]] std::vector<uintptr_t> DueTimeouts() const;
	void RearmTimeout(uintptr_t token);
	void Clear() noexcept;

private:
	struct Timeout {
		uintptr_t token;
		std::chrono::milliseconds interval;
		std::optional<Clock::time_point> deadline;
	};

	[[nodiscard]] std::vector<WaylandDbusWatch>::iterator FindWatch(
		uintptr_t token) noexcept;
	[[nodiscard]] std::vector<WaylandDbusWatch>::const_iterator FindWatch(
		uintptr_t token) const noexcept;
	[[nodiscard]] std::vector<Timeout>::iterator FindTimeout(
		uintptr_t token) noexcept;
	[[nodiscard]] std::vector<Timeout>::const_iterator FindTimeout(
		uintptr_t token) const noexcept;
	[[nodiscard]] std::optional<Clock::time_point> Deadline(
		std::chrono::milliseconds interval, bool enabled) const;

	std::vector<WaylandDbusWatch> watches;
	std::vector<Timeout> timeouts;
	NowFunction now;
};

/**
 * Owns the optional private session-bus connection used by later portal work.
 *
 * The connection is created only when requested. Once active, its watches and
 * timeouts join the same poll operation as Wayland and selection transfers.
 */
class WaylandDbus final {
public:
	using Clock = WaylandDbusState::Clock;
	using NowFunction = WaylandDbusState::NowFunction;

	explicit WaylandDbus(NowFunction now = Clock::now);
	~WaylandDbus() noexcept;

	WaylandDbus(const WaylandDbus &) = delete;
	WaylandDbus &operator=(const WaylandDbus &) = delete;

	[[nodiscard]] DBusConnection *ConnectSessionBus();
	[[nodiscard]] DBusConnection *Connection() const noexcept {
		return connection;
	}
	void AddPollSources(WaylandEventLoop &eventLoop);
	[[nodiscard]] std::optional<std::chrono::milliseconds>
		TimeUntilTimeout() const;
	void ProcessEvents();

private:
	void Disconnect() noexcept;
	void HandleWatch(uintptr_t token, short revents);

	static unsigned int AddWatch(DBusWatch *watch, void *data) noexcept;
	static void RemoveWatch(DBusWatch *watch, void *data) noexcept;
	static void ToggleWatch(DBusWatch *watch, void *data) noexcept;
	static unsigned int AddTimeout(DBusTimeout *timeout, void *data) noexcept;
	static void RemoveTimeout(DBusTimeout *timeout, void *data) noexcept;
	static void ToggleTimeout(DBusTimeout *timeout, void *data) noexcept;

	DBusConnection *connection = nullptr;
	WaylandDbusState state;
};

}

#endif
