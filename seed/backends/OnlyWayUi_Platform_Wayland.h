#pragma once

#include <OnlyWayUi/Config/Config.h>
#include <OnlyWayUi/Core/Input.h>
#include <OnlyWayUi/Core/SystemInterface.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>
#include <xkbcommon/xkbcommon.h>

struct wl_compositor;
struct wl_cursor_theme;
struct wl_data_device_manager;
struct wl_display;
struct wl_pointer;
struct wl_seat;
struct wl_shm;
struct wl_surface;
struct xdg_wm_base;
struct zxdg_decoration_manager_v1;
struct wp_presentation;
struct zxdg_exporter_v2;
struct xkb_context;
struct xkb_keymap;
struct xkb_state;
class ClipboardManager_Wayland;

class SystemInterface_Wayland : public OnlyWayUi::SystemInterface {
public:
	SystemInterface_Wayland(wl_display* display, wl_shm* shm, wl_data_device_manager* data_device_manager);
	~SystemInterface_Wayland();

	void SetPointer(wl_pointer* pointer);
	void SetSeat(wl_seat* seat);
	void SetCursorSurface(wl_surface* surface);
	void SetPointerSerial(uint32_t serial);
	void ClearPointerSerial();
	void SetSeatSerial(uint32_t serial);
	int GetClipboardReadFd() const;
	int GetClipboardWriteFd() const;
	void ProcessClipboardRead();
	void ProcessClipboardWrite();

	// Switches the elapsed-time axis to the compositor's presentation clock. Call during startup, before the first
	// GetElapsedTime consumer runs: switching re-anchors elapsed time back to zero.
	void SetClock(clockid_t clock_id);
	// Locks GetElapsedTime to the given absolute presentation-clock time (in seconds), so animations sample the moment
	// the frame is predicted to reach the screen rather than the moment we build it. Never steps the returned time
	// backwards.
	void SetPredictedFrameTime(double clock_seconds);
	// Unlocks GetElapsedTime back to the live clock. Call before dispatching input, which can arrive long after the last
	// prediction was made. The last prediction still floors the returned time so it never steps backwards.
	void ClearPredictedFrameTime();

	double GetElapsedTime() override;
	void SetMouseCursor(const OnlyWayUi::String& cursor_name) override;
	void SetClipboardText(const OnlyWayUi::String& text) override;
	void RequestClipboardText(OnlyWayUi::Function<void(OnlyWayUi::String)> callback) override;
	void GetClipboardText(OnlyWayUi::String& text) override;

private:
	void LoadCursorTheme();
	void ApplyCursor(const char* const* cursor_names, size_t cursor_name_count);
	double ClockNow() const;

	wl_display* display = nullptr;
	wl_shm* shm = nullptr;
	wl_pointer* pointer = nullptr;
	wl_surface* cursor_surface = nullptr;
	wl_cursor_theme* cursor_theme = nullptr;
	bool cursor_theme_load_attempted = false;
	OnlyWayUi::String last_failed_cursor_name;
	clockid_t clock_id = CLOCK_MONOTONIC;
	double clock_start_time = 0.0;
	double predicted_frame_time = 0.0;
	bool has_predicted_frame_time = false;
	uint32_t pointer_serial = 0;
	bool has_pointer_serial = false;
	OnlyWayUi::UniquePtr<ClipboardManager_Wayland> clipboard_manager;
};

namespace OwuiWayland {

struct Globals {
	wl_compositor* compositor = nullptr;
	wl_shm* shm = nullptr;
	wl_seat* seat = nullptr;
	wl_data_device_manager* data_device_manager = nullptr;
	xdg_wm_base* wm_base = nullptr;
	zxdg_decoration_manager_v1* decoration_manager = nullptr;
	wp_presentation* presentation = nullptr;
	// Exports the toplevel so its handle can identify the parent window to xdg-desktop-portal dialogs.
	zxdg_exporter_v2* exporter = nullptr;
};

struct KeyboardState {
	xkb_context* context = nullptr;
	xkb_keymap* keymap = nullptr;
	xkb_state* state = nullptr;
	int modifiers = 0;

	KeyboardState();
	~KeyboardState();
	void SetKeymapFromString(const char* keymap_string);
	void Reset();
	void UpdateModifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
};

OnlyWayUi::Input::KeyIdentifier ConvertKeySym(xkb_keysym_t sym);
int ConvertKeyModifiers(xkb_state* state);
int ConvertMouseButton(uint32_t button);

} // namespace OwuiWayland
