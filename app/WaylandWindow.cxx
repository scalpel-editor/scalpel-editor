// Minimal xdg-shell toplevel for the phase 6 vertical slice.
//
// The connection, configure handshake, and cleanup order were informed by
// OnlyWayUi's Wayland backend. Its MIT notice is retained in seed/LICENSE.txt.

#include "WaylandWindow.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>

#include <sys/mman.h>
#include <poll.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "xdg-shell-client-protocol.h"

namespace Scalpel {

const wl_registry_listener WaylandWindow::registryListener = {
	WaylandWindow::RegistryGlobal,
	WaylandWindow::RegistryGlobalRemove,
};

const wl_seat_listener WaylandWindow::seatListener = {
	WaylandWindow::SeatCapabilities,
	WaylandWindow::SeatName,
};

const wl_keyboard_listener WaylandWindow::keyboardListener = {
	WaylandWindow::KeyboardKeymap,
	WaylandWindow::KeyboardEnter,
	WaylandWindow::KeyboardLeave,
	WaylandWindow::KeyboardKey,
	WaylandWindow::KeyboardModifiers,
	WaylandWindow::KeyboardRepeatInfo,
};

const wl_pointer_listener WaylandWindow::pointerListener = {
	WaylandWindow::PointerEnter,
	WaylandWindow::PointerLeave,
	WaylandWindow::PointerMotion,
	WaylandWindow::PointerButton,
	WaylandWindow::PointerAxis,
	WaylandWindow::PointerFrame,
	WaylandWindow::PointerAxisSource,
	WaylandWindow::PointerAxisStop,
	WaylandWindow::PointerAxisDiscrete,
	WaylandWindow::PointerAxisValue120,
	WaylandWindow::PointerAxisRelativeDirection,
};

const wl_output_listener WaylandWindow::outputListener = {
	WaylandWindow::OutputGeometry,
	WaylandWindow::OutputMode,
	WaylandWindow::OutputDone,
	WaylandWindow::OutputScale,
	WaylandWindow::OutputName,
	WaylandWindow::OutputDescription,
};

const wl_surface_listener WaylandWindow::waylandSurfaceListener = {
	WaylandWindow::WaylandSurfaceEnter,
	WaylandWindow::WaylandSurfaceLeave,
	WaylandWindow::WaylandSurfacePreferredBufferScale,
	WaylandWindow::WaylandSurfacePreferredBufferTransform,
};

const xdg_wm_base_listener WaylandWindow::wmBaseListener = {
	WaylandWindow::WmBasePing,
};

const xdg_surface_listener WaylandWindow::surfaceListener = {
	WaylandWindow::SurfaceConfigure,
};

const xdg_toplevel_listener WaylandWindow::toplevelListener = {
	WaylandWindow::ToplevelConfigure,
	WaylandWindow::ToplevelClose,
	WaylandWindow::ToplevelConfigureBounds,
	WaylandWindow::ToplevelWmCapabilities,
};

WaylandWindow::WaylandWindow(const char *title, int width_, int height_) :
	lifecycle(width_, height_) {
	if (!title) {
		throw std::invalid_argument("WaylandWindow requires a title");
	}
	try {
		Initialise(title);
	} catch (...) {
		Destroy();
		throw;
	}
}

WaylandWindow::~WaylandWindow() noexcept {
	Destroy();
}

void WaylandWindow::Initialise(const char *title) {
	display = wl_display_connect(nullptr);
	if (!display) {
		throw std::runtime_error("could not connect to the Wayland display");
	}

	registry = wl_display_get_registry(display);
	if (!registry || wl_registry_add_listener(registry, &registryListener, this) != 0) {
		throw std::runtime_error("could not listen for Wayland globals");
	}
	RoundTrip();
	if (CallbackFailed()) {
		throw std::runtime_error("could not initialise Wayland globals");
	}

	if (!compositor || !wmBase) {
		throw std::runtime_error("Wayland compositor and xdg_wm_base are required");
	}
	if (xdg_wm_base_add_listener(wmBase, &wmBaseListener, this) != 0) {
		throw std::runtime_error("could not listen for xdg_wm_base events");
	}

	surface = wl_compositor_create_surface(compositor);
	if (!surface) {
		throw std::runtime_error("could not create the Wayland surface");
	}
	if (wl_surface_add_listener(surface, &waylandSurfaceListener, this) != 0) {
		throw std::runtime_error("could not listen for Wayland surface output changes");
	}
	shellSurface = xdg_wm_base_get_xdg_surface(wmBase, surface);
	if (!shellSurface || xdg_surface_add_listener(shellSurface, &surfaceListener, this) != 0) {
		throw std::runtime_error("could not create the xdg_surface");
	}
	toplevel = xdg_surface_get_toplevel(shellSurface);
	if (!toplevel || xdg_toplevel_add_listener(toplevel, &toplevelListener, this) != 0) {
		throw std::runtime_error("could not create the xdg_toplevel");
	}
	xdg_toplevel_set_title(toplevel, title);
	xdg_toplevel_set_app_id(toplevel, "scalpel-editor");

	// xdg-shell forbids attaching a buffer before this empty initial commit
	// has been answered with xdg_surface.configure and acknowledged.
	wl_surface_commit(surface);
	DispatchUntilConfigured();
	if (CallbackFailed()) {
		throw std::runtime_error("could not initialise Wayland listeners");
	}
	eglWindow = wl_egl_window_create(surface, Width(), Height());
	if (!eglWindow) {
		throw std::runtime_error("could not create the Wayland EGL window");
	}
	(void)lifecycle.TakeResize();
}

void WaylandWindow::Destroy() noexcept {
	if (eglWindow) {
		wl_egl_window_destroy(eglWindow);
	}
	if (toplevel) {
		xdg_toplevel_destroy(toplevel);
	}
	if (shellSurface) {
		xdg_surface_destroy(shellSurface);
	}
	if (surface) {
		wl_surface_destroy(surface);
	}
	for (const Output &output : outputs) {
		if (wl_output_get_version(output.proxy) >= WL_OUTPUT_RELEASE_SINCE_VERSION) {
			wl_output_release(output.proxy);
		} else {
			wl_output_destroy(output.proxy);
		}
	}
	outputs.clear();
	if (keyboard) {
		if (wl_keyboard_get_version(keyboard) >= WL_KEYBOARD_RELEASE_SINCE_VERSION) {
			wl_keyboard_release(keyboard);
		} else {
			wl_keyboard_destroy(keyboard);
		}
	}
	if (pointer) {
		if (wl_pointer_get_version(pointer) >= WL_POINTER_RELEASE_SINCE_VERSION) {
			wl_pointer_release(pointer);
		} else {
			wl_pointer_destroy(pointer);
		}
	}
	if (seat) {
		if (wl_seat_get_version(seat) >= WL_SEAT_RELEASE_SINCE_VERSION) {
			wl_seat_release(seat);
		} else {
			wl_seat_destroy(seat);
		}
	}
	if (wmBase) {
		xdg_wm_base_destroy(wmBase);
	}
	if (compositor) {
		wl_compositor_destroy(compositor);
	}
	if (registry) {
		wl_registry_destroy(registry);
	}
	if (display) {
		wl_display_disconnect(display);
	}
	toplevel = nullptr;
	eglWindow = nullptr;
	shellSurface = nullptr;
	surface = nullptr;
	keyboard = nullptr;
	pointer = nullptr;
	seat = nullptr;
	wmBase = nullptr;
	compositor = nullptr;
	registry = nullptr;
	display = nullptr;
}

void WaylandWindow::ApplyLifecycleActions(const std::vector<WaylandLifecycleAction> &actions) {
	for (const WaylandLifecycleAction &action : actions) {
		switch (action.type) {
		case WaylandLifecycleActionType::BindCompositor:
			compositor = static_cast<wl_compositor *>(wl_registry_bind(
				registry, action.name, &wl_compositor_interface, std::min(action.version, 4U)));
			if (!compositor) {
				callbackFailed = true;
			}
			break;
		case WaylandLifecycleActionType::BindWmBase:
			wmBase = static_cast<xdg_wm_base *>(wl_registry_bind(
				registry, action.name, &xdg_wm_base_interface, std::min(action.version, 1U)));
			if (!wmBase) {
				callbackFailed = true;
			}
			break;
		case WaylandLifecycleActionType::BindOutput: {
			wl_output *output = static_cast<wl_output *>(wl_registry_bind(
				registry, action.name, &wl_output_interface, std::min(action.version, 2U)));
			if (!output || wl_output_add_listener(output, &outputListener, this) != 0) {
				if (output) {
					wl_output_destroy(output);
				}
				callbackFailed = true;
				break;
			}
			outputs.push_back(Output{action.name, output});
			break;
		}
		case WaylandLifecycleActionType::ReleaseOutput: {
			const auto found = std::find_if(outputs.begin(), outputs.end(),
				[&action](const Output &output) { return output.name == action.name; });
			if (found != outputs.end()) {
				if (wl_output_get_version(found->proxy) >= WL_OUTPUT_RELEASE_SINCE_VERSION) {
					wl_output_release(found->proxy);
				} else {
					wl_output_destroy(found->proxy);
				}
				outputs.erase(found);
			}
			break;
		}
		case WaylandLifecycleActionType::BindSeat:
			seat = static_cast<wl_seat *>(wl_registry_bind(
				registry, action.name, &wl_seat_interface, std::min(action.version, 5U)));
			if (!seat || wl_seat_add_listener(seat, &seatListener, this) != 0) {
				if (seat) {
					if (wl_seat_get_version(seat) >= WL_SEAT_RELEASE_SINCE_VERSION) {
						wl_seat_release(seat);
					} else {
						wl_seat_destroy(seat);
					}
					seat = nullptr;
				}
				callbackFailed = true;
			}
			break;
		case WaylandLifecycleActionType::ReleaseSeat:
			if (seat) {
				if (wl_seat_get_version(seat) >= WL_SEAT_RELEASE_SINCE_VERSION) {
					wl_seat_release(seat);
				} else {
					wl_seat_destroy(seat);
				}
				seat = nullptr;
			}
			break;
		case WaylandLifecycleActionType::CreatePointer:
			pointer = wl_seat_get_pointer(seat);
			if (!pointer || wl_pointer_add_listener(pointer, &pointerListener, this) != 0) {
				if (pointer) {
					if (wl_pointer_get_version(pointer) >= WL_POINTER_RELEASE_SINCE_VERSION) {
						wl_pointer_release(pointer);
					} else {
						wl_pointer_destroy(pointer);
					}
					pointer = nullptr;
				}
				callbackFailed = true;
			}
			break;
		case WaylandLifecycleActionType::ReleasePointer:
			input.ResetPointerDevice();
			if (pointer) {
				if (wl_pointer_get_version(pointer) >= WL_POINTER_RELEASE_SINCE_VERSION) {
					wl_pointer_release(pointer);
				} else {
					wl_pointer_destroy(pointer);
				}
				pointer = nullptr;
			}
			break;
		case WaylandLifecycleActionType::CreateKeyboard:
			keyboard = wl_seat_get_keyboard(seat);
			if (!keyboard || wl_keyboard_add_listener(
				keyboard, &keyboardListener, this) != 0) {
				if (keyboard) {
					if (wl_keyboard_get_version(keyboard) >= WL_KEYBOARD_RELEASE_SINCE_VERSION) {
						wl_keyboard_release(keyboard);
					} else {
						wl_keyboard_destroy(keyboard);
					}
					keyboard = nullptr;
				}
				callbackFailed = true;
			}
			break;
		case WaylandLifecycleActionType::ReleaseKeyboard:
			input.ResetKeyboardDevice();
			if (keyboard) {
				if (wl_keyboard_get_version(keyboard) >= WL_KEYBOARD_RELEASE_SINCE_VERSION) {
					wl_keyboard_release(keyboard);
				} else {
					wl_keyboard_destroy(keyboard);
				}
				keyboard = nullptr;
			}
			break;
		case WaylandLifecycleActionType::Close:
			break;
		}
	}
}

std::optional<uint32_t> WaylandWindow::RegistryNameForSeat(wl_seat *seat_) const noexcept {
	return seat_ == seat ? lifecycle.ActiveSeat() : std::nullopt;
}

std::optional<uint32_t> WaylandWindow::OutputName(wl_output *output) const noexcept {
	const auto found = std::find_if(outputs.begin(), outputs.end(),
		[output](const Output &candidate) { return candidate.proxy == output; });
	return found == outputs.end() ? std::nullopt : std::optional<uint32_t>{found->name};
}

void WaylandWindow::DispatchUntilConfigured() {
	while (!configured) {
		if (wl_display_dispatch(display) < 0) {
			throw std::runtime_error("Wayland display failed before initial configure");
		}
	}
}

void WaylandWindow::RoundTrip() {
	if (!display || wl_display_roundtrip(display) < 0) {
		throw std::runtime_error("Wayland display round trip failed");
	}
}

void WaylandWindow::WaitForEvents(std::optional<std::chrono::milliseconds> timeout) {
	if (!display) {
		throw std::runtime_error("Wayland event wait requires a display");
	}
	bool recoveringBlockedFlush = false;
	for (;;) {
		while (wl_display_prepare_read(display) != 0) {
			if (wl_display_dispatch_pending(display) < 0 || wl_display_get_error(display) != 0) {
				throw std::runtime_error("Wayland display dispatch failed");
			}
		}

		short events = POLLIN;
		if (wl_display_flush(display) < 0) {
			if (errno != EAGAIN) {
				wl_display_cancel_read(display);
				throw std::runtime_error("Wayland display flush failed");
			}
			events |= POLLOUT;
			recoveringBlockedFlush = true;
		} else if (recoveringBlockedFlush) {
			wl_display_cancel_read(display);
			break;
		}

		// Once output is blocked, wait for socket progress even when editor work is
		// already due. Returning on its zero timeout would spin until POLLOUT.
		const int timeoutMilliseconds = recoveringBlockedFlush ? -1 :
			(timeout ? static_cast<int>(std::clamp<int64_t>(
				timeout->count(), 0, INT_MAX)) : -1);
		pollfd displayPoll{wl_display_get_fd(display), events, 0};
		const int pollResult = poll(&displayPoll, 1, timeoutMilliseconds);
		if (pollResult < 0) {
			wl_display_cancel_read(display);
			if (errno == EINTR) {
				return;
			}
			throw std::runtime_error("Wayland display poll failed");
		}

		if (pollResult > 0 && (displayPoll.revents & POLLIN)) {
			if (wl_display_read_events(display) < 0) {
				throw std::runtime_error("Wayland display read failed");
			}
		} else {
			wl_display_cancel_read(display);
		}

		if (displayPoll.revents & (POLLERR | POLLHUP | POLLNVAL)) {
			throw std::runtime_error("Wayland display connection failed");
		}

		int dispatched = 0;
		do {
			dispatched = wl_display_dispatch_pending(display);
		} while (dispatched > 0);
		if (dispatched < 0 || wl_display_get_error(display) != 0) {
			throw std::runtime_error("Wayland display dispatch failed");
		}
		if (CallbackFailed()) {
			throw std::runtime_error("Wayland listener failed");
		}
		if (!recoveringBlockedFlush) {
			break;
		}
	}
}

void WaylandWindow::RegistryGlobal(void *data, wl_registry *, uint32_t name,
	const char *interface, uint32_t version) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
		window.ApplyLifecycleActions(window.lifecycle.AddGlobal(
			WaylandGlobalKind::Compositor, name, version));
	} else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
		window.ApplyLifecycleActions(window.lifecycle.AddGlobal(
			WaylandGlobalKind::WmBase, name, version));
	} else if (std::strcmp(interface, wl_output_interface.name) == 0) {
		window.ApplyLifecycleActions(window.lifecycle.AddGlobal(
			WaylandGlobalKind::Output, name, version));
	} else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
		window.ApplyLifecycleActions(window.lifecycle.AddGlobal(
			WaylandGlobalKind::Seat, name, version));
	}
}

