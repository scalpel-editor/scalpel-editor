// Wayland xdg-shell toplevel and input connection.

#ifndef WAYLANDWINDOW_H
#define WAYLANDWINDOW_H

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "WaylandClipboard.h"
#include "WaylandCursor.h"
#include "WaylandFrame.h"
#include "WaylandInput.h"
#include "WaylandLifecycle.h"
#include "WaylandPrimarySelection.h"
#include "WaylandScale.h"
#include "WaylandTextInput.h"

struct wl_array;
struct wl_callback;
struct wl_callback_listener;
struct wl_compositor;
struct wl_data_device_manager;
struct wl_display;
struct wl_egl_window;
struct wl_keyboard;
struct wl_keyboard_listener;
struct wl_output;
struct wl_output_listener;
struct wl_pointer;
struct wl_pointer_listener;
struct wl_registry;
struct wl_registry_listener;
struct wl_seat;
struct wl_seat_listener;
struct wl_shm;
struct wl_surface;
struct wl_surface_listener;
struct wp_presentation;
struct wp_presentation_feedback;
struct wp_presentation_feedback_listener;
struct wp_presentation_listener;
struct wp_viewporter;
struct wp_viewport;
struct wp_fractional_scale_manager_v1;
struct wp_fractional_scale_v1;
struct wp_fractional_scale_v1_listener;
struct xdg_surface;
struct xdg_surface_listener;
struct xdg_toplevel;
struct xdg_toplevel_listener;
struct xdg_wm_base;
struct xdg_wm_base_listener;
struct zxdg_decoration_manager_v1;
struct zxdg_toplevel_decoration_v1;
struct zxdg_toplevel_decoration_v1_listener;
struct zwp_primary_selection_device_manager_v1;
struct zwp_text_input_manager_v3;
struct wl_cursor_theme;

