// Minimal xdg-shell toplevel for the phase 6 vertical slice.

#ifndef WAYLANDWINDOW_H
#define WAYLANDWINDOW_H

#include <cstdint>

struct wl_array;
struct wl_compositor;
struct wl_display;
struct wl_registry;
struct wl_registry_listener;
struct wl_seat;
struct wl_surface;
struct xdg_surface;
struct xdg_surface_listener;
struct xdg_toplevel;
struct xdg_toplevel_listener;
struct xdg_wm_base;
struct xdg_wm_base_listener;

namespace Scalpel {

/**
 * One Wayland display connection and configured xdg-toplevel.
 *
 * Construction performs the required initial commit without a buffer and
 * waits for xdg_surface.configure before returning. Input objects, redraw
 * dispatch, resizing after startup, and robust global removal belong to the
 * following phase 6 and phase 7 steps.
 */
class WaylandWindow final {
public:
	WaylandWindow(const char *title, int width, int height);
	~WaylandWindow() noexcept;

	WaylandWindow(const WaylandWindow &) = delete;
	WaylandWindow(WaylandWindow &&) = delete;
	WaylandWindow &operator=(const WaylandWindow &) = delete;
	WaylandWindow &operator=(WaylandWindow &&) = delete;

	[[nodiscard]] wl_display *Display() const noexcept { return display; }
	[[nodiscard]] wl_surface *Surface() const noexcept { return surface; }
	[[nodiscard]] wl_seat *Seat() const noexcept { return seat; }
	[[nodiscard]] int Width() const noexcept { return width; }
	[[nodiscard]] int Height() const noexcept { return height; }
	[[nodiscard]] bool Configured() const noexcept { return configured; }
	[[nodiscard]] bool CloseRequested() const noexcept { return closeRequested; }

	/** Complete one request/event round trip, throwing on display failure. */
	void RoundTrip();

private:
	void Initialise(const char *title);
	void Destroy() noexcept;
	void DispatchUntilConfigured();

	static void RegistryGlobal(void *data, wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version);
	static void RegistryGlobalRemove(void *data, wl_registry *registry, uint32_t name);
	static void WmBasePing(void *data, xdg_wm_base *wmBase, uint32_t serial);
	static void SurfaceConfigure(void *data, xdg_surface *shellSurface, uint32_t serial);
	static void ToplevelConfigure(void *data, xdg_toplevel *toplevel, int32_t width,
		int32_t height, wl_array *states);
	static void ToplevelClose(void *data, xdg_toplevel *toplevel);
	static void ToplevelConfigureBounds(void *data, xdg_toplevel *toplevel,
		int32_t width, int32_t height);
	static void ToplevelWmCapabilities(void *data, xdg_toplevel *toplevel,
		wl_array *capabilities);

	static const wl_registry_listener registryListener;
	static const xdg_wm_base_listener wmBaseListener;
	static const xdg_surface_listener surfaceListener;
	static const xdg_toplevel_listener toplevelListener;

	wl_display *display = nullptr;
	wl_registry *registry = nullptr;
	wl_compositor *compositor = nullptr;
	wl_seat *seat = nullptr;
	xdg_wm_base *wmBase = nullptr;
	wl_surface *surface = nullptr;
	xdg_surface *shellSurface = nullptr;
	xdg_toplevel *toplevel = nullptr;
	int width;
	int height;
	bool configured = false;
	bool closeRequested = false;
};

}

#endif
