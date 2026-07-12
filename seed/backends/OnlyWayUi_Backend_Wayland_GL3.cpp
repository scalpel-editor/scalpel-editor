#include "OnlyWayUi_Backend.h"
#include "OnlyWayUi_Platform_Wayland.h"
#include "OnlyWayUi_Portal_Uri.h"
#include "OnlyWayUi_Renderer_GL3.h"
#include "presentation-time-client-protocol.h"
#include "xdg-decoration-client-protocol.h"
#include "xdg-foreign-unstable-v2-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include <EGL/eglplatform.h>
#include <OnlyWayUi/Config/Config.h>
#include <OnlyWayUi/Core/Context.h>
#include <OnlyWayUi/Core/Debug.h>
#include <OnlyWayUi/Core/Input.h>
#include <OnlyWayUi/Core/Log.h>
#include <OnlyWayUi/Core/Math.h>
#include <OnlyWayUi/Core/Rectangle.h>
#include <OnlyWayUi/Core/Span.h>
#include <OnlyWayUi/Core/Vector2.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dbus/dbus-protocol.h>
#include <dbus/dbus-shared.h>
#include <dbus/dbus.h>
#include <poll.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-egl-core.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>

namespace OnlyWayUi {
class RenderInterface;
class SystemInterface;
enum class Character : char32_t;
} // namespace OnlyWayUi

struct wl_callback;
struct wl_compositor;
struct wl_data_device_manager;
struct wl_display;
struct wl_egl_window;
struct wl_keyboard;
struct wl_output;
struct wl_pointer;
struct wl_registry;
struct wl_seat;
struct wl_shm;
struct wl_surface;
struct wp_presentation;
struct xdg_surface;
struct xdg_toplevel;
struct xdg_wm_base;
struct zxdg_decoration_manager_v1;
struct zxdg_exported_v2;
struct zxdg_exporter_v2;
struct zxdg_toplevel_decoration_v1;

static constexpr int MinimumWindowWidth = 1;
static constexpr int MinimumWindowHeight = 1;
static constexpr double ClipboardPollTimeout = 0.5;
// Safety timeout while waiting for a frame callback. In practice the callback (or input) wakes us far sooner; this only
// bounds the wait if the compositor withholds callbacks (e.g. while occluded), so we keep servicing the event loop.
static constexpr double FramePacingTimeout = 0.1;
static constexpr double DefaultRefreshInterval = 1.0 / 60.0;
static constexpr const char* PortalDesktopService = "org.freedesktop.portal.Desktop";
using Clock = std::chrono::steady_clock;
using SwapBuffersWithDamageFunction = EGLBoolean(EGLAPIENTRY*)(EGLDisplay, EGLSurface, const EGLint*, EGLint);

struct KeyboardRepeatState {
	bool active = false;
	xkb_keycode_t keycode = 0;
	int32_t rate = 0;
	int32_t delay = 0;
	Clock::time_point next_time;
	std::chrono::milliseconds interval {0};

	void Stop()
	{
		active = false;
		keycode = 0;
	}
};

// D-Bus session-bus connection used for the xdg-desktop-portal file dialog. The connection is created lazily on the
// first dialog request; until then it stays null and adds nothing to the poll loop. libdbus hands us the fds and timers
// it wants the main loop to watch through the watch/timeout callbacks, which DispatchWaylandEvents folds into its poll.
struct DbusTimeoutRecord {
	DBusTimeout* timeout = nullptr;
	// When the timeout should next fire, or time_point::max() while it is disabled.
	Clock::time_point deadline = Clock::time_point::max();
};

// One in-flight file-dialog request. The portal answers asynchronously with a Response signal on request_path; until
// then the pending request holds the portal owner and the caller's callback.
struct FileDialogPending {
	OnlyWayUi::String request_path;
	OnlyWayUi::String portal_owner;
	Backend::FileDialogCallback callback;
};

struct DbusState {
	DBusConnection* connection = nullptr;
	// Every watch libdbus has asked us to monitor, enabled or not; CollectExtraPollSources polls the enabled ones.
	OnlyWayUi::Vector<DBusWatch*> watches;
	OnlyWayUi::Vector<DbusTimeoutRecord> timeouts;
	// In-flight file-dialog requests, keyed by their portal Request object path.
	OnlyWayUi::Vector<OnlyWayUi::UniquePtr<FileDialogPending>> file_dialogs;
	// Whether the shared Response signal filter and match rule have been installed on the connection.
	bool response_filter_installed = false;
};

struct BackendData {
	OwuiWayland::Globals globals;
	OnlyWayUi::UniquePtr<SystemInterface_Wayland> system_interface;
	OwuiWayland::KeyboardState keyboard_state;
	KeyboardRepeatState repeat_state;
	OnlyWayUi::UniquePtr<RenderInterface_GL3> render_interface;

	wl_display* display = nullptr;
	wl_registry* registry = nullptr;
	wl_surface* surface = nullptr;
	wl_surface* cursor_surface = nullptr;
	xdg_surface* shell_surface = nullptr;
	xdg_toplevel* shell_toplevel = nullptr;
	zxdg_toplevel_decoration_v1* shell_decoration = nullptr;
	// Exported handle for the toplevel, used as the portal parent-window token. exported is the protocol object;
	// parent_window_handle is the "wayland:<handle>" string, empty until the compositor sends the handle.
	zxdg_exported_v2* exported = nullptr;
	OnlyWayUi::String parent_window_handle;
	wl_egl_window* egl_window = nullptr;
	wl_pointer* pointer = nullptr;
	wl_keyboard* keyboard = nullptr;
	wl_callback* frame_callback = nullptr;
	// In-flight presentation feedback objects, oldest first. The presented event for a commit arrives shortly after the
	// frame callback that lets us make the next commit, so two are routinely pending at once; each is destroyed when its
	// presented or discarded event arrives.
	OnlyWayUi::Vector<struct wp_presentation_feedback*> presentation_feedbacks;

	EGLDisplay egl_display = EGL_NO_DISPLAY;
	EGLConfig egl_config = nullptr;
	EGLSurface egl_surface = EGL_NO_SURFACE;
	EGLContext egl_context = EGL_NO_CONTEXT;
	bool egl_buffer_age_supported = false;
	SwapBuffersWithDamageFunction egl_swap_buffers_with_damage = nullptr;
	int egl_last_buffer_age = 0;
	OnlyWayUi::Vector<OnlyWayUi::Rectanglei> frame_damage_regions;
	OnlyWayUi::Vector<OnlyWayUi::Rectanglei> surface_damage_regions;
	OnlyWayUi::Vector<OnlyWayUi::Vector<OnlyWayUi::Rectanglei>> damage_history;
	OnlyWayUi::Rectanglei frame_damage_bounds = OnlyWayUi::Rectanglei::MakeInvalid();

	OnlyWayUi::Context* context = nullptr;
	KeyDownCallback key_down_callback = nullptr;

	int width = 0;
	int height = 0;
	bool configured = false;
	bool running = true;
	// A compositor close request (decoration close button) is recorded here. By default ProcessEvents honors it by
	// clearing running, so the window closes as before. When intercept_close is set, the request is instead delivered to
	// the application through ConsumeCloseRequest, which resolves it on its own schedule (for example, after confirming
	// unsaved work) and exits via RequestExit.
	bool close_requested = false;
	bool intercept_close = false;
	bool context_dimensions_dirty = true;
	// Presentation is decoupled from event dispatch. frame_ready (driven by the frame callback) tells us the compositor
	// is ready for a new frame; while it is false we skip the swap rather than block, so the event loop keeps running
	// (and clipboard sends keep flowing) even when occluded. needs_redraw tracks whether the app should return from
	// event processing to update the context and check for visual changes.
	bool frame_ready = true;
	bool needs_redraw = true;

	// Presentation timing feedback (wp_presentation). Timestamps are seconds on the compositor's presentation clock;
	// last_refresh_interval is zero until the first presented event, or when the compositor cannot predict the refresh.
	clockid_t presentation_clock_id = CLOCK_MONOTONIC;
	double last_presented_time = 0.0;
	double last_refresh_interval = 0.0;

	// Session D-Bus connection for portal-backed file dialogs; see DbusState. Null until the first dialog request.
	DbusState dbus;
};

static OnlyWayUi::UniquePtr<BackendData> data;

static bool HasEglExtension(const char* extensions, const char* extension)
{
	if (!extensions || !extension)
		return false;

	const size_t extension_length = std::strlen(extension);
	const char* position = extensions;
	while ((position = std::strstr(position, extension)) != nullptr)
	{
		const bool at_start = (position == extensions || position[-1] == ' ');
		const bool at_end = (position[extension_length] == '\0' || position[extension_length] == ' ');
		if (at_start && at_end)
			return true;

		position += extension_length;
	}

	return false;
}

static SwapBuffersWithDamageFunction LoadSwapBuffersWithDamageFunction(const char* name)
{
	return reinterpret_cast<SwapBuffersWithDamageFunction>(eglGetProcAddress(name));
}

static void UpdateEglBufferAge()
{
	data->egl_last_buffer_age = 0;
	if (data->egl_buffer_age_supported)
	{
		EGLint buffer_age = 0;
		if (eglQuerySurface(data->egl_display, data->egl_surface, EGL_BUFFER_AGE_EXT, &buffer_age))
			data->egl_last_buffer_age = buffer_age;
	}
}

