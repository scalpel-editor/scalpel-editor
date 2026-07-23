#include "WaylandDbus.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include <poll.h>
#include <dbus/dbus.h>

#include "WaylandEventLoop.h"

namespace Scalpel {

namespace {

short WatchEvents(unsigned int flags) noexcept {
	short events = 0;
	if (flags & DBUS_WATCH_READABLE) {
		events |= POLLIN;
	}
	if (flags & DBUS_WATCH_WRITABLE) {
		events |= POLLOUT;
	}
	return events;
}

unsigned int WatchReadyFlags(short revents) noexcept {
	unsigned int flags = 0;
	if (revents & POLLIN) {
		flags |= DBUS_WATCH_READABLE;
	}
	if (revents & POLLOUT) {
		flags |= DBUS_WATCH_WRITABLE;
	}
	if (revents & (POLLERR | POLLNVAL)) {
		flags |= DBUS_WATCH_ERROR;
	}
	if (revents & POLLHUP) {
		flags |= DBUS_WATCH_HANGUP;
	}
	return flags;
}

uintptr_t Token(const void *pointer) noexcept {
	return reinterpret_cast<uintptr_t>(pointer);
}

DBusWatch *Watch(uintptr_t token) noexcept {
	return reinterpret_cast<DBusWatch *>(token);
}

DBusTimeout *Timeout(uintptr_t token) noexcept {
	return reinterpret_cast<DBusTimeout *>(token);
}

}

WaylandDbusState::WaylandDbusState(NowFunction now_) : now(std::move(now_)) {
	if (!now) {
		throw std::invalid_argument("Wayland D-Bus state requires a clock");
	}
}

void WaylandDbusState::AddWatch(
	uintptr_t token, int descriptor, short events, bool enabled) {
	if (token == 0 || descriptor < 0) {
		throw std::invalid_argument("Invalid Wayland D-Bus watch");
	}
	if (HasWatch(token)) {
		throw std::invalid_argument("Duplicate Wayland D-Bus watch");
	}
	watches.push_back({token, descriptor, events, enabled});
}

void WaylandDbusState::UpdateWatch(
	uintptr_t token, int descriptor, short events, bool enabled) {
	const auto found = FindWatch(token);
	if (found == watches.end()) {
		return;
	}
	if (descriptor < 0) {
		throw std::invalid_argument("Invalid Wayland D-Bus watch update");
	}
	found->descriptor = descriptor;
	found->events = events;
	found->enabled = enabled;
}

void WaylandDbusState::RemoveWatch(uintptr_t token) noexcept {
	const auto found = FindWatch(token);
	if (found != watches.end()) {
		watches.erase(found);
	}
}

bool WaylandDbusState::HasWatch(uintptr_t token) const noexcept {
	return FindWatch(token) != watches.end();
}

std::vector<WaylandDbusWatch> WaylandDbusState::EnabledWatches() const {
	std::vector<WaylandDbusWatch> enabled;
	for (const WaylandDbusWatch &watch : watches) {
		if (watch.enabled && watch.events != 0) {
			enabled.push_back(watch);
		}
	}
	return enabled;
}

void WaylandDbusState::AddTimeout(uintptr_t token,
	std::chrono::milliseconds interval, bool enabled) {
	if (token == 0 || interval.count() < 0) {
		throw std::invalid_argument("Invalid Wayland D-Bus timeout");
	}
	if (HasTimeout(token)) {
		throw std::invalid_argument("Duplicate Wayland D-Bus timeout");
	}
	timeouts.push_back({token, interval, Deadline(interval, enabled)});
}

void WaylandDbusState::UpdateTimeout(uintptr_t token,
	std::chrono::milliseconds interval, bool enabled) {
	const auto found = FindTimeout(token);
	if (found == timeouts.end()) {
		return;
	}
	if (interval.count() < 0) {
		throw std::invalid_argument("Invalid Wayland D-Bus timeout update");
	}
	found->interval = interval;
	found->deadline = Deadline(interval, enabled);
}

void WaylandDbusState::RemoveTimeout(uintptr_t token) noexcept {
	const auto found = FindTimeout(token);
	if (found != timeouts.end()) {
		timeouts.erase(found);
	}
}

bool WaylandDbusState::HasTimeout(uintptr_t token) const noexcept {
	return FindTimeout(token) != timeouts.end();
}

std::optional<std::chrono::milliseconds>
WaylandDbusState::TimeUntilTimeout() const {
	std::optional<Clock::time_point> soonest;
	for (const Timeout &timeout : timeouts) {
		if (timeout.deadline &&
			(!soonest || *timeout.deadline < *soonest)) {
			soonest = timeout.deadline;
		}
	}
	if (!soonest) {
		return std::nullopt;
	}
	return std::chrono::ceil<std::chrono::milliseconds>(
		std::max(Clock::duration::zero(), *soonest - now()));
}

std::vector<uintptr_t> WaylandDbusState::DueTimeouts() const {
	const Clock::time_point current = now();
	std::vector<uintptr_t> due;
	for (const Timeout &timeout : timeouts) {
		if (timeout.deadline && current >= *timeout.deadline) {
			due.push_back(timeout.token);
		}
	}
	return due;
}

void WaylandDbusState::RearmTimeout(uintptr_t token) {
	const auto found = FindTimeout(token);
	if (found != timeouts.end() && found->deadline) {
		found->deadline = now() + found->interval;
	}
}

void WaylandDbusState::Clear() noexcept {
	watches.clear();
	timeouts.clear();
}

std::vector<WaylandDbusWatch>::iterator
WaylandDbusState::FindWatch(uintptr_t token) noexcept {
	return std::find_if(watches.begin(), watches.end(),
		[token](const WaylandDbusWatch &watch) {
			return watch.token == token;
		});
}

std::vector<WaylandDbusWatch>::const_iterator
WaylandDbusState::FindWatch(uintptr_t token) const noexcept {
	return std::find_if(watches.begin(), watches.end(),
		[token](const WaylandDbusWatch &watch) {
			return watch.token == token;
		});
}

std::vector<WaylandDbusState::Timeout>::iterator
WaylandDbusState::FindTimeout(uintptr_t token) noexcept {
	return std::find_if(timeouts.begin(), timeouts.end(),
		[token](const Timeout &timeout) {
			return timeout.token == token;
		});
}

std::vector<WaylandDbusState::Timeout>::const_iterator
WaylandDbusState::FindTimeout(uintptr_t token) const noexcept {
	return std::find_if(timeouts.begin(), timeouts.end(),
		[token](const Timeout &timeout) {
			return timeout.token == token;
		});
}

std::optional<WaylandDbusState::Clock::time_point>
WaylandDbusState::Deadline(
	std::chrono::milliseconds interval, bool enabled) const {
	return enabled ?
		std::optional<Clock::time_point>{now() + interval} :
		std::nullopt;
}

WaylandDbus::WaylandDbus(NowFunction now) : state(std::move(now)) {
}

WaylandDbus::~WaylandDbus() noexcept {
	Disconnect();
}

DBusConnection *WaylandDbus::ConnectSessionBus() {
	if (connection) {
		return connection;
	}
	DBusError error = DBUS_ERROR_INIT;
	DBusConnection *candidate = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
	if (!candidate) {
		if (dbus_error_is_set(&error)) {
			dbus_error_free(&error);
		}
		return nullptr;
	}
	dbus_connection_set_exit_on_disconnect(candidate, FALSE);
	if (!dbus_connection_set_watch_functions(candidate,
			&WaylandDbus::AddWatch,
			&WaylandDbus::RemoveWatch,
			&WaylandDbus::ToggleWatch,
			this, nullptr) ||
		!dbus_connection_set_timeout_functions(candidate,
			&WaylandDbus::AddTimeout,
			&WaylandDbus::RemoveTimeout,
			&WaylandDbus::ToggleTimeout,
			this, nullptr)) {
		dbus_connection_set_watch_functions(
			candidate, nullptr, nullptr, nullptr, nullptr, nullptr);
		dbus_connection_set_timeout_functions(
			candidate, nullptr, nullptr, nullptr, nullptr, nullptr);
		dbus_connection_close(candidate);
		dbus_connection_unref(candidate);
		state.Clear();
		return nullptr;
	}
	connection = candidate;
	return connection;
}

void WaylandDbus::AddPollSources(WaylandEventLoop &eventLoop) {
	for (const WaylandDbusWatch &watch : state.EnabledWatches()) {
		eventLoop.AddSource(watch.descriptor, watch.events,
			[this, token = watch.token](short revents) {
				HandleWatch(token, revents);
			});
	}
}

std::optional<std::chrono::milliseconds>
WaylandDbus::TimeUntilTimeout() const {
	return state.TimeUntilTimeout();
}

void WaylandDbus::ProcessEvents() {
	for (const uintptr_t token : state.DueTimeouts()) {
		if (!state.HasTimeout(token)) {
			continue;
		}
		(void)dbus_timeout_handle(Timeout(token));
		if (state.HasTimeout(token)) {
			DBusTimeout *timeout = Timeout(token);
			state.UpdateTimeout(token,
				std::chrono::milliseconds(dbus_timeout_get_interval(timeout)),
				dbus_timeout_get_enabled(timeout));
		}
	}
	if (!connection) {
		return;
	}
	while (dbus_connection_dispatch(connection) ==
		DBUS_DISPATCH_DATA_REMAINS) {
	}
}

void WaylandDbus::Disconnect() noexcept {
	if (!connection) {
		return;
	}
	dbus_connection_set_watch_functions(
		connection, nullptr, nullptr, nullptr, nullptr, nullptr);
	dbus_connection_set_timeout_functions(
		connection, nullptr, nullptr, nullptr, nullptr, nullptr);
	dbus_connection_close(connection);
	dbus_connection_unref(connection);
	connection = nullptr;
	state.Clear();
}

void WaylandDbus::HandleWatch(uintptr_t token, short revents) {
	if (!state.HasWatch(token)) {
		return;
	}
	const unsigned int flags = WatchReadyFlags(revents);
	if (flags != 0) {
		(void)dbus_watch_handle(Watch(token), flags);
	}
}

unsigned int WaylandDbus::AddWatch(DBusWatch *watch, void *data) noexcept {
	try {
		auto &dbus = *static_cast<WaylandDbus *>(data);
		dbus.state.AddWatch(Token(watch), dbus_watch_get_unix_fd(watch),
			WatchEvents(dbus_watch_get_flags(watch)),
			dbus_watch_get_enabled(watch));
		return TRUE;
	} catch (...) {
		return FALSE;
	}
}

void WaylandDbus::RemoveWatch(DBusWatch *watch, void *data) noexcept {
	auto &dbus = *static_cast<WaylandDbus *>(data);
	dbus.state.RemoveWatch(Token(watch));
}

void WaylandDbus::ToggleWatch(DBusWatch *watch, void *data) noexcept {
	try {
		auto &dbus = *static_cast<WaylandDbus *>(data);
		dbus.state.UpdateWatch(Token(watch), dbus_watch_get_unix_fd(watch),
			WatchEvents(dbus_watch_get_flags(watch)),
			dbus_watch_get_enabled(watch));
	} catch (...) {
	}
}

unsigned int WaylandDbus::AddTimeout(DBusTimeout *timeout, void *data) noexcept {
	try {
		auto &dbus = *static_cast<WaylandDbus *>(data);
		dbus.state.AddTimeout(Token(timeout),
			std::chrono::milliseconds(dbus_timeout_get_interval(timeout)),
			dbus_timeout_get_enabled(timeout));
		return TRUE;
	} catch (...) {
		return FALSE;
	}
}

void WaylandDbus::RemoveTimeout(DBusTimeout *timeout, void *data) noexcept {
	auto &dbus = *static_cast<WaylandDbus *>(data);
	dbus.state.RemoveTimeout(Token(timeout));
}

void WaylandDbus::ToggleTimeout(DBusTimeout *timeout, void *data) noexcept {
	try {
		auto &dbus = *static_cast<WaylandDbus *>(data);
		dbus.state.UpdateTimeout(Token(timeout),
			std::chrono::milliseconds(dbus_timeout_get_interval(timeout)),
			dbus_timeout_get_enabled(timeout));
	} catch (...) {
	}
}

}
