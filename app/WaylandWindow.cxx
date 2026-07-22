// Minimal xdg-shell toplevel for the phase 6 vertical slice.
//
// The connection, configure handshake, and cleanup order were informed by
// OnlyWayUi's Wayland backend. Its MIT notice is retained in seed/LICENSE.txt.

#include "WaylandWindow.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

#include <wayland-client.h>
#include <wayland-egl.h>

#include "xdg-shell-client-protocol.h"

namespace Scalpel {

const wl_registry_listener WaylandWindow::registryListener = {
	WaylandWindow::RegistryGlobal,
	WaylandWindow::RegistryGlobalRemove,
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
	width(width_), height(height_) {
	if (!title || width <= 0 || height <= 0) {
		throw std::invalid_argument("WaylandWindow requires a title and positive size");
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
	eglWindow = wl_egl_window_create(surface, width, height);
	if (!eglWindow) {
		throw std::runtime_error("could not create the Wayland EGL window");
	}
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
	}
}

void WaylandWindow::RegistryGlobalRemove(void *, wl_registry *, uint32_t) {
	// Robust removal and hot-plugged seats belong to phase 7.
}

void WaylandWindow::WmBasePing(void *, xdg_wm_base *wmBase_, uint32_t serial) {
	xdg_wm_base_pong(wmBase_, serial);
}

void WaylandWindow::SurfaceConfigure(void *data, xdg_surface *shellSurface_, uint32_t serial) {
	auto &window = *static_cast<WaylandWindow *>(data);
	xdg_surface_ack_configure(shellSurface_, serial);
	window.configured = true;
}

void WaylandWindow::ToplevelConfigure(void *data, xdg_toplevel *, int32_t width_,
	int32_t height_, wl_array *) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (width_ > 0 && height_ > 0) {
		window.width = width_;
		window.height = height_;
	}
}

void WaylandWindow::ToplevelClose(void *data, xdg_toplevel *) {
	static_cast<WaylandWindow *>(data)->closeRequested = true;
}

void WaylandWindow::ToplevelConfigureBounds(void *, xdg_toplevel *, int32_t, int32_t) {
}

void WaylandWindow::ToplevelWmCapabilities(void *, xdg_toplevel *, wl_array *) {
}

}