static void InitializeEglDamageSupport()
{
	const char* extensions = eglQueryString(data->egl_display, EGL_EXTENSIONS);
	data->egl_buffer_age_supported = HasEglExtension(extensions, "EGL_EXT_buffer_age");

	if (HasEglExtension(extensions, "EGL_KHR_swap_buffers_with_damage"))
		data->egl_swap_buffers_with_damage = LoadSwapBuffersWithDamageFunction("eglSwapBuffersWithDamageKHR");
	else if (HasEglExtension(extensions, "EGL_EXT_swap_buffers_with_damage"))
		data->egl_swap_buffers_with_damage = LoadSwapBuffersWithDamageFunction("eglSwapBuffersWithDamageEXT");
}

static OnlyWayUi::Rectanglei GetSurfaceRegion()
{
	return OnlyWayUi::Rectanglei::FromSize({data->width, data->height});
}

static void AddDamageRegion(OnlyWayUi::Vector<OnlyWayUi::Rectanglei>& regions, OnlyWayUi::Rectanglei region)
{
	const OnlyWayUi::Rectanglei surface_region = GetSurfaceRegion();
	if (!region.Valid() || surface_region.Width() <= 0 || surface_region.Height() <= 0)
		return;

	region = region.Intersect(surface_region);
	if (region.Width() <= 0 || region.Height() <= 0)
		return;

	static constexpr size_t MaxDamageRegions = 32;
	for (OnlyWayUi::Rectanglei& existing : regions)
	{
		if (existing.Intersects(region) || existing.Contains(region.TopLeft()) || region.Contains(existing.TopLeft()))
		{
			existing = existing.Join(region);
			return;
		}
	}

	if (regions.size() >= MaxDamageRegions)
	{
		for (OnlyWayUi::Rectanglei existing : regions)
			region = region.Join(existing);
		regions.clear();
		regions.push_back(region);
		return;
	}

	regions.push_back(region);
}

static void AddFullDamageRegion(OnlyWayUi::Vector<OnlyWayUi::Rectanglei>& regions)
{
	regions.clear();
	AddDamageRegion(regions, GetSurfaceRegion());
}

static OnlyWayUi::Rectanglei JoinDamageRegions(OnlyWayUi::Span<const OnlyWayUi::Rectanglei> regions)
{
	OnlyWayUi::Rectanglei bounds = OnlyWayUi::Rectanglei::MakeInvalid();
	for (OnlyWayUi::Rectanglei region : regions)
		bounds = bounds.Valid() ? bounds.Join(region) : region;
	return bounds;
}

static void PrepareFrameDamage(OnlyWayUi::Context* context)
{
	data->surface_damage_regions.clear();
	data->frame_damage_regions.clear();
	data->frame_damage_bounds = OnlyWayUi::Rectanglei::MakeInvalid();

	for (OnlyWayUi::Rectanglei region : context->GetRenderDamageRegions())
		AddDamageRegion(data->surface_damage_regions, region);

	if (data->surface_damage_regions.empty())
		AddFullDamageRegion(data->surface_damage_regions);

	for (OnlyWayUi::Rectanglei region : data->surface_damage_regions)
		AddDamageRegion(data->frame_damage_regions, region);

	const int buffer_age = data->egl_last_buffer_age;
	if (!data->egl_buffer_age_supported || buffer_age <= 0 || buffer_age - 1 > int(data->damage_history.size()))
	{
		AddFullDamageRegion(data->frame_damage_regions);
	}
	else
	{
		for (int i = 0; i < buffer_age - 1; ++i)
		{
			for (OnlyWayUi::Rectanglei region : data->damage_history[i])
				AddDamageRegion(data->frame_damage_regions, region);
		}
	}

	data->frame_damage_bounds = JoinDamageRegions(data->frame_damage_regions);
}

static void PushDamageHistory()
{
	static constexpr size_t MaxDamageHistory = 4;
	data->damage_history.insert(data->damage_history.begin(), data->surface_damage_regions);
	if (data->damage_history.size() > MaxDamageHistory)
		data->damage_history.resize(MaxDamageHistory);
}

// Wakes the app so it can update the context and check whether there is a frame worth rendering.
static void RequestRedraw()
{
	if (data)
		data->needs_redraw = true;
}

static void UpdateWindowSize(int width, int height)
{
	OWUI_ASSERT(data);

	data->width = OnlyWayUi::Math::Max(width, MinimumWindowWidth);
	data->height = OnlyWayUi::Math::Max(height, MinimumWindowHeight);

	if (data->egl_window)
		wl_egl_window_resize(data->egl_window, data->width, data->height, 0, 0);

	if (data->render_interface)
		data->render_interface->SetViewport(data->width, data->height);

	data->context_dimensions_dirty = true;
	data->damage_history.clear();
	RequestRedraw();
}

static void RegistryHandleGlobal(void* user_data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
{
	auto* globals = static_cast<OwuiWayland::Globals*>(user_data);

	if (std::strcmp(interface, wl_compositor_interface.name) == 0)
		globals->compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
	else if (std::strcmp(interface, wl_shm_interface.name) == 0)
		globals->shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
	else if (std::strcmp(interface, wl_seat_interface.name) == 0)
		globals->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 7u)));
	else if (std::strcmp(interface, wl_data_device_manager_interface.name) == 0)
		globals->data_device_manager = static_cast<wl_data_device_manager*>(
			wl_registry_bind(registry, name, &wl_data_device_manager_interface, std::min(version, 3u)));
	else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0)
		globals->wm_base = static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, std::min(version, 7u)));
	else if (std::strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0)
		globals->decoration_manager = static_cast<zxdg_decoration_manager_v1*>(
			wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1));
	else if (std::strcmp(interface, wp_presentation_interface.name) == 0)
		globals->presentation = static_cast<wp_presentation*>(wl_registry_bind(registry, name, &wp_presentation_interface, std::min(version, 2u)));
	else if (std::strcmp(interface, zxdg_exporter_v2_interface.name) == 0)
		globals->exporter = static_cast<zxdg_exporter_v2*>(wl_registry_bind(registry, name, &zxdg_exporter_v2_interface, 1));
}

// The compositor delivers the exported toplevel's handle here, asynchronously after export_toplevel. We keep it as the
// "wayland:<handle>" parent-window token the portal expects; until it arrives, dialogs open without a parent.
static void ExportedHandle(void*, zxdg_exported_v2*, const char* handle)
{
	if (data && handle)
		data->parent_window_handle = OnlyWayUi::String("wayland:") + handle;
}

static const zxdg_exported_v2_listener exported_listener = {
	ExportedHandle,
};

static void RegistryHandleGlobalRemove(void*, wl_registry*, uint32_t) {}

static const wl_registry_listener registry_listener = {
	RegistryHandleGlobal,
	RegistryHandleGlobalRemove,
};

static void PresentationHandleClockId(void*, wp_presentation*, uint32_t clk_id)
{
	data->presentation_clock_id = clockid_t(clk_id);
	if (data->system_interface)
		data->system_interface->SetClock(clockid_t(clk_id));
}

static const wp_presentation_listener presentation_listener = {
	PresentationHandleClockId,
};

static void PresentationFeedbackHandleSyncOutput(void*, struct wp_presentation_feedback*, wl_output*) {}

static void RemovePresentationFeedback(struct wp_presentation_feedback* feedback)
{
	wp_presentation_feedback_destroy(feedback);
	if (!data)
		return;
	auto& feedbacks = data->presentation_feedbacks;
	auto it = std::find(feedbacks.begin(), feedbacks.end(), feedback);
	if (it != feedbacks.end())
		feedbacks.erase(it);
}

static void PresentationFeedbackHandlePresented(void*, struct wp_presentation_feedback* feedback, uint32_t tv_sec_hi, uint32_t tv_sec_lo,
	uint32_t tv_nsec, uint32_t refresh, uint32_t /*seq_hi*/, uint32_t /*seq_lo*/, uint32_t /*flags*/)
{
	RemovePresentationFeedback(feedback);
	if (!data)
		return;

	data->last_presented_time = double((uint64_t(tv_sec_hi) << 32) | tv_sec_lo) + double(tv_nsec) / 1.0e9;
	data->last_refresh_interval = double(refresh) / 1.0e9;
}

static void PresentationFeedbackHandleDiscarded(void*, struct wp_presentation_feedback* feedback)
{
	RemovePresentationFeedback(feedback);
}

static const wp_presentation_feedback_listener presentation_feedback_listener = {
	PresentationFeedbackHandleSyncOutput,
	PresentationFeedbackHandlePresented,
	PresentationFeedbackHandleDiscarded,
};

// Predicts when the frame the application is about to build will reach the screen (the next output refresh after now,
// extrapolated from the last presented event), and locks the system interface's elapsed time to it so animations are
// sampled at display time. Without feedback data yet, the current presentation-clock time is the best estimate.
static void UpdatePredictedFrameTime()
{
	if (!data->globals.presentation || !data->system_interface)
		return;

	timespec now_spec {};
	if (clock_gettime(data->presentation_clock_id, &now_spec) != 0)
		return;
	const double now = double(now_spec.tv_sec) + double(now_spec.tv_nsec) / 1.0e9;

	double predicted = now;
	if (data->last_presented_time > 0.0 && data->last_refresh_interval > 0.0)
	{
		const double periods = std::ceil((now - data->last_presented_time) / data->last_refresh_interval);
		predicted = data->last_presented_time + OnlyWayUi::Math::Max(periods, 1.0) * data->last_refresh_interval;
	}

	data->system_interface->SetPredictedFrameTime(predicted);
}