void WaylandWindow::RegistryGlobalRemove(void *data, wl_registry *, uint32_t name) {
	auto &window = *static_cast<WaylandWindow *>(data);
	window.ApplyLifecycleActions(window.lifecycle.RemoveGlobal(name));
}

void WaylandWindow::SeatCapabilities(void *data, wl_seat *seat_, uint32_t capabilities) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (const std::optional<uint32_t> name = window.RegistryNameForSeat(seat_)) {
		window.ApplyLifecycleActions(window.lifecycle.UpdateSeatCapabilities(*name,
			capabilities & WL_SEAT_CAPABILITY_POINTER,
			capabilities & WL_SEAT_CAPABILITY_KEYBOARD));
	}
}

void WaylandWindow::SeatName(void *, wl_seat *, const char *) {
}

void WaylandWindow::KeyboardKeymap(void *data, wl_keyboard *, uint32_t format,
	int32_t descriptor, uint32_t size) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (descriptor < 0) {
		window.callbackFailed = true;
		return;
	}
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || size == 0) {
		close(descriptor);
		window.callbackFailed = true;
		return;
	}
	void *mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, descriptor, 0);
	close(descriptor);
	if (mapped == MAP_FAILED) {
		window.callbackFailed = true;
		return;
	}
	const bool loaded = window.input.SetKeymap(
		std::string_view(static_cast<const char *>(mapped), size));
	munmap(mapped, size);
	if (!loaded) {
		window.callbackFailed = true;
	}
}

