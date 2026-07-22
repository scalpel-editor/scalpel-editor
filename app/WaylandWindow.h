// Minimal xdg-shell toplevel for the phase 6 vertical slice.

#ifndef WAYLANDWINDOW_H
#define WAYLANDWINDOW_H

#include <cstdint>
#include <optional>

#include "WaylandLifecycle.h"

struct wl_array;
struct wl_compositor;
struct wl_display;
struct wl_egl_window;
struct wl_keyboard;
struct wl_keyboard_listener;
struct wl_registry;
struct wl_registry_listener;
struct wl_seat;
struct wl_seat_listener;
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
 * waits for xdg_surface.configure before returning. It coalesces later resize
 * events and reports keyboard focus changes. Key translation, pointer input,
 * and robust global removal belong to the following phase 6 and phase 7 steps.
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
	[[nodiscard]] wl_egl_window *EglWindow() const noexcept { return eglWindow; }
	[[nodiscard]] wl_seat *Seat() const noexcept { return seat; }
	[[nodiscard]] int Width() const noexcept { return lifecycle.Width(); }
	[[nodiscard]] int Height() const noexcept { return lifecycle.Height(); }
	[[nodiscard]] bool Configured() const noexcept { return configured; }
	[[nodiscard]] bool CloseRequested() const noexcept { return lifecycle.CloseRequested(); }
	[[nodiscard]] bool CallbackFailed() const noexcept { return callbackFailed; }
	[[nodiscard]] std::optional<WindowSize> TakeResize() noexcept { return lifecycle.TakeResize(); }
	[[nodiscard]] std::optional<bool> TakeKeyboardFocus() noexcept { return lifecycle.TakeKeyboardFocus(); }

	/** Complete one request/event round trip, throwing on display failure. */
	void RoundTrip();

private:
	void Initialise(const char *title);
	void Destroy() noexcept;
	void DispatchUntilConfigured();

	static void RegistryGlobal(void *data, wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version);
	static void RegistryGlobalRemove(void *data, wl_registry *registry, uint32_t name);
	static void SeatCapabilities(void *data, wl_seat *seat, uint32_t capabilities);
	static void SeatName(void *data, wl_seat *seat, const char *name);
	static void KeyboardKeymap(void *data, wl_keyboard *keyboard, uint32_t format,
		int32_t descriptor, uint32_t size);
	static void KeyboardEnter(void *data, wl_keyboard *keyboard, uint32_t serial,
		wl_surface *surface, wl_array *keys);
	static void KeyboardLeave(void *data, wl_keyboard *keyboard, uint32_t serial,
		wl_surface *surface);
	static void KeyboardKey(void *data, wl_keyboard *keyboard, uint32_t serial,
		uint32_t time, uint32_t key, uint32_t state);
	static void KeyboardModifiers(void *data, wl_keyboard *keyboard, uint32_t serial,
		uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
	static void KeyboardRepeatInfo(void *data, wl_keyboard *keyboard, int32_t rate,
		int32_t delay);
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
	static const wl_seat_listener seatListener;
	static const wl_keyboard_listener keyboardListener;
	static const xdg_wm_base_listener wmBaseListener;
	static const xdg_surface_listener surfaceListener;
	static const xdg_toplevel_listener toplevelListener;

	wl_display *display = nullptr;
	wl_registry *registry = nullptr;
	wl_compositor *compositor = nullptr;
	wl_seat *seat = nullptr;
	wl_keyboard *keyboard = nullptr;
	xdg_wm_base *wmBase = nullptr;
	wl_surface *surface = nullptr;
	wl_egl_window *eglWindow = nullptr;
	xdg_surface *shellSurface = nullptr;
	xdg_toplevel *toplevel = nullptr;
	WaylandLifecycle lifecycle;
	bool callbackFailed = false;
	bool configured = false;
};

}

#endif