static void XdgWmBaseHandlePing(void*, xdg_wm_base* xdg_wm_base, uint32_t serial)
{
	xdg_wm_base_pong(xdg_wm_base, serial);
}

static const xdg_wm_base_listener xdg_wm_base_listener = {
	XdgWmBaseHandlePing,
};

static void XdgSurfaceHandleConfigure(void*, xdg_surface* xdg_surface, uint32_t serial)
{
	xdg_surface_ack_configure(xdg_surface, serial);
	data->configured = true;
	RequestRedraw();
}

static const xdg_surface_listener xdg_surface_listener = {
	XdgSurfaceHandleConfigure,
};

static void XdgToplevelHandleConfigure(void*, xdg_toplevel*, int32_t width, int32_t height, wl_array*)
{
	if (width > 0 && height > 0)
		UpdateWindowSize(width, height);
}

static void XdgToplevelHandleClose(void*, xdg_toplevel*)
{
	// xdg_toplevel.close is a request, not a command. Record it; ProcessEvents exits by default, or hands it to the
	// application through ConsumeCloseRequest when interception is enabled.
	data->close_requested = true;
}

static void XdgToplevelHandleConfigureBounds(void*, xdg_toplevel*, int32_t, int32_t) {}
static void XdgToplevelHandleWmCapabilities(void*, xdg_toplevel*, wl_array*) {}

static const xdg_toplevel_listener xdg_toplevel_listener = {
	XdgToplevelHandleConfigure,
	XdgToplevelHandleClose,
	XdgToplevelHandleConfigureBounds,
	XdgToplevelHandleWmCapabilities,
};

static void XdgToplevelDecorationHandleConfigure(void*, zxdg_toplevel_decoration_v1*, uint32_t) {}

static const zxdg_toplevel_decoration_v1_listener xdg_toplevel_decoration_listener = {
	XdgToplevelDecorationHandleConfigure,
};

static void PointerHandleEnter(void*, wl_pointer*, uint32_t serial, wl_surface*, wl_fixed_t sx, wl_fixed_t sy)
{
	RequestRedraw();
	data->system_interface->SetPointerSerial(serial);
	if (data->context)
		data->context->ProcessMouseMove(wl_fixed_to_int(sx), wl_fixed_to_int(sy), data->keyboard_state.modifiers);
	data->system_interface->SetMouseCursor("arrow");
}

static void PointerHandleLeave(void*, wl_pointer*, uint32_t, wl_surface*)
{
	RequestRedraw();
	data->system_interface->ClearPointerSerial();
	if (data->context)
		data->context->ProcessMouseLeave();
}

static void PointerHandleMotion(void*, wl_pointer*, uint32_t, wl_fixed_t sx, wl_fixed_t sy)
{
	RequestRedraw();
	if (data->context)
		data->context->ProcessMouseMove(wl_fixed_to_int(sx), wl_fixed_to_int(sy), data->keyboard_state.modifiers);
}

static void PointerHandleButton(void*, wl_pointer*, uint32_t serial, uint32_t, uint32_t button, uint32_t state)
{
	if (!data->context)
		return;

	const int mouse_button = OwuiWayland::ConvertMouseButton(button);
	if (mouse_button < 0)
		return;

	RequestRedraw();
	data->system_interface->SetSeatSerial(serial);

	if (state == WL_POINTER_BUTTON_STATE_PRESSED)
		data->context->ProcessMouseButtonDown(mouse_button, data->keyboard_state.modifiers);
	else
		data->context->ProcessMouseButtonUp(mouse_button, data->keyboard_state.modifiers);
}

static void PointerHandleAxis(void*, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value)
{
	RequestRedraw();
	if (data->context && axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
		data->context->ProcessMouseWheel(float(wl_fixed_to_double(value)) / 10.f, data->keyboard_state.modifiers);
}

static void PointerHandleFrame(void*, wl_pointer*) {}
static void PointerHandleAxisSource(void*, wl_pointer*, uint32_t) {}
static void PointerHandleAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
static void PointerHandleAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}
static void PointerHandleAxisValue120(void*, wl_pointer*, uint32_t, int32_t) {}
static void PointerHandleAxisRelativeDirection(void*, wl_pointer*, uint32_t, uint32_t) {}

static const wl_pointer_listener pointer_listener = {
	PointerHandleEnter,
	PointerHandleLeave,
	PointerHandleMotion,
	PointerHandleButton,
	PointerHandleAxis,
	PointerHandleFrame,
	PointerHandleAxisSource,
	PointerHandleAxisStop,
	PointerHandleAxisDiscrete,
	PointerHandleAxisValue120,
	PointerHandleAxisRelativeDirection,
};

static void KeyboardHandleKeymap(void*, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size)
{
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
	{
		close(fd);
		return;
	}

	void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (mapped == MAP_FAILED)
		return;

	data->keyboard_state.SetKeymapFromString(static_cast<const char*>(mapped));
	data->repeat_state.Stop();
	munmap(mapped, size);
}

static void KeyboardHandleEnter(void*, wl_keyboard*, uint32_t serial, wl_surface*, wl_array*)
{
	data->system_interface->SetSeatSerial(serial);
}

static void KeyboardHandleLeave(void*, wl_keyboard*, uint32_t, wl_surface*)
{
	RequestRedraw();
	data->repeat_state.Stop();
	data->keyboard_state.Reset();
}

static bool IsTextInputCodepoint(uint32_t codepoint)
{
	return codepoint >= 0x20 && !(codepoint >= 0x7f && codepoint <= 0x9f);
}

static void SubmitKeyDown(xkb_keycode_t keycode)
{
	if (!data->context || !data->keyboard_state.state)
		return;

	RequestRedraw();

	const xkb_keysym_t sym = xkb_state_key_get_one_sym(data->keyboard_state.state, keycode);
	const OnlyWayUi::Input::KeyIdentifier ui_key = OwuiWayland::ConvertKeySym(sym);
	const int modifiers = data->keyboard_state.modifiers;
	const float native_dp_ratio = 1.f;

	if (data->key_down_callback && !data->key_down_callback(data->context, ui_key, modifiers, native_dp_ratio, true))
		return;

	bool propagates = true;
	if (ui_key != OnlyWayUi::Input::KI_UNKNOWN)
		propagates = data->context->ProcessKeyDown(ui_key, modifiers);

	const uint32_t codepoint = xkb_state_key_get_utf32(data->keyboard_state.state, keycode);
	if (ui_key == OnlyWayUi::Input::KI_RETURN || ui_key == OnlyWayUi::Input::KI_NUMPADENTER)
		propagates &= data->context->ProcessTextInput('\n');
	else if (IsTextInputCodepoint(codepoint) && !(modifiers & OnlyWayUi::Input::KM_CTRL))
		propagates &= data->context->ProcessTextInput(OnlyWayUi::Character(codepoint));

	if (propagates && data->key_down_callback)
		data->key_down_callback(data->context, ui_key, modifiers, native_dp_ratio, false);
}

static void StartKeyRepeat(xkb_keycode_t keycode)
{
	KeyboardRepeatState& repeat = data->repeat_state;
	repeat.Stop();

	if (repeat.rate <= 0 || !data->keyboard_state.keymap || !xkb_keymap_key_repeats(data->keyboard_state.keymap, keycode))
		return;

	repeat.active = true;
	repeat.keycode = keycode;
	repeat.interval = std::chrono::milliseconds(std::max(1, 1000 / repeat.rate));
	repeat.next_time = Clock::now() + std::chrono::milliseconds(std::max(0, repeat.delay));
}

static void ProcessKeyRepeats()
{
	KeyboardRepeatState& repeat = data->repeat_state;
	if (!repeat.active)
		return;

	const Clock::time_point now = Clock::now();
	if (now < repeat.next_time)
		return;

	int repeated_keys = 0;
	do
	{
		SubmitKeyDown(repeat.keycode);
		repeat.next_time += repeat.interval;
		repeated_keys += 1;
	} while (repeat.active && repeat.next_time <= now && repeated_keys < 32);

	if (repeated_keys == 32)
		repeat.next_time = now + repeat.interval;
}

static double GetKeyRepeatTimeout(double timeout_seconds)
{
	const KeyboardRepeatState& repeat = data->repeat_state;
	if (!repeat.active)
		return timeout_seconds;

	const Clock::time_point now = Clock::now();
	if (now >= repeat.next_time)
		return 0.0;

	const double repeat_timeout = std::chrono::duration<double>(repeat.next_time - now).count();
	return OnlyWayUi::Math::Min(timeout_seconds, repeat_timeout);
}

static void KeyboardHandleKey(void*, wl_keyboard*, uint32_t serial, uint32_t, uint32_t key, uint32_t state)
{
	if (!data->context || !data->keyboard_state.state)
		return;

	RequestRedraw();
	data->system_interface->SetSeatSerial(serial);

	const xkb_keycode_t keycode = key + 8;
	const bool pressed = (state == WL_KEYBOARD_KEY_STATE_PRESSED);

	if (pressed)
	{
		SubmitKeyDown(keycode);
		StartKeyRepeat(keycode);
	}
	else
	{
		if (data->repeat_state.active && data->repeat_state.keycode == keycode)
			data->repeat_state.Stop();

		const xkb_keysym_t sym = xkb_state_key_get_one_sym(data->keyboard_state.state, keycode);
		const OnlyWayUi::Input::KeyIdentifier ui_key = OwuiWayland::ConvertKeySym(sym);
		const int modifiers = data->keyboard_state.modifiers;
		data->context->ProcessKeyUp(ui_key, modifiers);
	}
}