void WaylandWindow::KeyboardEnter(void *data, wl_keyboard *, uint32_t, wl_surface *surface_, wl_array *) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (surface_ == window.surface) {
		window.input.RecordKeyboardFocus(true);
	}
}

void WaylandWindow::KeyboardLeave(void *data, wl_keyboard *, uint32_t, wl_surface *surface_) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (surface_ == window.surface) {
		window.input.RecordKeyboardFocus(false);
		window.input.ResetKeyboardState();
	}
}

void WaylandWindow::KeyboardKey(void *data, wl_keyboard *, uint32_t, uint32_t time,
	uint32_t key, uint32_t state) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (state == WL_KEYBOARD_KEY_STATE_PRESSED || state == WL_KEYBOARD_KEY_STATE_RELEASED) {
		window.input.RecordKey(time, key, state == WL_KEYBOARD_KEY_STATE_PRESSED);
	}
}

void WaylandWindow::KeyboardModifiers(void *data, wl_keyboard *, uint32_t, uint32_t depressed,
	uint32_t latched, uint32_t locked, uint32_t group) {
	static_cast<WaylandWindow *>(data)->input.UpdateModifiers(
		depressed, latched, locked, group);
}

void WaylandWindow::KeyboardRepeatInfo(void *, wl_keyboard *, int32_t, int32_t) {
	// Key repeat belongs to phase 7.
}

