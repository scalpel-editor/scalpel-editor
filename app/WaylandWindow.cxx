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
		throw std::runtime_error("could not listen for Wayland keyboard focus");
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
		throw std::runtime_error("could not initialise Wayland keyboard focus");
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
	if (keyboard) {
		wl_keyboard_destroy(keyboard);
	}
	if (seat) {
		wl_seat_destroy(seat);
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
	seat = nullptr;
	wmBase = nullptr;
	compositor = nullptr;
	registry = nullptr;
	display = nullptr;
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
			throw std::runtime_error("Wayland input listener failed");
		}
		if (!recoveringBlockedFlush) {
			break;
		}
	}
}

void WaylandWindow::RegistryGlobal(void *data, wl_registry *registry_, uint32_t name,
	const char *interface, uint32_t version) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (std::strcmp(interface, wl_compositor_interface.name) == 0 && !window.compositor) {
		window.compositor = static_cast<wl_compositor *>(wl_registry_bind(
			registry_, name, &wl_compositor_interface, std::min(version, 4U)));
	} else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0 && !window.wmBase) {
		window.wmBase = static_cast<xdg_wm_base *>(wl_registry_bind(
			registry_, name, &xdg_wm_base_interface, std::min(version, 1U)));
	} else if (std::strcmp(interface, wl_seat_interface.name) == 0 && !window.seat) {
		window.seat = static_cast<wl_seat *>(wl_registry_bind(
			registry_, name, &wl_seat_interface, std::min(version, 1U)));
		if (!window.seat || wl_seat_add_listener(window.seat, &seatListener, &window) != 0) {
			window.callbackFailed = true;
		}
	}
}

void WaylandWindow::RegistryGlobalRemove(void *, wl_registry *, uint32_t) {
	// Robust removal and hot-plugged seats belong to phase 7.
}

void WaylandWindow::SeatCapabilities(void *data, wl_seat *seat_, uint32_t capabilities) {
	auto &window = *static_cast<WaylandWindow *>(data);
	const bool hasKeyboard = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;
	if (hasKeyboard && !window.keyboard) {
		window.keyboard = wl_seat_get_keyboard(seat_);
		if (!window.keyboard || wl_keyboard_add_listener(window.keyboard, &keyboardListener, &window) != 0) {
			if (window.keyboard) {
				wl_keyboard_destroy(window.keyboard);
				window.keyboard = nullptr;
			}
			window.callbackFailed = true;
		}
	} else if (!hasKeyboard && window.keyboard) {
		wl_keyboard_destroy(window.keyboard);
		window.keyboard = nullptr;
		window.lifecycle.RecordKeyboardFocus(false);
	}
}

void WaylandWindow::SeatName(void *, wl_seat *, const char *) {
}

void WaylandWindow::KeyboardKeymap(void *, wl_keyboard *, uint32_t, int32_t descriptor, uint32_t) {
	if (descriptor >= 0) {
		close(descriptor);
	}
}

void WaylandWindow::KeyboardEnter(void *data, wl_keyboard *, uint32_t, wl_surface *surface_, wl_array *) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (surface_ == window.surface) {
		window.lifecycle.RecordKeyboardFocus(true);
	}
}

void WaylandWindow::KeyboardLeave(void *data, wl_keyboard *, uint32_t, wl_surface *surface_) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (surface_ == window.surface) {
		window.lifecycle.RecordKeyboardFocus(false);
	}
}

void WaylandWindow::KeyboardKey(void *, wl_keyboard *, uint32_t, uint32_t, uint32_t, uint32_t) {
	// Key translation and delivery belong to phase 6 step 11.
}

void WaylandWindow::KeyboardModifiers(void *, wl_keyboard *, uint32_t, uint32_t, uint32_t,
	uint32_t, uint32_t) {
	// Modifier state belongs to phase 6 step 11.
}

void WaylandWindow::KeyboardRepeatInfo(void *, wl_keyboard *, int32_t, int32_t) {
	// Key repeat belongs to phase 7.
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