static void KeyboardHandleModifiers(void*, wl_keyboard*, uint32_t, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
{
	RequestRedraw();
	data->keyboard_state.UpdateModifiers(depressed, latched, locked, group);
}

static void KeyboardHandleRepeatInfo(void*, wl_keyboard*, int32_t rate, int32_t delay)
{
	data->repeat_state.rate = rate;
	data->repeat_state.delay = delay;
	if (rate <= 0)
		data->repeat_state.Stop();
}

static const wl_keyboard_listener keyboard_listener = {
	KeyboardHandleKeymap,
	KeyboardHandleEnter,
	KeyboardHandleLeave,
	KeyboardHandleKey,
	KeyboardHandleModifiers,
	KeyboardHandleRepeatInfo,
};

static void SeatHandleCapabilities(void*, wl_seat* seat, uint32_t capabilities)
{
	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !data->pointer)
	{
		data->pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(data->pointer, &pointer_listener, nullptr);
		data->system_interface->SetPointer(data->pointer);
	}
	else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && data->pointer)
	{
		wl_pointer_destroy(data->pointer);
		data->pointer = nullptr;
		data->system_interface->SetPointer(nullptr);
	}

	if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !data->keyboard)
	{
		data->keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(data->keyboard, &keyboard_listener, nullptr);
	}
	else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && data->keyboard)
	{
		data->repeat_state.Stop();
		data->keyboard_state.Reset();
		wl_keyboard_destroy(data->keyboard);
		data->keyboard = nullptr;
	}
}

static void SeatHandleName(void*, wl_seat*, const char*) {}

static const wl_seat_listener seat_listener = {
	SeatHandleCapabilities,
	SeatHandleName,
};

static bool InitializeWayland(const char* window_name, int width, int height, bool allow_resize)
{
	wl_display* display = wl_display_connect(nullptr);
	if (!display)
	{
		OnlyWayUi::Log::Message(OnlyWayUi::Log::LT_ERROR, "Failed to connect to Wayland display.");
		return false;
	}

	auto new_data = OnlyWayUi::MakeUnique<BackendData>();
	new_data->display = display;
	new_data->registry = wl_display_get_registry(display);
	wl_registry_add_listener(new_data->registry, &registry_listener, &new_data->globals);

	data = std::move(new_data);
	wl_display_roundtrip(display);

	if (!data->globals.compositor || !data->globals.wm_base || !data->globals.shm)
	{
		OnlyWayUi::Log::Message(OnlyWayUi::Log::LT_ERROR, "Required Wayland globals are not available.");
		return false;
	}

	data->system_interface = OnlyWayUi::MakeUnique<SystemInterface_Wayland>(display, data->globals.shm, data->globals.data_device_manager);

	// The clock_id event arrives during the configure wait below, before any elapsed-time consumer runs.
	if (data->globals.presentation)
		wp_presentation_add_listener(data->globals.presentation, &presentation_listener, nullptr);

	xdg_wm_base_add_listener(data->globals.wm_base, &xdg_wm_base_listener, nullptr);

	if (data->globals.seat)
	{
		wl_seat_add_listener(data->globals.seat, &seat_listener, nullptr);
		data->system_interface->SetSeat(data->globals.seat);
	}

	data->surface = wl_compositor_create_surface(data->globals.compositor);
	data->cursor_surface = wl_compositor_create_surface(data->globals.compositor);
	data->system_interface->SetCursorSurface(data->cursor_surface);

	data->shell_surface = xdg_wm_base_get_xdg_surface(data->globals.wm_base, data->surface);
	xdg_surface_add_listener(data->shell_surface, &xdg_surface_listener, nullptr);
	data->shell_toplevel = xdg_surface_get_toplevel(data->shell_surface);
	xdg_toplevel_add_listener(data->shell_toplevel, &xdg_toplevel_listener, nullptr);
	xdg_toplevel_set_title(data->shell_toplevel, window_name);
	if (data->globals.exporter)
	{
		// Export the toplevel so portal dialogs can be parented to it. The handle arrives asynchronously (ExportedHandle).
		data->exported = zxdg_exporter_v2_export_toplevel(data->globals.exporter, data->surface);
		if (data->exported)
			zxdg_exported_v2_add_listener(data->exported, &exported_listener, nullptr);
	}
	if (data->globals.decoration_manager)
	{
		data->shell_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(data->globals.decoration_manager, data->shell_toplevel);
		zxdg_toplevel_decoration_v1_add_listener(data->shell_decoration, &xdg_toplevel_decoration_listener, nullptr);
		zxdg_toplevel_decoration_v1_set_mode(data->shell_decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
	}
	if (!allow_resize)
	{
		xdg_toplevel_set_min_size(data->shell_toplevel, width, height);
		xdg_toplevel_set_max_size(data->shell_toplevel, width, height);
	}

	UpdateWindowSize(width, height);
	wl_surface_commit(data->surface);

	while (!data->configured && wl_display_dispatch(display) != -1) {}

	return data->configured;
}

static bool InitializeEGL()
{
	data->egl_display = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(data->display));
	if (data->egl_display == EGL_NO_DISPLAY)
		return false;

	if (!eglInitialize(data->egl_display, nullptr, nullptr))
		return false;

	if (!eglBindAPI(EGL_OPENGL_API))
		return false;

	const EGLint config_attributes[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 24,
		EGL_STENCIL_SIZE, 8,
		EGL_NONE,
	};

	EGLint config_count = 0;
	if (!eglChooseConfig(data->egl_display, config_attributes, &data->egl_config, 1, &config_count) || config_count == 0)
		return false;

	const EGLint context_attributes[] = {
		EGL_CONTEXT_MAJOR_VERSION, 3,
		EGL_CONTEXT_MINOR_VERSION, 3,
		EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
		EGL_NONE,
	};

	data->egl_context = eglCreateContext(data->egl_display, data->egl_config, EGL_NO_CONTEXT, context_attributes);
	if (data->egl_context == EGL_NO_CONTEXT)
		return false;

	data->egl_window = wl_egl_window_create(data->surface, data->width, data->height);
	if (!data->egl_window)
		return false;

	data->egl_surface = eglCreateWindowSurface(data->egl_display, data->egl_config, reinterpret_cast<EGLNativeWindowType>(data->egl_window), nullptr);
	if (data->egl_surface == EGL_NO_SURFACE)
		return false;

	if (!eglMakeCurrent(data->egl_display, data->egl_surface, data->egl_surface, data->egl_context))
		return false;

	InitializeEglDamageSupport();

	// Present without blocking: eglSwapBuffers under an interval >= 1 would park the main thread until the next frame
	// throttle, stalling Wayland event dispatch (and thus clipboard sends) whenever the compositor withholds frames,
	// e.g. while occluded. Frame pacing is instead driven by wl_surface_frame callbacks in PresentFrame.
	eglSwapInterval(data->egl_display, 0);
	return true;
}

// A file descriptor that a backend service needs the main loop to poll, beyond the Wayland display fd (which is handled
// separately because it uses the prepare_read/read_events protocol). Clipboard transfers and the D-Bus connection
// register their fds here so a single poll wakes on any of them.
struct PollSource {
	int fd = -1;
	short events = 0;
	// Invoked after poll with the fd's returned events when they are non-zero. Left null by services that instead
	// process their fds unconditionally after dispatch (the clipboard does this); such a source only serves to wake the
	// poll and, through max_timeout, bound how long it may sleep.
	void (*ready)(void* userdata, short revents) = nullptr;
	void* userdata = nullptr;
	// When >= 0, clamp the poll timeout (seconds) so the service is serviced promptly. Negative means no clamp.
	double max_timeout = -1.0;
};

static short DbusWatchEventsToPoll(unsigned int flags)
{
	short events = 0;
	if (flags & DBUS_WATCH_READABLE)
		events |= POLLIN;
	if (flags & DBUS_WATCH_WRITABLE)
		events |= POLLOUT;
	return events;
}

// Poll-source callback for a D-Bus watch: hand the ready events back to libdbus. Guards against a watch that libdbus
// removed earlier in the same poll pass (handling one watch can tear down another, whose fd may then be closed).
static void DbusWatchReady(void* userdata, short revents)
{
	DBusWatch* watch = static_cast<DBusWatch*>(userdata);
	OnlyWayUi::Vector<DBusWatch*>& watches = data->dbus.watches;
	if (std::find(watches.begin(), watches.end(), watch) == watches.end())
		return;

	unsigned int flags = 0;
	if (revents & POLLIN)
		flags |= DBUS_WATCH_READABLE;
	if (revents & POLLOUT)
		flags |= DBUS_WATCH_WRITABLE;
	if (revents & POLLERR)
		flags |= DBUS_WATCH_ERROR;
	if (revents & POLLHUP)
		flags |= DBUS_WATCH_HANGUP;
	dbus_watch_handle(watch, flags);
}

static dbus_bool_t DbusAddWatch(DBusWatch* watch, void*)
{
	data->dbus.watches.push_back(watch);
	return TRUE;
}