void WaylandWindow::PointerEnter(void *data, wl_pointer *, uint32_t,
	wl_surface *surface_, int32_t x, int32_t y) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (surface_ == window.surface) {
		window.input.RecordPointerMotion(0, wl_fixed_to_double(x), wl_fixed_to_double(y));
	}
}

void WaylandWindow::PointerLeave(void *data, wl_pointer *, uint32_t, wl_surface *surface_) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (surface_ == window.surface) {
		window.input.RecordPointerLeave();
	}
}

void WaylandWindow::PointerMotion(void *data, wl_pointer *, uint32_t time,
	int32_t x, int32_t y) {
	static_cast<WaylandWindow *>(data)->input.RecordPointerMotion(
		time, wl_fixed_to_double(x), wl_fixed_to_double(y));
}

void WaylandWindow::PointerButton(void *data, wl_pointer *, uint32_t, uint32_t time,
	uint32_t button, uint32_t state) {
	if (state == WL_POINTER_BUTTON_STATE_PRESSED || state == WL_POINTER_BUTTON_STATE_RELEASED) {
		static_cast<WaylandWindow *>(data)->input.RecordPointerButton(
			time, button, state == WL_POINTER_BUTTON_STATE_PRESSED);
	}
}

void WaylandWindow::PointerAxis(void *data, wl_pointer *, uint32_t time,
	uint32_t axis, int32_t value) {
	static_cast<WaylandWindow *>(data)->input.RecordPointerAxis(
		time, axis, wl_fixed_to_double(value));
}