namespace Scalpel {

/**
 * One Wayland display connection and configured xdg-toplevel.
 *
 * Construction performs the required initial commit without a buffer and
 * waits for xdg_surface.configure before returning. It coalesces later resize
 * events, then translates and queues keyboard and pointer events for the
 * application loop. Registry, output, seat, decoration, and retained
 * toplevel changes pass through testable lifecycle state. Pointer-axis
 * callbacks are coalesced into one scroll event per protocol frame. Keyboard
 * composition and compositor-configured repeat are part of the input state.
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
	[[nodiscard]] bool PresentationAvailable() const noexcept {
		return presentation != nullptr;
	}
	[[nodiscard]] std::optional<uint32_t> PresentationClockId() const noexcept {
		return presentationClockId;
	}
	[[nodiscard]] std::optional<WindowSize> TakeResize() noexcept { return lifecycle.TakeResize(); }
	[[nodiscard]] std::optional<WaylandScaleConfiguration>
		TakeScaleConfiguration();
	[[nodiscard]] const WaylandScaleConfiguration &ScaleConfiguration() const noexcept {
		return scaleState.Configuration();
	}
	[[nodiscard]] std::vector<InputEvent> TakeInputs() { return input.TakeInputs(); }
	void SetCursor(Scintilla::Internal::Window::Cursor cursor);
	void SetCursorScale(int scale);
	void CopyToClipboard(uint64_t request, std::string text);
	void PasteFromClipboard(uint64_t request);
	void PublishPrimarySelection(uint64_t request, std::optional<std::string> text);
	void PasteFromPrimarySelection(uint64_t request);
	[[nodiscard]] bool ClipboardPasteAvailable() const { return clipboard.CanPaste(); }
	[[nodiscard]] std::vector<ClipboardResult> TakeClipboardResults() {
		return clipboard.TakeResults();
	}
	[[nodiscard]] std::vector<PrimarySelectionResult> TakePrimarySelectionResults() {
		return primarySelection.TakeResults();
	}
	void UpdateTextInputState(WaylandTextInputClientState state) {
		textInput.UpdateClientState(std::move(state));
	}
	[[nodiscard]] std::vector<WaylandTextInputBatch> TakeTextInputBatches() {
		return textInput.TakeBatches();
	}
	void InvalidateFrame(FrameRectangle rectangle) {
		frameState.Invalidate(rectangle);
	}
	[[nodiscard]] bool CanSubmitFrame() const noexcept {
		return frameState.CanSubmit();
	}
	[[nodiscard]] std::optional<FramePlan> BeginFrame(
		int bufferAge, bool bufferAgeSupported, bool damageSwapSupported);
	void PrepareFrame(const FramePlan &plan);
	void SubmitFrame(uint64_t submission);
	void CancelFrame() noexcept;
	[[nodiscard]] std::vector<PresentationResult> TakePresentationResults() {
		return frameState.TakePresentationResults();
	}

	/** Complete one request/event round trip, throwing on display failure. */
	void RoundTrip();
	/** Wait for display activity, key repeat, or the optional editor-work deadline. */
	void WaitForEvents(std::optional<std::chrono::milliseconds> timeout);

private:
	void Initialise(const char *title);
	void Destroy() noexcept;
	void DispatchUntilConfigured();
	void ApplyLifecycleActions(const std::vector<WaylandLifecycleAction> &actions);
	void ApplyCursorAction(const std::optional<WaylandCursorAction> &action);
	void ApplyCursorAction(const WaylandCursorAction &action);
	void LoadCursorTheme();
	void DestroyCursorTheme() noexcept;
	void CreateDecoration();
	void CreateScaleObjects();
	void DestroyViewport() noexcept;
	void DestroyFractionalScale() noexcept;
	void RefreshScaleProtocolAvailability();
	void ApplyScaleConfiguration(const WaylandScaleConfiguration &configuration);
	[[nodiscard]] std::optional<uint32_t> OutputName(wl_output *output) const noexcept;
	[[nodiscard]] std::optional<uint32_t> RegistryNameForSeat(wl_seat *seat) const noexcept;
	void DestroyPresentationFeedback(uint64_t submission) noexcept;
	[[nodiscard]] bool HasPresentationFeedback(uint64_t submission) const noexcept;

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
	static void PointerEnter(void *data, wl_pointer *pointer, uint32_t serial,
		wl_surface *surface, int32_t x, int32_t y);
	static void PointerLeave(void *data, wl_pointer *pointer, uint32_t serial,
		wl_surface *surface);
	static void PointerMotion(void *data, wl_pointer *pointer, uint32_t time,
		int32_t x, int32_t y);
	static void PointerButton(void *data, wl_pointer *pointer, uint32_t serial,
		uint32_t time, uint32_t button, uint32_t state);
	static void PointerAxis(void *data, wl_pointer *pointer, uint32_t time,
		uint32_t axis, int32_t value);
	static void PointerFrame(void *data, wl_pointer *pointer);
	static void PointerAxisSource(void *data, wl_pointer *pointer, uint32_t source);
	static void PointerAxisStop(void *data, wl_pointer *pointer, uint32_t time, uint32_t axis);
	static void PointerAxisDiscrete(void *data, wl_pointer *pointer, uint32_t axis,
		int32_t discrete);
	static void PointerAxisValue120(void *data, wl_pointer *pointer, uint32_t axis,
		int32_t value120);
	static void PointerAxisRelativeDirection(void *data, wl_pointer *pointer,
		uint32_t axis, uint32_t direction);
	static void OutputGeometry(void *data, wl_output *output, int32_t x, int32_t y,
		int32_t physicalWidth, int32_t physicalHeight, int32_t subpixel,
		const char *make, const char *model, int32_t transform);
	static void OutputMode(void *data, wl_output *output, uint32_t flags,
		int32_t width, int32_t height, int32_t refresh);
	static void OutputDone(void *data, wl_output *output);
	static void OutputScale(void *data, wl_output *output, int32_t factor);
	static void OutputName(void *data, wl_output *output, const char *name);
	static void OutputDescription(void *data, wl_output *output, const char *description);
	static void WaylandSurfaceEnter(void *data, wl_surface *surface, wl_output *output);
	static void WaylandSurfaceLeave(void *data, wl_surface *surface, wl_output *output);
	static void WaylandSurfacePreferredBufferScale(void *data, wl_surface *surface,
		int32_t factor);
	static void WaylandSurfacePreferredBufferTransform(void *data, wl_surface *surface,
		uint32_t transform);
	static void WmBasePing(void *data, xdg_wm_base *wmBase, uint32_t serial);
	static void SurfaceConfigure(void *data, xdg_surface *shellSurface, uint32_t serial);
	static void ToplevelConfigure(void *data, xdg_toplevel *toplevel, int32_t width,
		int32_t height, wl_array *states);
	static void ToplevelClose(void *data, xdg_toplevel *toplevel);
	static void ToplevelConfigureBounds(void *data, xdg_toplevel *toplevel,
		int32_t width, int32_t height);
	static void ToplevelWmCapabilities(void *data, xdg_toplevel *toplevel,
		wl_array *capabilities);
	static void DecorationConfigure(void *data,
		zxdg_toplevel_decoration_v1 *decoration, uint32_t mode);
	static void PresentationClockId(void *data,
		wp_presentation *presentation, uint32_t clockId);
	static void FrameDone(void *data, wl_callback *callback, uint32_t time);
	static void PresentationFeedbackSyncOutput(void *data,
		wp_presentation_feedback *feedback, wl_output *output);
	static void PresentationFeedbackPresented(void *data,
		wp_presentation_feedback *feedback, uint32_t secondsHigh,
		uint32_t secondsLow, uint32_t nanoseconds, uint32_t refreshNanoseconds,
		uint32_t sequenceHigh, uint32_t sequenceLow, uint32_t flags);
	static void PresentationFeedbackDiscarded(void *data,
		wp_presentation_feedback *feedback);
	static void FractionalPreferredScale(void *data,
		wp_fractional_scale_v1 *fractionalScale, uint32_t scale);