static void DbusRemoveWatch(DBusWatch* watch, void*)
{
	OnlyWayUi::Vector<DBusWatch*>& watches = data->dbus.watches;
	watches.erase(std::remove(watches.begin(), watches.end(), watch), watches.end());
}

static void DbusWatchToggled(DBusWatch*, void*)
{
	// The enabled state is re-checked when sources are collected, so nothing to do here.
}

static Clock::time_point DbusTimeoutDeadline(DBusTimeout* timeout)
{
	if (!dbus_timeout_get_enabled(timeout))
		return Clock::time_point::max();
	return Clock::now() + std::chrono::milliseconds(dbus_timeout_get_interval(timeout));
}

static dbus_bool_t DbusAddTimeout(DBusTimeout* timeout, void*)
{
	data->dbus.timeouts.push_back(DbusTimeoutRecord{timeout, DbusTimeoutDeadline(timeout)});
	return TRUE;
}

static void DbusRemoveTimeout(DBusTimeout* timeout, void*)
{
	OnlyWayUi::Vector<DbusTimeoutRecord>& timeouts = data->dbus.timeouts;
	timeouts.erase(std::remove_if(timeouts.begin(), timeouts.end(),
					   [timeout](const DbusTimeoutRecord& record) { return record.timeout == timeout; }),
		timeouts.end());
}

static void DbusTimeoutToggled(DBusTimeout* timeout, void*)
{
	for (DbusTimeoutRecord& record : data->dbus.timeouts)
		if (record.timeout == timeout)
			record.deadline = DbusTimeoutDeadline(timeout);
}

// Seconds until the soonest enabled D-Bus timeout fires, or a negative value if none, so the poll wakes in time to
// service it. A deadline already in the past clamps to zero.
static double DbusNextTimeoutSeconds()
{
	bool any = false;
	Clock::time_point soonest = Clock::time_point::max();
	for (const DbusTimeoutRecord& record : data->dbus.timeouts)
	{
		if (record.deadline == Clock::time_point::max())
			continue;
		if (!any || record.deadline < soonest)
		{
			soonest = record.deadline;
			any = true;
		}
	}
	if (!any)
		return -1.0;
	const double seconds = std::chrono::duration<double>(soonest - Clock::now()).count();
	return seconds < 0.0 ? 0.0 : seconds;
}

// Fire any elapsed D-Bus timeouts and turn freshly read bytes into message dispatches (pending-call replies, signals).
static void ProcessDbus()
{
	if (!data->dbus.connection)
		return;

	const Clock::time_point now = Clock::now();
	// Snapshot the due timeouts first: handling one may add or remove others, invalidating the container's iterators.
	OnlyWayUi::Vector<DBusTimeout*> due;
	for (const DbusTimeoutRecord& record : data->dbus.timeouts)
		if (record.deadline != Clock::time_point::max() && now >= record.deadline)
			due.push_back(record.timeout);
	for (DBusTimeout* timeout : due)
	{
		bool present = false;
		for (const DbusTimeoutRecord& record : data->dbus.timeouts)
			if (record.timeout == timeout)
			{
				present = true;
				break;
			}
		if (!present)
			continue;
		dbus_timeout_handle(timeout);
		// A repeating timeout stays enabled without a toggle callback, so re-arm its deadline from now. If handling
		// removed the timeout, the matching record is already gone and this loop simply finds nothing to update.
		for (DbusTimeoutRecord& record : data->dbus.timeouts)
			if (record.timeout == timeout)
				record.deadline = DbusTimeoutDeadline(timeout);
	}

	while (dbus_connection_dispatch(data->dbus.connection) == DBUS_DISPATCH_DATA_REMAINS)
	{
	}
}

// Connect to the session bus and register our main-loop integration. Returns the connection, or null if no session bus
// is reachable, in which case the caller falls back gracefully. Created once and reused across dialog requests.
static DBusConnection* EnsureDbusConnection()
{
	if (data->dbus.connection)
		return data->dbus.connection;

	DBusError error = DBUS_ERROR_INIT;
	DBusConnection* connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
	if (!connection)
	{
		data->system_interface->LogMessage(OnlyWayUi::Log::LT_INFO,
			OnlyWayUi::String("Session D-Bus unavailable, file dialogs fall back: ") +
				(dbus_error_is_set(&error) ? error.message : "unknown error"));
		dbus_error_free(&error);
		return nullptr;
	}

	// A dropped bus must not terminate the application; we own teardown in ShutdownDbus.
	dbus_connection_set_exit_on_disconnect(connection, FALSE);

	if (!dbus_connection_set_watch_functions(
			connection, DbusAddWatch, DbusRemoveWatch, DbusWatchToggled, nullptr, nullptr) ||
		!dbus_connection_set_timeout_functions(
			connection, DbusAddTimeout, DbusRemoveTimeout, DbusTimeoutToggled, nullptr, nullptr))
	{
		data->system_interface->LogMessage(OnlyWayUi::Log::LT_WARNING, "Failed to register D-Bus main-loop callbacks.");
		dbus_connection_close(connection);
		dbus_connection_unref(connection);
		data->dbus.watches.clear();
		data->dbus.timeouts.clear();
		return nullptr;
	}

	data->dbus.connection = connection;
	return connection;
}

static void ShutdownDbus()
{
	if (!data->dbus.connection)
		return;
	// Clear the callbacks before closing so libdbus does not call back into us mid-teardown.
	dbus_connection_set_watch_functions(data->dbus.connection, nullptr, nullptr, nullptr, nullptr, nullptr);
	dbus_connection_set_timeout_functions(data->dbus.connection, nullptr, nullptr, nullptr, nullptr, nullptr);
	dbus_connection_close(data->dbus.connection);
	dbus_connection_unref(data->dbus.connection);
	data->dbus.connection = nullptr;
	data->dbus.watches.clear();
	data->dbus.timeouts.clear();
	// Drop any in-flight dialogs without firing their callbacks; the application is shutting down.
	data->dbus.file_dialogs.clear();
	data->dbus.response_filter_installed = false;
}

static void LogDbusError(OnlyWayUi::Log::Type type, const char* message, DBusError& error)
{
	data->system_interface->LogMessage(type,
		OnlyWayUi::String(message) + (dbus_error_is_set(&error) ? error.message : "unknown error"));
	dbus_error_free(&error);
}

// Make sure the portal service is running before ShowFileDialog returns. Without this check, a missing portal would only
// be reported later by the method reply and callers could not use the documented immediate fallback path.
static bool StartPortalService(DBusConnection* connection)
{
	DBusError error = DBUS_ERROR_INIT;
	dbus_uint32_t reply = 0;
	if (!dbus_bus_start_service_by_name(connection, PortalDesktopService, 0, &reply, &error))
	{
		LogDbusError(OnlyWayUi::Log::LT_INFO, "Desktop portal unavailable, file dialogs fall back: ", error);
		return false;
	}

	if (reply != DBUS_START_REPLY_SUCCESS && reply != DBUS_START_REPLY_ALREADY_RUNNING)
	{
		data->system_interface->LogMessage(
			OnlyWayUi::Log::LT_INFO, "Desktop portal unavailable, file dialogs fall back: unexpected service-start reply.");
		return false;
	}

	return true;
}

static bool GetPortalOwner(DBusConnection* connection, OnlyWayUi::String& owner)
{
	DBusMessage* message =
		dbus_message_new_method_call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, "GetNameOwner");
	if (!message)
		return false;

	const char* service = PortalDesktopService;
	if (!dbus_message_append_args(message, DBUS_TYPE_STRING, &service, DBUS_TYPE_INVALID))
	{
		dbus_message_unref(message);
		data->system_interface->LogMessage(
			OnlyWayUi::Log::LT_WARNING, "Failed to build desktop portal owner lookup request.");
		return false;
	}

	DBusError error = DBUS_ERROR_INIT;
	DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, message, DBUS_TIMEOUT_USE_DEFAULT, &error);
	dbus_message_unref(message);
	if (!reply)
	{
		LogDbusError(OnlyWayUi::Log::LT_INFO, "Desktop portal owner unavailable, file dialogs fall back: ", error);
		return false;
	}

	bool ok = false;
	const char* value = nullptr;
	if (dbus_message_get_args(reply, &error, DBUS_TYPE_STRING, &value, DBUS_TYPE_INVALID) && value && value[0] == ':')
	{
		owner = value;
		ok = true;
	}
	else if (dbus_error_is_set(&error))
	{
		LogDbusError(OnlyWayUi::Log::LT_INFO, "Desktop portal owner lookup failed, file dialogs fall back: ", error);
	}
	else
	{
		data->system_interface->LogMessage(
			OnlyWayUi::Log::LT_INFO, "Desktop portal owner lookup returned an invalid owner.");
	}
	dbus_message_unref(reply);
	return ok;
}

static bool EnsurePortalReady(DBusConnection* connection, OnlyWayUi::String& portal_owner)
{
	return StartPortalService(connection) && GetPortalOwner(connection, portal_owner);
}