void WaylandWindow::PointerFrame(void *, wl_pointer *) {
}

void WaylandWindow::PointerAxisSource(void *, wl_pointer *, uint32_t) {
}

void WaylandWindow::PointerAxisStop(void *, wl_pointer *, uint32_t, uint32_t) {
}

void WaylandWindow::PointerAxisDiscrete(void *, wl_pointer *, uint32_t, int32_t) {
}

void WaylandWindow::PointerAxisValue120(void *, wl_pointer *, uint32_t, int32_t) {
}

void WaylandWindow::PointerAxisRelativeDirection(void *, wl_pointer *, uint32_t, uint32_t) {
}

void WaylandWindow::OutputGeometry(void *, wl_output *, int32_t, int32_t, int32_t,
	int32_t, int32_t, const char *, const char *, int32_t) {
}

void WaylandWindow::OutputMode(void *, wl_output *, uint32_t, int32_t, int32_t, int32_t) {
}

void WaylandWindow::OutputDone(void *, wl_output *) {
}

void WaylandWindow::OutputScale(void *, wl_output *, int32_t) {
	// Output scale becomes application-visible in phase 7 step 10.
}

void WaylandWindow::OutputName(void *, wl_output *, const char *) {
}

void WaylandWindow::OutputDescription(void *, wl_output *, const char *) {
}