	static const wl_registry_listener registryListener;
	static const wl_seat_listener seatListener;
	static const wl_keyboard_listener keyboardListener;
	static const wl_pointer_listener pointerListener;
	static const wl_output_listener outputListener;
	static const wl_surface_listener waylandSurfaceListener;
	static const xdg_wm_base_listener wmBaseListener;
	static const xdg_surface_listener surfaceListener;
	static const xdg_toplevel_listener toplevelListener;
	static const zxdg_toplevel_decoration_v1_listener decorationListener;
	static const wp_presentation_listener presentationListener;
	static const wl_callback_listener frameListener;
	static const wp_presentation_feedback_listener presentationFeedbackListener;
	static const wp_fractional_scale_v1_listener fractionalScaleListener;

	wl_display *display = nullptr;
	wl_registry *registry = nullptr;
	wl_compositor *compositor = nullptr;
	struct Output {
		uint32_t name;
		wl_output *proxy;
	};
	std::vector<Output> outputs;
	wl_seat *seat = nullptr;
	wl_keyboard *keyboard = nullptr;
	wl_pointer *pointer = nullptr;
	wl_shm *sharedMemory = nullptr;
	wl_data_device_manager *dataDeviceManager = nullptr;
	zwp_primary_selection_device_manager_v1 *primarySelectionManager = nullptr;
	zwp_text_input_manager_v3 *textInputManager = nullptr;
	wp_presentation *presentation = nullptr;
	wp_viewporter *viewporter = nullptr;
	wp_viewport *viewport = nullptr;
	wp_fractional_scale_manager_v1 *fractionalScaleManager = nullptr;
	wp_fractional_scale_v1 *fractionalScale = nullptr;
	std::optional<uint32_t> presentationClockId;
	wl_callback *frameCallback = nullptr;
	struct PresentationFeedback {
		uint64_t submission;
		wp_presentation_feedback *proxy;
	};
	std::vector<PresentationFeedback> presentationFeedback;
	uint64_t preparedSubmission = 0;
	wl_surface *cursorSurface = nullptr;
	wl_cursor_theme *cursorTheme = nullptr;
	int cursorThemeScale = 0;
	int cursorThemeAttemptedScale = 0;
	xdg_wm_base *wmBase = nullptr;
	wl_surface *surface = nullptr;
	wl_egl_window *eglWindow = nullptr;
	xdg_surface *shellSurface = nullptr;
	xdg_toplevel *toplevel = nullptr;
	zxdg_decoration_manager_v1 *decorationManager = nullptr;
	zxdg_toplevel_decoration_v1 *decoration = nullptr;
	WaylandLifecycle lifecycle;
	WaylandScaleState scaleState;
	std::optional<WaylandScaleConfiguration> appliedScaleConfiguration;
	WaylandInput input;
	WaylandCursorState cursorState;
	WaylandClipboard clipboard;
	WaylandPrimarySelection primarySelection;
	WaylandTextInput textInput;
	WaylandFrameState frameState;
	WaylandCursorSettings cursorSettings;
	bool callbackFailed = false;
	bool configured = false;
	bool cursorUnavailableReported = false;
};

}

#endif