// Parse a Request.Response message (u response, a{sv} results) into result, decoding the "uris" entry into local paths.
// Leaves result.accepted false on cancellation (response != 0) or if no usable uris are present.
static void ParseFileDialogResponse(DBusMessage* message, Backend::FileDialogResult& result)
{
	DBusMessageIter iter;
	if (!dbus_message_iter_init(message, &iter) || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UINT32)
		return;
	dbus_uint32_t response = 0;
	dbus_message_iter_get_basic(&iter, &response);
	if (response != 0)
		return;

	if (!dbus_message_iter_next(&iter) || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
		return;
	DBusMessageIter results;
	dbus_message_iter_recurse(&iter, &results);
	while (dbus_message_iter_get_arg_type(&results) == DBUS_TYPE_DICT_ENTRY)
	{
		DBusMessageIter entry;
		dbus_message_iter_recurse(&results, &entry);
		const char* key = nullptr;
		if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING)
			dbus_message_iter_get_basic(&entry, &key);

		if (key && std::strcmp(key, "uris") == 0 && dbus_message_iter_next(&entry) &&
			dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_VARIANT)
		{
			DBusMessageIter variant;
			dbus_message_iter_recurse(&entry, &variant);
			if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_ARRAY)
			{
				DBusMessageIter uris;
				dbus_message_iter_recurse(&variant, &uris);
				while (dbus_message_iter_get_arg_type(&uris) == DBUS_TYPE_STRING)
				{
					const char* uri = nullptr;
					dbus_message_iter_get_basic(&uris, &uri);
					OnlyWayUi::String path;
					if (uri && Backend::PortalUriToLocalPath(uri, path))
						result.paths.push_back(std::move(path));
					dbus_message_iter_next(&uris);
				}
			}
		}
		dbus_message_iter_next(&results);
	}

	result.accepted = !result.paths.empty();
}

// Connection filter for the portal's Request.Response signals. Matches the signal to a pending dialog by object path,
// verifies that it came from the portal owner that accepted the request, delivers the parsed result to its callback, and
// drops the pending entry.
static DBusHandlerResult DbusResponseFilter(DBusConnection*, DBusMessage* message, void*)
{
	if (!dbus_message_is_signal(message, "org.freedesktop.portal.Request", "Response"))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	const char* path = dbus_message_get_path(message);
	if (!path)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	const char* sender = dbus_message_get_sender(message);
	if (!sender)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	OnlyWayUi::UniquePtr<FileDialogPending> pending;
	OnlyWayUi::Vector<OnlyWayUi::UniquePtr<FileDialogPending>>& dialogs = data->dbus.file_dialogs;
	for (size_t i = 0; i < dialogs.size(); ++i)
	{
		if (dialogs[i]->request_path == path)
		{
			if (dialogs[i]->portal_owner != sender)
				return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
			pending = std::move(dialogs[i]);
			dialogs.erase(dialogs.begin() + i);
			break;
		}
	}
	if (!pending)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	Backend::FileDialogResult result;
	ParseFileDialogResponse(message, result);
	pending->callback(result);
	return DBUS_HANDLER_RESULT_HANDLED;
}

// Append one a{sv} dict entry whose variant holds a string value.
static void AppendDictString(DBusMessageIter* dict, const char* key, const char* value)
{
	DBusMessageIter entry, variant;
	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, DBUS_TYPE_STRING_AS_STRING, &variant);
	dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
	dbus_message_iter_close_container(&entry, &variant);
	dbus_message_iter_close_container(dict, &entry);
}

// Append one a{sv} dict entry whose variant holds a boolean value.
static void AppendDictBool(DBusMessageIter* dict, const char* key, bool value)
{
	DBusMessageIter entry, variant;
	dbus_bool_t boolean = value ? TRUE : FALSE;
	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, DBUS_TYPE_BOOLEAN_AS_STRING, &variant);
	dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &boolean);
	dbus_message_iter_close_container(&entry, &variant);
	dbus_message_iter_close_container(dict, &entry);
}

// Append one a{sv} dict entry whose variant holds a filesystem path as a nul-terminated byte array (ay), which is how
// the portal expects current_folder.
static void AppendDictPath(DBusMessageIter* dict, const char* key, const OnlyWayUi::String& path)
{
	DBusMessageIter entry, variant, array;
	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
	dbus_message_iter_open_container(
		&entry, DBUS_TYPE_VARIANT, DBUS_TYPE_ARRAY_AS_STRING DBUS_TYPE_BYTE_AS_STRING, &variant);
	dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE_AS_STRING, &array);
	const char* bytes = path.c_str();
	const int byte_count = int(path.size()) + 1; // include the terminating nul the portal requires
	dbus_message_iter_append_fixed_array(&array, DBUS_TYPE_BYTE, &bytes, byte_count);
	dbus_message_iter_close_container(&variant, &array);
	dbus_message_iter_close_container(&entry, &variant);
	dbus_message_iter_close_container(dict, &entry);
}

// Append the "filters" a{sv} dict entry: a(sa(us)) where each filter is a name and a list of (0 = glob pattern, string).
static void AppendDictFilters(DBusMessageIter* dict, const OnlyWayUi::Vector<Backend::FileDialogFilter>& filters)
{
	DBusMessageIter entry, variant, filter_array;
	const char* key = "filters";
	dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "a(sa(us))", &variant);
	dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "(sa(us))", &filter_array);
	for (const Backend::FileDialogFilter& filter : filters)
	{
		DBusMessageIter filter_struct, pattern_array;
		dbus_message_iter_open_container(&filter_array, DBUS_TYPE_STRUCT, nullptr, &filter_struct);
		const char* name = filter.name.c_str();
		dbus_message_iter_append_basic(&filter_struct, DBUS_TYPE_STRING, &name);
		dbus_message_iter_open_container(&filter_struct, DBUS_TYPE_ARRAY, "(us)", &pattern_array);
		for (const OnlyWayUi::String& pattern : filter.patterns)
		{
			DBusMessageIter pattern_struct;
			dbus_message_iter_open_container(&pattern_array, DBUS_TYPE_STRUCT, nullptr, &pattern_struct);
			dbus_uint32_t kind = 0; // 0 marks a glob-style pattern
			const char* text = pattern.c_str();
			dbus_message_iter_append_basic(&pattern_struct, DBUS_TYPE_UINT32, &kind);
			dbus_message_iter_append_basic(&pattern_struct, DBUS_TYPE_STRING, &text);
			dbus_message_iter_close_container(&pattern_array, &pattern_struct);
		}
		dbus_message_iter_close_container(&filter_struct, &pattern_array);
		dbus_message_iter_close_container(&filter_array, &filter_struct);
	}
	dbus_message_iter_close_container(&variant, &filter_array);
	dbus_message_iter_close_container(&entry, &variant);
	dbus_message_iter_close_container(dict, &entry);
}

static char HexDigit(unsigned int value)
{
	return value < 10 ? char('0' + value) : char('a' + value - 10);
}

static bool FillRandomBytes(unsigned char* bytes, size_t byte_count)
{
	size_t offset = 0;
	while (offset < byte_count)
	{
		const ssize_t result = getrandom(bytes + offset, byte_count - offset, 0);
		if (result > 0)
		{
			offset += size_t(result);
			continue;
		}
		if (result < 0 && errno == EINTR)
			continue;

		data->system_interface->LogMessage(
			OnlyWayUi::Log::LT_WARNING, "Failed to read random bytes for portal request token.");
		return false;
	}
	return true;
}

// Build a unique handle token and the Request object path the portal will use for it. The path format is documented and
// deterministic (/org/freedesktop/portal/desktop/request/SENDER/TOKEN), so we can watch for the Response before calling.
// The token itself must not be guessable because the response signal is matched by this object path.
static bool MakeRequestToken(OnlyWayUi::String& token, OnlyWayUi::String& request_path)
{
	const char* unique_name = dbus_bus_get_unique_name(data->dbus.connection);
	if (!unique_name)
		return false;

	// SENDER is the unique name without the leading ':' and with each '.' replaced by '_'.
	OnlyWayUi::String sender = unique_name;
	if (!sender.empty() && sender[0] == ':')
		sender.erase(0, 1);
	for (char& c : sender)
		if (c == '.')
			c = '_';

	unsigned char random_bytes[16];
	if (!FillRandomBytes(random_bytes, sizeof(random_bytes)))
		return false;

	token = "owui";
	token.reserve(4 + sizeof(random_bytes) * 2);
	for (unsigned char byte : random_bytes)
	{
		token.push_back(HexDigit(byte >> 4));
		token.push_back(HexDigit(byte & 0x0f));
	}
	request_path = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
	return true;
}