void WaylandWindow::WaylandSurfaceEnter(void *data, wl_surface *surface_, wl_output *output) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (surface_ == window.surface) {
		if (const std::optional<uint32_t> name = window.OutputName(output)) {
			window.lifecycle.EnterOutput(*name);
		}
	}
}

void WaylandWindow::WaylandSurfaceLeave(void *data, wl_surface *surface_, wl_output *output) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (surface_ == window.surface) {
		if (const std::optional<uint32_t> name = window.OutputName(output)) {
			window.lifecycle.LeaveOutput(*name);
		}
	}
}

void WaylandWindow::WaylandSurfacePreferredBufferScale(void *, wl_surface *, int32_t) {
}

void WaylandWindow::WaylandSurfacePreferredBufferTransform(void *, wl_surface *, uint32_t) {
}

void WaylandWindow::WmBasePing(void *, xdg_wm_base *wmBase_, uint32_t serial) {
	xdg_wm_base_pong(wmBase_, serial);
}

void WaylandWindow::SurfaceConfigure(void *data, xdg_surface *shellSurface_, uint32_t serial) {
	auto &window = *static_cast<WaylandWindow *>(data);
	xdg_surface_ack_configure(shellSurface_, serial);
	if (const std::optional<WindowSize> resize = window.lifecycle.CommitConfigure();
		resize && window.eglWindow) {
		wl_egl_window_resize(window.eglWindow, resize->width, resize->height, 0, 0);
	}
	window.configured = true;
}

void WaylandWindow::ToplevelConfigure(void *data, xdg_toplevel *, int32_t width_,
	int32_t height_, wl_array *) {
	auto &window = *static_cast<WaylandWindow *>(data);
	window.lifecycle.ProposeSize(width_, height_);
}

void WaylandWindow::ToplevelClose(void *data, xdg_toplevel *) {
	static_cast<WaylandWindow *>(data)->lifecycle.RequestClose();
}

void WaylandWindow::ToplevelConfigureBounds(void *, xdg_toplevel *, int32_t, int32_t) {
}

void WaylandWindow::ToplevelWmCapabilities(void *, xdg_toplevel *, wl_array *) {
}

}