// Send an OpenFile/SaveFile request to the portal and record the pending dialog. Returns false if the request could not
// be started, in which case no callback will fire.
static bool SendFileDialog(const Backend::FileDialogRequest& request, Backend::FileDialogCallback callback)
{
	DBusConnection* connection = EnsureDbusConnection();
	if (!connection)
		return false;

	OnlyWayUi::String portal_owner;
	if (!EnsurePortalReady(connection, portal_owner))
		return false;

	// Subscribe to Response signals once, before any call, so no response can race ahead of the subscription.
	if (!data->dbus.response_filter_installed)
	{
		DBusError error = DBUS_ERROR_INIT;
		dbus_bus_add_match(connection,
			"type='signal',interface='org.freedesktop.portal.Request',member='Response'",
			&error);
		if (dbus_error_is_set(&error))
		{
			data->system_interface->LogMessage(OnlyWayUi::Log::LT_WARNING,
				OnlyWayUi::String("Failed to subscribe to portal responses: ") + error.message);
			dbus_error_free(&error);
			return false;
		}
		if (!dbus_connection_add_filter(connection, DbusResponseFilter, nullptr, nullptr))
		{
			data->system_interface->LogMessage(OnlyWayUi::Log::LT_WARNING, "Failed to add portal response filter.");
			return false;
		}
		data->dbus.response_filter_installed = true;
	}

	OnlyWayUi::String token, request_path;
	if (!MakeRequestToken(token, request_path))
		return false;

	const char* method = (request.mode == Backend::FileDialogMode::Save) ? "SaveFile" : "OpenFile";
	DBusMessage* message = dbus_message_new_method_call("org.freedesktop.portal.Desktop",
		"/org/freedesktop/portal/desktop", "org.freedesktop.portal.FileChooser", method);
	if (!message)
		return false;

	DBusMessageIter iter;
	dbus_message_iter_init_append(message, &iter);
	// The exported toplevel handle parents the dialog to our window; empty (no handle yet) is a valid unparented call.
	const char* parent_window = data->parent_window_handle.c_str();
	const char* title = request.title.c_str();
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &parent_window);
	dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &title);

	DBusMessageIter options;
	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &options);
	AppendDictString(&options, "handle_token", token.c_str());
	if (request.mode == Backend::FileDialogMode::Open)
	{
		if (request.allow_multiple)
			AppendDictBool(&options, "multiple", true);
	}
	else if (!request.suggested_name.empty())
	{
		AppendDictString(&options, "current_name", request.suggested_name.c_str());
	}
	if (!request.current_folder.empty())
		AppendDictPath(&options, "current_folder", request.current_folder);
	if (!request.filters.empty())
		AppendDictFilters(&options, request.filters);
	dbus_message_iter_close_container(&iter, &options);

	DBusError error = DBUS_ERROR_INIT;
	DBusMessage* reply = dbus_connection_send_with_reply_and_block(connection, message, DBUS_TIMEOUT_USE_DEFAULT, &error);
	dbus_message_unref(message);
	if (!reply)
	{
		LogDbusError(OnlyWayUi::Log::LT_INFO, "Desktop portal file dialog could not be started: ", error);
		return false;
	}

	const char* reply_sender = dbus_message_get_sender(reply);
	if (reply_sender && reply_sender[0] == ':')
		portal_owner = reply_sender;

	DBusMessageIter reply_iter;
	if (!dbus_message_iter_init(reply, &reply_iter) || dbus_message_iter_get_arg_type(&reply_iter) != DBUS_TYPE_OBJECT_PATH)
	{
		dbus_message_unref(reply);
		data->system_interface->LogMessage(
			OnlyWayUi::Log::LT_INFO, "Desktop portal file dialog returned an invalid request handle.");
		return false;
	}

	const char* handle = nullptr;
	dbus_message_iter_get_basic(&reply_iter, &handle);
	if (handle && request_path != handle)
		request_path = handle;
	dbus_message_unref(reply);

	// Record the pending dialog after the portal has accepted the request. The Response signal is delivered later through
	// ProcessDbus and is matched by both request path and portal owner.
	OnlyWayUi::UniquePtr<FileDialogPending> pending = OnlyWayUi::MakeUnique<FileDialogPending>();
	pending->request_path = std::move(request_path);
	pending->portal_owner = std::move(portal_owner);
	pending->callback = std::move(callback);
	data->dbus.file_dialogs.push_back(std::move(pending));
	return true;
}

// Gather the extra fds to poll this iteration. The set is rebuilt each call because it changes as clipboard transfers
// start and finish and as D-Bus adds or removes watches.
static void CollectExtraPollSources(OnlyWayUi::Vector<PollSource>& sources)
{
	if (const int fd = data->system_interface->GetClipboardReadFd(); fd >= 0)
		sources.push_back(PollSource{fd, POLLIN, nullptr, nullptr, ClipboardPollTimeout});
	if (const int fd = data->system_interface->GetClipboardWriteFd(); fd >= 0)
		sources.push_back(PollSource{fd, POLLOUT, nullptr, nullptr, ClipboardPollTimeout});

	for (DBusWatch* watch : data->dbus.watches)
	{
		if (!dbus_watch_get_enabled(watch))
			continue;
		const short events = DbusWatchEventsToPoll(dbus_watch_get_flags(watch));
		if (events == 0)
			continue;
		sources.push_back(PollSource{dbus_watch_get_unix_fd(watch), events, DbusWatchReady, watch, -1.0});
	}
}

static bool DispatchWaylandEvents(double timeout_seconds, bool& timed_out)
{
	timed_out = false;
	wl_display* display = data->display;

	while (wl_display_prepare_read(display) != 0)
	{
		if (wl_display_dispatch_pending(display) < 0)
			return false;
	}

	wl_display_flush(display);

	// The Wayland display fd is always index 0; each extra source follows at index i + 1, so revents map back by
	// position after poll.
	pollfd display_fd {};
	display_fd.fd = wl_display_get_fd(display);
	display_fd.events = POLLIN;

	OnlyWayUi::Vector<pollfd> poll_fds;
	poll_fds.push_back(display_fd);

	OnlyWayUi::Vector<PollSource> sources;
	CollectExtraPollSources(sources);
	for (const PollSource& source : sources)
	{
		poll_fds.push_back(pollfd{source.fd, source.events, 0});
		if (source.max_timeout >= 0.0)
			timeout_seconds = OnlyWayUi::Math::Min(timeout_seconds, source.max_timeout);
	}
	if (const double dbus_timeout = DbusNextTimeoutSeconds(); dbus_timeout >= 0.0)
		timeout_seconds = OnlyWayUi::Math::Min(timeout_seconds, dbus_timeout);

	const int timeout_ms = int(std::ceil(timeout_seconds * 1000.0));
	const int poll_result = poll(poll_fds.data(), poll_fds.size(), timeout_ms);
	timed_out = (poll_result == 0);
	if (poll_result > 0 && (poll_fds[0].revents & POLLIN))
	{
		if (wl_display_read_events(display) < 0)
			return false;
	}
	else
	{
		wl_display_cancel_read(display);
	}

	while (wl_display_dispatch_pending(display) > 0) {}

	if (poll_result > 0)
	{
		for (size_t i = 0; i < sources.size(); ++i)
		{
			const short revents = poll_fds[i + 1].revents;
			if (sources[i].ready && revents != 0)
				sources[i].ready(sources[i].userdata, revents);
		}
	}

	if (data->system_interface->GetClipboardReadFd() >= 0)
		data->system_interface->ProcessClipboardRead();
	if (data->system_interface->GetClipboardWriteFd() >= 0)
		data->system_interface->ProcessClipboardWrite();

	ProcessDbus();

	return wl_display_get_error(display) == 0;
}

bool Backend::Initialize(const char* window_name, int width, int height, bool allow_resize)
{
	OWUI_ASSERT(!data);

	const auto shutdown_on_failure = [] {
		if (data)
			Backend::Shutdown();
		return false;
	};

	if (!InitializeWayland(window_name, width, height, allow_resize))
		return shutdown_on_failure();

	if (!InitializeEGL())
		return shutdown_on_failure();

	OnlyWayUi::String renderer_message;
	if (!OwuiGL3::Initialize(&renderer_message))
		return shutdown_on_failure();

	data->render_interface = OnlyWayUi::MakeUnique<RenderInterface_GL3>();
	if (!data->render_interface || !*data->render_interface)
		return shutdown_on_failure();

	data->render_interface->SetViewport(data->width, data->height);
	data->system_interface->LogMessage(OnlyWayUi::Log::LT_INFO, renderer_message);
	return true;
}

void Backend::Shutdown()
{
	OWUI_ASSERT(data);

	ShutdownDbus();
	data->render_interface.reset();
	OwuiGL3::Shutdown();
	data->system_interface.reset();

	if (data->egl_display != EGL_NO_DISPLAY)
	{
		eglMakeCurrent(data->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (data->egl_surface != EGL_NO_SURFACE)
			eglDestroySurface(data->egl_display, data->egl_surface);
		if (data->egl_context != EGL_NO_CONTEXT)
			eglDestroyContext(data->egl_display, data->egl_context);
		eglTerminate(data->egl_display);
	}

	for (struct wp_presentation_feedback* feedback : data->presentation_feedbacks)
		wp_presentation_feedback_destroy(feedback);
	if (data->frame_callback)
		wl_callback_destroy(data->frame_callback);
	if (data->egl_window)
		wl_egl_window_destroy(data->egl_window);
	if (data->keyboard)
		wl_keyboard_destroy(data->keyboard);
	if (data->pointer)
		wl_pointer_destroy(data->pointer);
	if (data->exported)
		zxdg_exported_v2_destroy(data->exported);
	if (data->shell_decoration)
		zxdg_toplevel_decoration_v1_destroy(data->shell_decoration);
	if (data->shell_toplevel)
		xdg_toplevel_destroy(data->shell_toplevel);
	if (data->shell_surface)
		xdg_surface_destroy(data->shell_surface);
	if (data->cursor_surface)
		wl_surface_destroy(data->cursor_surface);
	if (data->surface)
		wl_surface_destroy(data->surface);
	if (data->globals.seat)
		wl_seat_destroy(data->globals.seat);
	if (data->globals.data_device_manager)
		wl_data_device_manager_destroy(data->globals.data_device_manager);
	if (data->globals.wm_base)
		xdg_wm_base_destroy(data->globals.wm_base);
	if (data->globals.decoration_manager)
		zxdg_decoration_manager_v1_destroy(data->globals.decoration_manager);
	if (data->globals.exporter)
		zxdg_exporter_v2_destroy(data->globals.exporter);
	if (data->globals.presentation)
		wp_presentation_destroy(data->globals.presentation);
	if (data->globals.shm)
		wl_shm_destroy(data->globals.shm);
	if (data->globals.compositor)
		wl_compositor_destroy(data->globals.compositor);
	if (data->registry)
		wl_registry_destroy(data->registry);
	if (data->display)
		wl_display_disconnect(data->display);

	data.reset();
}

OnlyWayUi::SystemInterface* Backend::GetSystemInterface()
{
	OWUI_ASSERT(data);
	return data->system_interface.get();
}

OnlyWayUi::RenderInterface* Backend::GetRenderInterface()
{
	OWUI_ASSERT(data);
	return data->render_interface.get();
}

bool Backend::ShowFileDialog(const FileDialogRequest& request, FileDialogCallback callback)
{
	OWUI_ASSERT(data);
	if (!callback)
		return false;
	return SendFileDialog(request, std::move(callback));
}

bool Backend::ProcessEvents(OnlyWayUi::Context* context, KeyDownCallback key_down_callback, bool power_save)
{
	OWUI_ASSERT(data && context);

	if (!data->running)
		return false;

	if (data->context_dimensions_dirty)
	{
		data->context_dimensions_dirty = false;
		context->SetDimensions({data->width, data->height});
		context->SetDensityIndependentPixelRatio(1.f);
	}

	data->context = context;
	data->key_down_callback = key_down_callback;

	// Return immediately when the context already has visual changes or an animation is scheduled (OnlyWayUi requests an
	// immediate update, i.e. a delay of zero). Clean non-power-saving loops still wake on a display-rate timeout below
	// so app-side update code can run without spinning at 100% CPU.
	const double next_update_delay = context->GetNextUpdateDelay();
	if (context->IsRenderDirty() || next_update_delay <= 0.0)
		data->needs_redraw = true;

	// Choose how long to block:
	// - Something to update and the compositor is ready: return immediately so the app can update and maybe render.
	// - Something to update but awaiting the frame callback: pace to the compositor; the callback (or input) wakes us.
	// - Clean non-power-saving loop: wake at about the display rate so app-side updates keep running without GPU work.
	// - Clean power-saving loop: idle until the next scheduled update or input arrives. The cap bounds how long an
	//   out-of-contract application change can go unseen, matching the platform-independent sample loop behaviour.
	double timeout_seconds;
	if (data->needs_redraw && data->frame_ready)
		timeout_seconds = 0.0;
	else if (data->needs_redraw)
		timeout_seconds = FramePacingTimeout;
	else if (!power_save)
		timeout_seconds = data->last_refresh_interval > 0.0 ? data->last_refresh_interval : DefaultRefreshInterval;
	else
		timeout_seconds = OnlyWayUi::Math::Min(next_update_delay, 10.0);
	timeout_seconds = GetKeyRepeatTimeout(timeout_seconds);

	// The dispatch below can block long after the last prediction was made; let input handlers read the live clock
	// instead of a stale frame time.
	data->system_interface->ClearPredictedFrameTime();

	bool timed_out = false;
	if (!DispatchWaylandEvents(timeout_seconds, timed_out))
		data->running = false;

	// An expired wait means a scheduled update came due or we hit the idle cap; wake the app so animations advance and any
	// application-driven change is picked up even if the application did not request an update.
	if (timed_out)
		data->needs_redraw = true;

	ProcessKeyRepeats();

	// The application updates the context right after we return; lock its time base to this frame's predicted display time.
	UpdatePredictedFrameTime();

	data->context = nullptr;
	data->key_down_callback = nullptr;

	// Honor a compositor close request unless the application opted to intercept it. Interception keeps the window open so
	// the application can, for example, confirm unsaved work and then exit on its own terms via RequestExit.
	if (data->close_requested && !data->intercept_close)
		data->running = false;

	return data->running;
}

void Backend::RequestExit()
{
	OWUI_ASSERT(data);
	data->running = false;
}

void Backend::SetCloseRequestInterception(bool intercept)
{
	OWUI_ASSERT(data);
	data->intercept_close = intercept;
}

bool Backend::ConsumeCloseRequest()
{
	OWUI_ASSERT(data);
	const bool requested = data->close_requested;
	data->close_requested = false;
	return requested;
}

static void SurfaceFrameHandleDone(void*, wl_callback* callback, uint32_t)
{
	wl_callback_destroy(callback);
	if (data && data->frame_callback == callback)
	{
		data->frame_callback = nullptr;
		data->frame_ready = true;
	}
}

static const wl_callback_listener surface_frame_listener = {
	SurfaceFrameHandleDone,
};

static void SubmitWaylandDamage()
{
	const uint32_t surface_version = wl_surface_get_version(data->surface);
	for (OnlyWayUi::Rectanglei region : data->surface_damage_regions)
	{
		if (surface_version >= WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION)
			wl_surface_damage_buffer(data->surface, region.Left(), region.Top(), region.Width(), region.Height());
		else
			wl_surface_damage(data->surface, region.Left(), region.Top(), region.Width(), region.Height());
	}
}

static OnlyWayUi::Vector<EGLint> MakeEglDamageRectangles()
{
	OnlyWayUi::Vector<EGLint> rectangles;
	rectangles.reserve(data->surface_damage_regions.size() * 4);
	for (OnlyWayUi::Rectanglei region : data->surface_damage_regions)
	{
		rectangles.push_back(EGLint(region.Left()));
		rectangles.push_back(EGLint(data->height - region.Bottom()));
		rectangles.push_back(EGLint(region.Width()));
		rectangles.push_back(EGLint(region.Height()));
	}
	return rectangles;
}

static EGLBoolean SwapBuffers()
{
	// Submit Wayland damage explicitly so the surface damage is correct even when EGL damage-swap is unavailable or
	// implemented without forwarding damage to the Wayland surface. Duplicate damage is harmless; the compositor unions it.
	SubmitWaylandDamage();

	if (data->egl_swap_buffers_with_damage && !data->surface_damage_regions.empty())
	{
		const OnlyWayUi::Vector<EGLint> rectangles = MakeEglDamageRectangles();
		return data->egl_swap_buffers_with_damage(data->egl_display, data->egl_surface, rectangles.data(), EGLint(data->surface_damage_regions.size()));
	}

	return eglSwapBuffers(data->egl_display, data->egl_surface);
}

void Backend::BeginFrame()
{
	OWUI_ASSERT(data && data->render_interface);
	UpdateEglBufferAge();
	AddFullDamageRegion(data->surface_damage_regions);
	AddFullDamageRegion(data->frame_damage_regions);
	data->frame_damage_bounds = JoinDamageRegions(data->frame_damage_regions);
	data->render_interface->BeginFrame();
}

static void BeginFrameWithDamage(OnlyWayUi::Context* context)
{
	OWUI_ASSERT(data && data->render_interface && context);
	UpdateEglBufferAge();
	PrepareFrameDamage(context);
	data->render_interface->BeginFrame(data->frame_damage_bounds);
}

void Backend::PresentFrame()
{
	OWUI_ASSERT(data && data->render_interface);
	data->render_interface->EndFrame();

	// Present only when the compositor is ready for a new frame (frame_ready; false e.g. while occluded). needs_redraw
	// is set by RenderFrame() after Context::IsRenderDirty() confirms that there are visual changes.
	// Skipping the swap when idle is what lets the loop block in poll: otherwise the non-blocking swap would be issued
	// every iteration and the buffer release events it generates would keep waking poll, spinning the loop at full speed.
	if (!data->frame_ready || !data->needs_redraw)
		return;

	// Ask to be notified when the compositor wants the next frame, and clear frame_ready until it does. The request is
	// committed together with the buffer by eglSwapBuffers, so it throttles continuous redrawing to the refresh rate of
	// the output the surface is on.
	if (wl_callback* callback = wl_surface_frame(data->surface))
	{
		data->frame_callback = callback;
		wl_callback_add_listener(callback, &surface_frame_listener, nullptr);
		data->frame_ready = false;
	}

	// Ask when this frame turns into light; the request is committed together with the buffer by eglSwapBuffers. The
	// presented event feeds the frame time prediction in UpdatePredictedFrameTime. Cap the in-flight objects so a
	// compositor that never answers cannot grow the list without bound.
	if (data->globals.presentation)
	{
		if (data->presentation_feedbacks.size() >= 4)
		{
			wp_presentation_feedback_destroy(data->presentation_feedbacks.front());
			data->presentation_feedbacks.erase(data->presentation_feedbacks.begin());
		}
		if (struct wp_presentation_feedback* feedback = wp_presentation_feedback(data->globals.presentation, data->surface))
		{
			wp_presentation_feedback_add_listener(feedback, &presentation_feedback_listener, nullptr);
			data->presentation_feedbacks.push_back(feedback);
		}
	}

	data->needs_redraw = false;
	if (SwapBuffers())
		PushDamageHistory();
	else
		data->needs_redraw = true;
}

bool Backend::RenderFrame(OnlyWayUi::Context* context)
{
	OWUI_ASSERT(data && context);

	if (!context->IsRenderDirty())
	{
		data->needs_redraw = false;
		return false;
	}

	if (!data->frame_ready)
	{
		data->needs_redraw = true;
		return false;
	}

	data->needs_redraw = true;
	BeginFrameWithDamage(context);
	context->Render();
	PresentFrame();
	return true;
}
