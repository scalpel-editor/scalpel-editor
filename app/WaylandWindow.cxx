// Wayland xdg-shell toplevel and input connection.

#include "WaylandWindow.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <sys/mman.h>
#include <poll.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-egl.h>

#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-client-protocol.h"
#include "primary-selection-client-protocol.h"
#include "text-input-client-protocol.h"
#include "presentation-time-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "fractional-scale-client-protocol.h"
#include "xdg-foreign-client-protocol.h"
#include "WaylandEventLoop.h"

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

const xdg_surface_listener WaylandWindow::popupSurfaceListener = {
	WaylandWindow::PopupSurfaceConfigure,
};

const xdg_popup_listener WaylandWindow::popupListener = {
	WaylandWindow::PopupConfigure,
	WaylandWindow::PopupDone,
	nullptr,
};

const xdg_toplevel_listener WaylandWindow::toplevelListener = {
	WaylandWindow::ToplevelConfigure,
	WaylandWindow::ToplevelClose,
	WaylandWindow::ToplevelConfigureBounds,
	WaylandWindow::ToplevelWmCapabilities,
};

const zxdg_toplevel_decoration_v1_listener WaylandWindow::decorationListener = {
	WaylandWindow::DecorationConfigure,
};

const zxdg_exported_v2_listener WaylandWindow::exportedListener = {
	WaylandWindow::ExportedHandle,
};

const wp_presentation_listener WaylandWindow::presentationListener = {
	WaylandWindow::PresentationClockId,
};

const wl_callback_listener WaylandWindow::frameListener = {
	WaylandWindow::FrameDone,
};

const wp_presentation_feedback_listener WaylandWindow::presentationFeedbackListener = {
	WaylandWindow::PresentationFeedbackSyncOutput,
	WaylandWindow::PresentationFeedbackPresented,
	WaylandWindow::PresentationFeedbackDiscarded,
};

const wp_fractional_scale_v1_listener WaylandWindow::fractionalScaleListener = {
	WaylandWindow::FractionalPreferredScale,
};

WaylandWindow::WaylandWindow(const char *title, int width_, int height_) :
	lifecycle(width_, height_), scaleState(width_, height_) {
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
	cursorSettings = ResolveCursorSettings();
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
	scaleState.SetBufferScaleAvailable(
		wl_surface_get_version(surface) >= WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION);
	CreateScaleObjects();
	textInput.SetSurface(surface);
	cursorSurface = wl_compositor_create_surface(compositor);
	if (!cursorSurface) {
		std::cerr << "scalpel-editor: Wayland cursor surface is unavailable\n";
		cursorUnavailableReported = true;
	}
	LoadCursorTheme();
	ApplyCursorAction(cursorState.SetThemeAvailable(cursorTheme != nullptr));
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
	CreateDecoration();
	CreatePortalParentExport();

	// xdg-shell forbids attaching a buffer before this empty initial commit
	// has been answered with xdg_surface.configure and acknowledged.
	wl_surface_commit(surface);
	DispatchUntilConfigured();
	if (CallbackFailed()) {
		throw std::runtime_error("could not initialise Wayland listeners");
	}
	const WaylandScaleConfiguration &scale = scaleState.Configuration();
	eglWindow = wl_egl_window_create(
		surface, scale.bufferWidth, scale.bufferHeight);
	if (!eglWindow) {
		throw std::runtime_error("could not create the Wayland EGL window");
	}
	ApplyScaleConfiguration(scale);
	(void)lifecycle.TakeResize();
}

void WaylandWindow::Destroy() noexcept {
	fileDialog.Clear();
	(void)textInput.SetSeat(nullptr);
	CancelFrame();
	for (const PresentationFeedback &feedback : presentationFeedback) {
		wp_presentation_feedback_destroy(feedback.proxy);
	}
	presentationFeedback.clear();
	// Popup before toplevel so reverse ownership order is preserved.
	DestroyContextMenuPopup();
	if (eglWindow) {
		wl_egl_window_destroy(eglWindow);
	}
	DestroyPortalParentExport();
	if (decoration) {
		zxdg_toplevel_decoration_v1_destroy(decoration);
	}
	if (toplevel) {
		xdg_toplevel_destroy(toplevel);
	}
	if (shellSurface) {
		xdg_surface_destroy(shellSurface);
	}
	DestroyFractionalScale();
	DestroyViewport();
	if (surface) {
		wl_surface_destroy(surface);
	}
	DestroyCursorTheme();
	if (cursorSurface) {
		wl_surface_destroy(cursorSurface);
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
		clipboard.SetSeat(nullptr);
		primarySelection.SetSeat(nullptr);
		(void)textInput.SetSeat(nullptr);
		if (wl_seat_get_version(seat) >= WL_SEAT_RELEASE_SINCE_VERSION) {
			wl_seat_release(seat);
		} else {
			wl_seat_destroy(seat);
		}
	}
	if (sharedMemory) {
		wl_shm_destroy(sharedMemory);
	}
	clipboard.SetManager(nullptr);
	primarySelection.SetManager(nullptr);
	(void)textInput.SetManager(nullptr);
	if (dataDeviceManager) {
		wl_data_device_manager_destroy(dataDeviceManager);
	}
	if (primarySelectionManager) {
		zwp_primary_selection_device_manager_v1_destroy(primarySelectionManager);
	}
	if (textInputManager) {
		zwp_text_input_manager_v3_destroy(textInputManager);
	}
	if (presentation) {
		wp_presentation_destroy(presentation);
	}
	if (fractionalScaleManager) {
		wp_fractional_scale_manager_v1_destroy(fractionalScaleManager);
	}
	if (viewporter) {
		wp_viewporter_destroy(viewporter);
	}
	if (wmBase) {
		xdg_wm_base_destroy(wmBase);
	}
	if (decorationManager) {
		zxdg_decoration_manager_v1_destroy(decorationManager);
	}
	if (exporter) {
		zxdg_exporter_v2_destroy(exporter);
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
	decoration = nullptr;
	decorationManager = nullptr;
	exported = nullptr;
	exporter = nullptr;
	eglWindow = nullptr;
	shellSurface = nullptr;
	surface = nullptr;
	cursorSurface = nullptr;
	sharedMemory = nullptr;
	dataDeviceManager = nullptr;
	primarySelectionManager = nullptr;
	textInputManager = nullptr;
	presentation = nullptr;
	viewporter = nullptr;
	viewport = nullptr;
	fractionalScaleManager = nullptr;
	fractionalScale = nullptr;
	appliedScaleConfiguration.reset();
	presentationClockId.reset();
	frameCallback = nullptr;
	preparedSubmission = 0;
	keyboard = nullptr;
	pointer = nullptr;
	seat = nullptr;
	wmBase = nullptr;
	compositor = nullptr;
	registry = nullptr;
	display = nullptr;
}

void WaylandWindow::PrepareFrame(const FramePlan &plan) {
	if (!surface || frameCallback || preparedSubmission != 0 ||
		!frameState.Painting()) {
		throw std::logic_error("Wayland frame preparation is out of sequence");
	}
	frameCallback = wl_surface_frame(surface);
	if (!frameCallback ||
		wl_callback_add_listener(frameCallback, &frameListener, this) != 0) {
		if (frameCallback) {
			wl_callback_destroy(frameCallback);
			frameCallback = nullptr;
		}
		frameState.CancelPaint();
		throw std::runtime_error("could not create a Wayland frame callback");
	}
	preparedSubmission = plan.submission;
	const bool bufferDamage = wl_surface_get_version(surface) >=
		WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION;
	const std::vector<DamageRectangle> damage = bufferDamage ?
		plan.waylandDamage : WaylandBufferDamage(plan.submissionDamage);
	for (const DamageRectangle &rectangle : damage) {
		if (bufferDamage) {
			wl_surface_damage_buffer(surface, rectangle.x, rectangle.y,
				rectangle.width, rectangle.height);
		} else {
			wl_surface_damage(surface, rectangle.x, rectangle.y,
				rectangle.width, rectangle.height);
		}
	}
	if (presentation) {
		struct wp_presentation_feedback *feedback =
			wp_presentation_feedback(presentation, surface);
		if (feedback && wp_presentation_feedback_add_listener(
			feedback, &presentationFeedbackListener, this) == 0) {
			presentationFeedback.push_back({plan.submission, feedback});
		} else if (feedback) {
			wp_presentation_feedback_destroy(feedback);
		}
	}
	std::optional<uint64_t> expired;
	try {
		expired = frameState.PrepareFrame(
			plan.submission, HasPresentationFeedback(plan.submission));
	} catch (...) {
		CancelFrame();
		throw;
	}
	if (expired) {
		DestroyPresentationFeedback(*expired);
	}
}

std::optional<WaylandScaleConfiguration>
WaylandWindow::TakeScaleConfiguration() {
	std::optional<WaylandScaleConfiguration> configuration =
		scaleState.PendingConfiguration();
	if (configuration) {
		ApplyScaleConfiguration(*configuration);
		scaleState.MarkConfigurationApplied(*configuration);
		(void)lifecycle.TakeResize();
	}
	return configuration;
}

std::optional<FramePlan> WaylandWindow::BeginFrame(
	int bufferAge, bool bufferAgeSupported, bool damageSwapSupported) {
	const WaylandScaleConfiguration &scale =
		appliedScaleConfiguration.value_or(scaleState.Configuration());
	std::optional<FramePlan> plan = frameState.BeginFrame(
		scale.logicalWidth, scale.logicalHeight, bufferAge,
		bufferAgeSupported, damageSwapSupported);
	if (!plan) {
		return std::nullopt;
	}
	return ScaleFramePlan(std::move(*plan),
		scale.logicalWidth, scale.logicalHeight,
		scale.bufferWidth, scale.bufferHeight);
}

void WaylandWindow::SubmitFrame(uint64_t submission) {
	if (submission != preparedSubmission) {
		throw std::logic_error("submitted Wayland frame was not prepared");
	}
	frameState.SubmitFrame(submission);
	preparedSubmission = 0;
}

void WaylandWindow::CancelFrame() noexcept {
	if (frameCallback) {
		wl_callback_destroy(frameCallback);
		frameCallback = nullptr;
		frameState.CancelFrameCallback();
	}
	if (preparedSubmission != 0) {
		DestroyPresentationFeedback(preparedSubmission);
		preparedSubmission = 0;
		frameState.CancelPaint();
	}
}

bool WaylandWindow::HasPresentationFeedback(uint64_t submission) const noexcept {
	return std::any_of(presentationFeedback.begin(), presentationFeedback.end(),
		[submission](const PresentationFeedback &feedback) {
			return feedback.submission == submission;
		});
}

void WaylandWindow::DestroyPresentationFeedback(uint64_t submission) noexcept {
	const auto found = std::find_if(presentationFeedback.begin(),
		presentationFeedback.end(),
		[submission](const PresentationFeedback &feedback) {
			return feedback.submission == submission;
		});
	if (found != presentationFeedback.end()) {
		wp_presentation_feedback_destroy(found->proxy);
		presentationFeedback.erase(found);
	}
}

void WaylandWindow::SetCursor(Scintilla::Internal::Window::Cursor cursor) {
	ApplyCursorAction(cursorState.Request(cursor));
	if (!cursorTheme) {
		LoadCursorTheme();
		ApplyCursorAction(cursorState.SetThemeAvailable(cursorTheme != nullptr));
	}
}

void WaylandWindow::SetCursorScale(int scale) {
	if (scale <= 0) {
		throw std::invalid_argument("Wayland cursor scale must be positive");
	}
	if (cursorSurface &&
		wl_surface_get_version(cursorSurface) < WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION) {
		scale = 1;
	}
	if (scale == cursorState.Scale()) {
		return;
	}
	ApplyCursorAction(cursorState.SetThemeAvailable(false));
	DestroyCursorTheme();
	(void)cursorState.SetScale(scale);
	LoadCursorTheme();
	ApplyCursorAction(cursorState.SetThemeAvailable(cursorTheme != nullptr));
}

void WaylandWindow::CopyToClipboard(uint64_t request, std::string text) {
	clipboard.CopyText(request, std::move(text));
}

void WaylandWindow::PasteFromClipboard(uint64_t request) {
	clipboard.PasteText(request);
}

void WaylandWindow::PublishPrimarySelection(
	uint64_t request, std::optional<std::string> text) {
	primarySelection.PublishText(request, std::move(text));
}

void WaylandWindow::PasteFromPrimarySelection(uint64_t request) {
	primarySelection.PasteText(request);
}

void WaylandWindow::ApplyCursorAction(const std::optional<WaylandCursorAction> &action) {
	if (action) {
		ApplyCursorAction(*action);
	}
}

void WaylandWindow::ApplyCursorAction(const WaylandCursorAction &action) {
	if (!pointer || !cursorSurface || !cursorTheme || action.scale != cursorThemeScale) {
		return;
	}
	const WaylandCursorNames names = CursorNames(action.cursor);
	wl_cursor *selected = nullptr;
	for (std::size_t index = 0; index < names.count; ++index) {
		selected = wl_cursor_theme_get_cursor(cursorTheme, names[index].data());
		if (selected && selected->image_count > 0) {
			break;
		}
	}
	if (!selected || selected->image_count == 0) {
		if (!cursorUnavailableReported) {
			std::cerr << "scalpel-editor: Wayland cursor theme has no usable cursor\n";
			cursorUnavailableReported = true;
		}
		return;
	}
	wl_cursor_image *image = selected->images[0];
	wl_buffer *buffer = wl_cursor_image_get_buffer(image);
	if (!buffer) {
		if (!cursorUnavailableReported) {
			std::cerr << "scalpel-editor: Wayland cursor buffer is unavailable\n";
			cursorUnavailableReported = true;
		}
		return;
	}
	const WaylandCursorImageGeometry geometry = CursorImageGeometry(
		image->width, image->height, image->hotspot_x, image->hotspot_y, action.scale);
	const uint32_t surfaceVersion = wl_surface_get_version(cursorSurface);
	if (action.scale != 1 &&
		surfaceVersion < WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION) {
		return;
	}
	wl_pointer_set_cursor(pointer, action.serial, cursorSurface,
		geometry.hotspotX, geometry.hotspotY);
	if (surfaceVersion >= WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION) {
		wl_surface_set_buffer_scale(cursorSurface, action.scale);
	}
	wl_surface_attach(cursorSurface, buffer, 0, 0);
	if (surfaceVersion >= WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION) {
		wl_surface_damage_buffer(cursorSurface, 0, 0,
			static_cast<int32_t>(geometry.bufferWidth),
			static_cast<int32_t>(geometry.bufferHeight));
	} else {
		wl_surface_damage(cursorSurface, 0, 0,
			static_cast<int32_t>((geometry.bufferWidth + action.scale - 1) / action.scale),
			static_cast<int32_t>((geometry.bufferHeight + action.scale - 1) / action.scale));
	}
	wl_surface_commit(cursorSurface);
}

void WaylandWindow::LoadCursorTheme() {
	if (!cursorSurface) {
		return;
	}
	if (!sharedMemory) {
		if (!cursorUnavailableReported) {
			std::cerr << "scalpel-editor: Wayland cursor service is unavailable\n";
			cursorUnavailableReported = true;
		}
		return;
	}
	if (cursorTheme || cursorThemeAttemptedScale == cursorState.Scale()) {
		return;
	}
	cursorThemeAttemptedScale = cursorState.Scale();
	const int pixelSize = CursorThemePixelSize(
		cursorSettings.logicalSize, cursorThemeAttemptedScale);
	const char *name = cursorSettings.themeName.empty() ?
		nullptr : cursorSettings.themeName.c_str();
	cursorTheme = wl_cursor_theme_load(name, pixelSize, sharedMemory);
	if (!cursorTheme && name) {
		cursorTheme = wl_cursor_theme_load(nullptr, pixelSize, sharedMemory);
	}
	if (cursorTheme) {
		cursorThemeScale = cursorThemeAttemptedScale;
	}
	if (!cursorTheme && !cursorUnavailableReported) {
		std::cerr << "scalpel-editor: Wayland cursor theme is unavailable\n";
		cursorUnavailableReported = true;
	}
}

void WaylandWindow::DestroyCursorTheme() noexcept {
	if (cursorTheme) {
		wl_cursor_theme_destroy(cursorTheme);
		cursorTheme = nullptr;
	}
	cursorThemeScale = 0;
	cursorThemeAttemptedScale = 0;
}

void WaylandWindow::ApplyLifecycleActions(const std::vector<WaylandLifecycleAction> &actions) {
	for (const WaylandLifecycleAction &action : actions) {
		switch (action.type) {
		case WaylandLifecycleActionType::BindCompositor:
			compositor = static_cast<wl_compositor *>(wl_registry_bind(
				registry, action.name, &wl_compositor_interface, std::min(action.version, 6U)));
			if (!compositor) {
				callbackFailed = true;
			}
			break;
		case WaylandLifecycleActionType::BindWmBase:
			wmBase = static_cast<xdg_wm_base *>(wl_registry_bind(
				registry, action.name, &xdg_wm_base_interface, std::min(action.version, 5U)));
			if (!wmBase) {
				callbackFailed = true;
			}
			break;
		case WaylandLifecycleActionType::BindDecorationManager:
			if (decorationManager) {
				callbackFailed = true;
				break;
			}
			decorationManager = static_cast<zxdg_decoration_manager_v1 *>(wl_registry_bind(
				registry, action.name, &zxdg_decoration_manager_v1_interface,
				std::min(action.version, 1U)));
			break;
		case WaylandLifecycleActionType::ReleaseDecorationManager:
			if (decorationManager) {
				zxdg_decoration_manager_v1_destroy(decorationManager);
				decorationManager = nullptr;
			}
			break;
		case WaylandLifecycleActionType::BindSharedMemory:
			if (sharedMemory) {
				callbackFailed = true;
				break;
			}
			sharedMemory = static_cast<wl_shm *>(wl_registry_bind(
				registry, action.name, &wl_shm_interface, std::min(action.version, 1U)));
			LoadCursorTheme();
			ApplyCursorAction(cursorState.SetThemeAvailable(cursorTheme != nullptr));
			break;
		case WaylandLifecycleActionType::ReleaseSharedMemory:
			ApplyCursorAction(cursorState.SetThemeAvailable(false));
			DestroyCursorTheme();
			if (sharedMemory) {
				wl_shm_destroy(sharedMemory);
				sharedMemory = nullptr;
			}
			break;
		case WaylandLifecycleActionType::BindDataDeviceManager:
			if (dataDeviceManager) {
				callbackFailed = true;
				break;
			}
			dataDeviceManager = static_cast<wl_data_device_manager *>(wl_registry_bind(
				registry, action.name, &wl_data_device_manager_interface,
				std::min(action.version, 3U)));
			clipboard.SetManager(dataDeviceManager);
			if (seat) {
				clipboard.SetSeat(seat);
			}
			break;
		case WaylandLifecycleActionType::ReleaseDataDeviceManager:
			clipboard.SetManager(nullptr);
			if (dataDeviceManager) {
				wl_data_device_manager_destroy(dataDeviceManager);
				dataDeviceManager = nullptr;
			}
			break;
		case WaylandLifecycleActionType::BindPrimarySelectionManager:
			if (primarySelectionManager) {
				callbackFailed = true;
				break;
			}
			primarySelectionManager =
				static_cast<zwp_primary_selection_device_manager_v1 *>(wl_registry_bind(
					registry, action.name,
					&zwp_primary_selection_device_manager_v1_interface,
					std::min(action.version, 1U)));
			primarySelection.SetManager(primarySelectionManager);
			if (seat) {
				primarySelection.SetSeat(seat);
			}
			break;
		case WaylandLifecycleActionType::ReleasePrimarySelectionManager:
			primarySelection.SetManager(nullptr);
			if (primarySelectionManager) {
				zwp_primary_selection_device_manager_v1_destroy(
					primarySelectionManager);
				primarySelectionManager = nullptr;
			}
			break;
		case WaylandLifecycleActionType::BindTextInputManager:
			if (textInputManager) {
				callbackFailed = true;
				break;
			}
			textInputManager = static_cast<zwp_text_input_manager_v3 *>(
				wl_registry_bind(registry, action.name,
					&zwp_text_input_manager_v3_interface,
					std::min(action.version, 1U)));
			if (!textInput.SetManager(textInputManager)) {
				callbackFailed = true;
			}
			if (seat && !textInput.SetSeat(seat)) {
				callbackFailed = true;
			}
			break;
		case WaylandLifecycleActionType::ReleaseTextInputManager:
			(void)textInput.SetManager(nullptr);
			if (textInputManager) {
				zwp_text_input_manager_v3_destroy(textInputManager);
				textInputManager = nullptr;
			}
			break;
		case WaylandLifecycleActionType::BindPresentation:
			if (presentation) {
				callbackFailed = true;
				break;
			}
			presentationClockId.reset();
			presentation = static_cast<wp_presentation *>(wl_registry_bind(
				registry, action.name, &wp_presentation_interface,
				std::min(action.version, 2U)));
			if (!presentation || wp_presentation_add_listener(
				presentation, &presentationListener, this) != 0) {
				if (presentation) {
					wp_presentation_destroy(presentation);
					presentation = nullptr;
				}
			}
			break;
		case WaylandLifecycleActionType::ReleasePresentation:
			if (presentation) {
				wp_presentation_destroy(presentation);
				presentation = nullptr;
			}
			presentationClockId.reset();
			break;
		case WaylandLifecycleActionType::BindViewporter:
			if (viewporter) {
				callbackFailed = true;
				break;
			}
			viewporter = static_cast<wp_viewporter *>(wl_registry_bind(
				registry, action.name, &wp_viewporter_interface,
				std::min(action.version, 1U)));
			CreateScaleObjects();
			break;
		case WaylandLifecycleActionType::ReleaseViewporter:
			DestroyViewport();
			if (viewporter) {
				wp_viewporter_destroy(viewporter);
				viewporter = nullptr;
			}
			RefreshScaleProtocolAvailability();
			break;
		case WaylandLifecycleActionType::BindFractionalScaleManager:
			if (fractionalScaleManager) {
				callbackFailed = true;
				break;
			}
			fractionalScaleManager =
				static_cast<wp_fractional_scale_manager_v1 *>(wl_registry_bind(
					registry, action.name,
					&wp_fractional_scale_manager_v1_interface,
					std::min(action.version, 1U)));
			CreateScaleObjects();
			break;
		case WaylandLifecycleActionType::ReleaseFractionalScaleManager:
			DestroyFractionalScale();
			try {
				scaleState.ClearFractionalPreferredScale();
			} catch (...) {
				callbackFailed = true;
			}
			if (fractionalScaleManager) {
				wp_fractional_scale_manager_v1_destroy(
					fractionalScaleManager);
				fractionalScaleManager = nullptr;
			}
			RefreshScaleProtocolAvailability();
			break;
		case WaylandLifecycleActionType::BindExporter:
			if (exporter) {
				callbackFailed = true;
				break;
			}
			exporter = static_cast<zxdg_exporter_v2 *>(wl_registry_bind(
				registry, action.name, &zxdg_exporter_v2_interface,
				std::min(action.version, 1U)));
			CreatePortalParentExport();
			break;
		case WaylandLifecycleActionType::ReleaseExporter:
			DestroyPortalParentExport();
			if (exporter) {
				zxdg_exporter_v2_destroy(exporter);
				exporter = nullptr;
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
			scaleState.AddOutput(action.name);
			break;
		}
		case WaylandLifecycleActionType::ReleaseOutput: {
			const auto found = std::find_if(outputs.begin(), outputs.end(),
				[&action](const Output &output) { return output.name == action.name; });
			if (found != outputs.end()) {
				scaleState.RemoveOutput(action.name);
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
			if (seat) {
				callbackFailed = true;
				break;
			}
			seat = static_cast<wl_seat *>(wl_registry_bind(
				registry, action.name, &wl_seat_interface, std::min(action.version, 9U)));
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
			} else {
				clipboard.SetSeat(seat);
				primarySelection.SetSeat(seat);
				if (!textInput.SetSeat(seat)) {
					callbackFailed = true;
				}
			}
			break;
		case WaylandLifecycleActionType::ReleaseSeat:
			clipboard.SetSeat(nullptr);
			primarySelection.SetSeat(nullptr);
			(void)textInput.SetSeat(nullptr);
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
			if (!seat || pointer) {
				callbackFailed = true;
				break;
			}
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
			} else {
				input.SetPointerVersion(wl_pointer_get_version(pointer));
			}
			break;
		case WaylandLifecycleActionType::ReleasePointer:
			input.ResetPointerDevice();
			cursorState.ResetPointer();
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
			if (!seat || keyboard) {
				callbackFailed = true;
				break;
			}
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
			textInput.SetKeyboardFocus(false);
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

void WaylandWindow::CreateDecoration() {
	if (!decorationManager || !toplevel || decoration) {
		return;
	}
	decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
		decorationManager, toplevel);
	if (!decoration || zxdg_toplevel_decoration_v1_add_listener(
		decoration, &decorationListener, this) != 0) {
		if (decoration) {
			zxdg_toplevel_decoration_v1_destroy(decoration);
			decoration = nullptr;
		}
		return;
	}
	zxdg_toplevel_decoration_v1_set_mode(
		decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

std::optional<uint64_t> WaylandWindow::ShowFileDialog(
	const FileDialogRequest &request) {
	DBusConnection *connection = dbus.ConnectSessionBus();
	if (!connection) {
		return std::nullopt;
	}
	return fileDialog.Show(connection, portalParent.ParentHandle(), request);
}

void WaylandWindow::CreatePortalParentExport() {
	if (!exporter || !surface || !toplevel || exported) {
		return;
	}
	exported = zxdg_exporter_v2_export_toplevel(exporter, surface);
	if (!exported) {
		return;
	}
	const uintptr_t token = reinterpret_cast<uintptr_t>(exported);
	portalParent.BeginExport(token);
	if (zxdg_exported_v2_add_listener(
		exported, &exportedListener, this) != 0) {
		portalParent.EndExport(token);
		zxdg_exported_v2_destroy(exported);
		exported = nullptr;
	}
}

void WaylandWindow::DestroyPortalParentExport() noexcept {
	if (!exported) {
		return;
	}
	portalParent.EndExport(reinterpret_cast<uintptr_t>(exported));
	zxdg_exported_v2_destroy(exported);
	exported = nullptr;
}

void WaylandWindow::CreateScaleObjects() {
	if (surface && viewporter && !viewport) {
		viewport = wp_viewporter_get_viewport(viewporter, surface);
	}
	if (surface && fractionalScaleManager && !fractionalScale) {
		fractionalScale =
			wp_fractional_scale_manager_v1_get_fractional_scale(
				fractionalScaleManager, surface);
		if (fractionalScale && wp_fractional_scale_v1_add_listener(
			fractionalScale, &fractionalScaleListener, this) != 0) {
			wp_fractional_scale_v1_destroy(fractionalScale);
			fractionalScale = nullptr;
		}
	}
	RefreshScaleProtocolAvailability();
}

void WaylandWindow::DestroyViewport() noexcept {
	if (viewport) {
		wp_viewport_destroy(viewport);
		viewport = nullptr;
	}
}

void WaylandWindow::DestroyFractionalScale() noexcept {
	if (fractionalScale) {
		wp_fractional_scale_v1_destroy(fractionalScale);
		fractionalScale = nullptr;
	}
}

void WaylandWindow::RefreshScaleProtocolAvailability() {
	scaleState.SetFractionalProtocols(
		viewport != nullptr, fractionalScale != nullptr);
}

void WaylandWindow::ApplyScaleConfiguration(
	const WaylandScaleConfiguration &configuration) {
	if (appliedScaleConfiguration &&
		*appliedScaleConfiguration == configuration) {
		return;
	}
	CancelFrame();
	if (surface && wl_surface_get_version(surface) >=
		WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION) {
		wl_surface_set_buffer_scale(
			surface, configuration.surfaceBufferScale);
	}
	if (viewport) {
		if (configuration.viewportDestination) {
			wp_viewport_set_destination(viewport,
				configuration.logicalWidth, configuration.logicalHeight);
		} else {
			wp_viewport_set_destination(viewport, -1, -1);
		}
	}
	if (eglWindow) {
		wl_egl_window_resize(eglWindow,
			configuration.bufferWidth, configuration.bufferHeight, 0, 0);
	}
	SetCursorScale(configuration.cursorScale);
	frameState.ResetDamageHistory();
	frameState.Invalidate({
		0, 0, configuration.logicalWidth, configuration.logicalHeight});
	appliedScaleConfiguration = configuration;
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

		WaylandEventLoop eventLoop(recoveringBlockedFlush);
		eventLoop.AddEditorDeadline(timeout);
		eventLoop.AddDeadline(input.TimeUntilKeyRepeat());
		eventLoop.AddDeadline(clipboard.TimeUntilTransfer());
		eventLoop.AddDeadline(primarySelection.TimeUntilTransfer());
		eventLoop.AddDeadline(dbus.TimeUntilTimeout());
		const std::size_t displaySource =
			eventLoop.AddSource(wl_display_get_fd(display), events);
		clipboard.AddPollSources(eventLoop);
		primarySelection.AddPollSources(eventLoop);
		dbus.AddPollSources(eventLoop);
		std::vector<pollfd> pollDescriptors = eventLoop.PollDescriptors();
		const int pollResult = poll(
			pollDescriptors.data(), pollDescriptors.size(),
			eventLoop.TimeoutMilliseconds());
		if (pollResult < 0) {
			wl_display_cancel_read(display);
		}
		const WaylandPollOutcome pollOutcome =
			WaylandEventLoop::InterpretPollResult(pollResult, errno);
		if (pollOutcome == WaylandPollOutcome::Interrupted) {
			return;
		}

		pollfd &displayPoll = pollDescriptors[displaySource];
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
		const bool serviceReady = eventLoop.DispatchReady(pollDescriptors);
		clipboard.ProcessPollTimeouts();
		primarySelection.ProcessPollTimeouts();
		dbus.ProcessEvents();

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
		const bool repeated = input.RunKeyRepeat();
		if (!recoveringBlockedFlush || repeated || serviceReady ||
			pollOutcome == WaylandPollOutcome::TimedOut) {
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
	} else if (std::strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
		window.ApplyLifecycleActions(window.lifecycle.AddGlobal(
			WaylandGlobalKind::DecorationManager, name, version));
	} else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
		window.ApplyLifecycleActions(window.lifecycle.AddGlobal(
			WaylandGlobalKind::SharedMemory, name, version));
	} else if (std::strcmp(interface, wl_data_device_manager_interface.name) == 0) {
		window.ApplyLifecycleActions(window.lifecycle.AddGlobal(
			WaylandGlobalKind::DataDeviceManager, name, version));
	} else if (std::strcmp(interface,
		zwp_primary_selection_device_manager_v1_interface.name) == 0) {
		window.ApplyLifecycleActions(window.lifecycle.AddGlobal(
			WaylandGlobalKind::PrimarySelectionManager, name, version));
	} else if (std::strcmp(interface, zwp_text_input_manager_v3_interface.name) == 0) {
		window.ApplyLifecycleActions(window.lifecycle.AddGlobal(
			WaylandGlobalKind::TextInputManager, name, version));
	} else if (std::strcmp(interface, wp_presentation_interface.name) == 0) {
		window.ApplyLifecycleActions(window.lifecycle.AddGlobal(
			WaylandGlobalKind::Presentation, name, version));
	} else if (std::strcmp(interface, wp_viewporter_interface.name) == 0) {
		window.ApplyLifecycleActions(window.lifecycle.AddGlobal(
			WaylandGlobalKind::Viewporter, name, version));
	} else if (std::strcmp(
		interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
		window.ApplyLifecycleActions(window.lifecycle.AddGlobal(
			WaylandGlobalKind::FractionalScaleManager, name, version));
	} else if (std::strcmp(interface, zxdg_exporter_v2_interface.name) == 0) {
		window.ApplyLifecycleActions(window.lifecycle.AddGlobal(
			WaylandGlobalKind::Exporter, name, version));
	} else if (std::strcmp(interface, wl_output_interface.name) == 0) {
		window.ApplyLifecycleActions(window.lifecycle.AddGlobal(
			WaylandGlobalKind::Output, name, version));
	} else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
		window.ApplyLifecycleActions(window.lifecycle.AddGlobal(
			WaylandGlobalKind::Seat, name, version));
	}
}

void WaylandWindow::PresentationClockId(
	void *data, wp_presentation *, uint32_t clockId) {
	auto &window = *static_cast<WaylandWindow *>(data);
	window.presentationClockId = clockId;
}

void WaylandWindow::FrameDone(void *data, wl_callback *callback, uint32_t) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (callback == window.frameCallback) {
		wl_callback_destroy(callback);
		window.frameCallback = nullptr;
		window.frameState.FrameCallbackDone();
	}
}

void WaylandWindow::PresentationFeedbackSyncOutput(
	void *, struct wp_presentation_feedback *, wl_output *) {
}

void WaylandWindow::PresentationFeedbackPresented(
	void *data, struct wp_presentation_feedback *feedback,
	uint32_t secondsHigh, uint32_t secondsLow, uint32_t nanoseconds,
	uint32_t refreshNanoseconds, uint32_t sequenceHigh,
	uint32_t sequenceLow, uint32_t flags) {
	auto &window = *static_cast<WaylandWindow *>(data);
	const auto found = std::find_if(window.presentationFeedback.begin(),
		window.presentationFeedback.end(),
		[feedback](const PresentationFeedback &item) {
			return item.proxy == feedback;
		});
	if (found == window.presentationFeedback.end()) {
		return;
	}
	const uint64_t submission = found->submission;
	wp_presentation_feedback_destroy(feedback);
	window.presentationFeedback.erase(found);
	window.frameState.Presented(submission,
		(static_cast<uint64_t>(secondsHigh) << 32) | secondsLow,
		nanoseconds, refreshNanoseconds,
		(static_cast<uint64_t>(sequenceHigh) << 32) | sequenceLow, flags);
}

void WaylandWindow::PresentationFeedbackDiscarded(
	void *data, struct wp_presentation_feedback *feedback) {
	auto &window = *static_cast<WaylandWindow *>(data);
	const auto found = std::find_if(window.presentationFeedback.begin(),
		window.presentationFeedback.end(),
		[feedback](const PresentationFeedback &item) {
			return item.proxy == feedback;
		});
	if (found == window.presentationFeedback.end()) {
		return;
	}
	const uint64_t submission = found->submission;
	wp_presentation_feedback_destroy(feedback);
	window.presentationFeedback.erase(found);
	window.frameState.Discarded(submission);
}

void WaylandWindow::FractionalPreferredScale(
	void *data, wp_fractional_scale_v1 *fractionalScale_, uint32_t scale) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (fractionalScale_ != window.fractionalScale || scale == 0) {
		return;
	}
	try {
		window.scaleState.SetFractionalPreferredScale(scale);
	} catch (...) {
		window.callbackFailed = true;
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

void WaylandWindow::KeyboardEnter(void *data, wl_keyboard *, uint32_t serial,
	wl_surface *surface_, wl_array *) {
	auto &window = *static_cast<WaylandWindow *>(data);
	// Grabbed popups receive keyboard focus; retain normal toplevel focus
	// for the main surface. Both record focus so keys continue to flow.
	if (surface_ == window.surface || surface_ == window.popupSurface) {
		window.clipboard.RecordSerial(serial);
		window.primarySelection.RecordSerial(serial);
		window.input.RecordKeyboardFocus(true);
		// Text input stays on the toplevel; the popup does not own IME.
		if (surface_ == window.surface) {
			window.textInput.SetKeyboardFocus(true);
		}
	}
}

void WaylandWindow::KeyboardLeave(void *data, wl_keyboard *, uint32_t, wl_surface *surface_) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (surface_ == window.surface) {
		window.textInput.SetKeyboardFocus(false);
		window.input.RecordKeyboardFocus(false);
		window.input.ResetKeyboardState();
	} else if (surface_ == window.popupSurface) {
		// Leaving the popup for the toplevel restores focus below; do not
		// clear keyboard state until the toplevel also leaves.
	}
}

void WaylandWindow::KeyboardKey(void *data, wl_keyboard *, uint32_t serial, uint32_t time,
	uint32_t key, uint32_t state) {
	auto &window = *static_cast<WaylandWindow *>(data);
	window.clipboard.RecordSerial(serial);
	window.primarySelection.RecordSerial(serial);
	if (state == WL_KEYBOARD_KEY_STATE_PRESSED || state == WL_KEYBOARD_KEY_STATE_RELEASED) {
		window.input.RecordKey(time, key, state == WL_KEYBOARD_KEY_STATE_PRESSED,
			serial);
	}
}

void WaylandWindow::KeyboardModifiers(void *data, wl_keyboard *, uint32_t, uint32_t depressed,
	uint32_t latched, uint32_t locked, uint32_t group) {
	static_cast<WaylandWindow *>(data)->input.UpdateModifiers(
		depressed, latched, locked, group);
}

void WaylandWindow::KeyboardRepeatInfo(
	void *data, wl_keyboard *, int32_t rate, int32_t delay) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (!window.input.SetRepeatInfo(rate, delay)) {
		window.callbackFailed = true;
	}
}

void WaylandWindow::PointerEnter(void *data, wl_pointer *, uint32_t serial,
	wl_surface *surface_, int32_t x, int32_t y) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (surface_ == window.surface) {
		window.input.SetPointerSurface(PointerSurface::Toplevel);
		window.clipboard.RecordSerial(serial);
		window.primarySelection.RecordSerial(serial);
		window.ApplyCursorAction(window.cursorState.Enter(serial));
		if (!window.cursorTheme) {
			window.LoadCursorTheme();
			window.ApplyCursorAction(window.cursorState.SetThemeAvailable(
				window.cursorTheme != nullptr));
		}
		window.input.RecordPointerMotion(0, wl_fixed_to_double(x),
			wl_fixed_to_double(y));
	} else if (surface_ == window.popupSurface) {
		// Popup-local coordinates; do not move the toplevel cursor theme.
		window.input.SetPointerSurface(PointerSurface::ContextPopup);
		window.clipboard.RecordSerial(serial);
		window.primarySelection.RecordSerial(serial);
		window.input.RecordPointerMotion(0, wl_fixed_to_double(x),
			wl_fixed_to_double(y));
	}
}

void WaylandWindow::PointerLeave(void *data, wl_pointer *, uint32_t, wl_surface *surface_) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (surface_ == window.surface || surface_ == window.popupSurface) {
		if (surface_ == window.surface) {
			window.cursorState.Leave();
		}
		window.input.RecordPointerLeave();
	}
}

void WaylandWindow::PointerMotion(void *data, wl_pointer *, uint32_t time,
	int32_t x, int32_t y) {
	static_cast<WaylandWindow *>(data)->input.RecordPointerMotion(
		time, wl_fixed_to_double(x), wl_fixed_to_double(y));
}

void WaylandWindow::PointerButton(void *data, wl_pointer *, uint32_t serial, uint32_t time,
	uint32_t button, uint32_t state) {
	if (state == WL_POINTER_BUTTON_STATE_PRESSED || state == WL_POINTER_BUTTON_STATE_RELEASED) {
		auto &window = *static_cast<WaylandWindow *>(data);
		window.clipboard.RecordSerial(serial);
		window.primarySelection.RecordSerial(serial);
		window.input.RecordPointerButton(
			time, button, state == WL_POINTER_BUTTON_STATE_PRESSED, serial);
	}
}

void WaylandWindow::PointerAxis(void *data, wl_pointer *, uint32_t time,
	uint32_t axis, int32_t value) {
	static_cast<WaylandWindow *>(data)->input.RecordPointerAxis(
		time, axis, wl_fixed_to_double(value));
}

void WaylandWindow::PointerFrame(void *data, wl_pointer *) {
	static_cast<WaylandWindow *>(data)->input.RecordPointerFrame();
}

void WaylandWindow::PointerAxisSource(void *data, wl_pointer *, uint32_t source) {
	static_cast<WaylandWindow *>(data)->input.RecordPointerAxisSource(source);
}

void WaylandWindow::PointerAxisStop(void *data, wl_pointer *, uint32_t time,
	uint32_t axis) {
	static_cast<WaylandWindow *>(data)->input.RecordPointerAxisStop(time, axis);
}

void WaylandWindow::PointerAxisDiscrete(void *data, wl_pointer *, uint32_t axis,
	int32_t discrete) {
	static_cast<WaylandWindow *>(data)->input.RecordPointerAxisDiscrete(axis, discrete);
}

void WaylandWindow::PointerAxisValue120(void *data, wl_pointer *, uint32_t axis,
	int32_t value120) {
	static_cast<WaylandWindow *>(data)->input.RecordPointerAxisValue120(axis, value120);
}

void WaylandWindow::PointerAxisRelativeDirection(void *data, wl_pointer *,
	uint32_t axis, uint32_t direction) {
	static_cast<WaylandWindow *>(data)->input.RecordPointerAxisRelativeDirection(
		axis, direction);
}

void WaylandWindow::OutputGeometry(void *, wl_output *, int32_t, int32_t, int32_t,
	int32_t, int32_t, const char *, const char *, int32_t) {
}

void WaylandWindow::OutputMode(void *, wl_output *, uint32_t, int32_t, int32_t, int32_t) {
}

void WaylandWindow::OutputDone(void *, wl_output *) {
}

void WaylandWindow::OutputScale(void *data, wl_output *output, int32_t factor) {
	auto &window = *static_cast<WaylandWindow *>(data);
	const std::optional<uint32_t> name = window.OutputName(output);
	if (!name || factor <= 0) {
		window.callbackFailed = true;
		return;
	}
	try {
		window.scaleState.SetOutputScale(*name, factor);
	} catch (...) {
		window.callbackFailed = true;
	}
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
			try {
				window.scaleState.EnterOutput(*name);
			} catch (...) {
				window.callbackFailed = true;
			}
		}
	}
}

void WaylandWindow::WaylandSurfaceLeave(void *data, wl_surface *surface_, wl_output *output) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (surface_ == window.surface) {
		if (const std::optional<uint32_t> name = window.OutputName(output)) {
			window.lifecycle.LeaveOutput(*name);
			window.scaleState.LeaveOutput(*name);
		}
	}
}

void WaylandWindow::WaylandSurfacePreferredBufferScale(
	void *data, wl_surface *surface_, int32_t factor) {
	auto &window = *static_cast<WaylandWindow *>(data);
	if (surface_ != window.surface) {
		return;
	}
	if (factor <= 0) {
		window.callbackFailed = true;
		return;
	}
	try {
		window.scaleState.SetPreferredBufferScale(factor);
	} catch (...) {
		window.callbackFailed = true;
	}
}

void WaylandWindow::WaylandSurfacePreferredBufferTransform(void *, wl_surface *, uint32_t) {
}

void WaylandWindow::WmBasePing(void *, xdg_wm_base *wmBase_, uint32_t serial) {
	xdg_wm_base_pong(wmBase_, serial);
}

void WaylandWindow::SurfaceConfigure(void *data, xdg_surface *shellSurface_, uint32_t serial) {
	auto &window = *static_cast<WaylandWindow *>(data);
	xdg_surface_ack_configure(shellSurface_, serial);
	if (const std::optional<WindowSize> resize = window.lifecycle.CommitConfigure()) {
		try {
			window.scaleState.Resize(resize->width, resize->height);
		} catch (...) {
			window.callbackFailed = true;
		}
	}
	window.configured = true;
}

void WaylandWindow::PopupSurfaceConfigure(void *data, xdg_surface *,
	uint32_t serial) {
	auto &window = *static_cast<WaylandWindow *>(data);
	window.popupLifecycle.RecordSurfaceConfigure(serial);
}

void WaylandWindow::PopupConfigure(void *data, xdg_popup *, int32_t x, int32_t y,
	int32_t width, int32_t height) {
	auto &window = *static_cast<WaylandWindow *>(data);
	window.popupLifecycle.RecordPopupConfigure(x, y, width, height);
}

void WaylandWindow::PopupDone(void *data, xdg_popup *) {
	auto &window = *static_cast<WaylandWindow *>(data);
	window.popupLifecycle.RecordPopupDone();
}

bool WaylandWindow::CreateContextMenuPopup(int logicalWidth, int logicalHeight,
	int anchorX, int anchorY, uint32_t serial) {
	if (!display || !wmBase || !shellSurface || !seat || !compositor) {
		return false;
	}
	if (logicalWidth <= 0 || logicalHeight <= 0) {
		return false;
	}
	// Replace any existing popup rather than nesting.
	DestroyContextMenuPopup();

	wl_surface *child = wl_compositor_create_surface(compositor);
	if (!child) {
		return false;
	}
	xdg_surface *childShell = xdg_wm_base_get_xdg_surface(wmBase, child);
	if (!childShell) {
		wl_surface_destroy(child);
		return false;
	}
	if (xdg_surface_add_listener(childShell, &popupSurfaceListener, this) != 0) {
		xdg_surface_destroy(childShell);
		wl_surface_destroy(child);
		return false;
	}

	xdg_positioner *positioner = xdg_wm_base_create_positioner(wmBase);
	if (!positioner) {
		xdg_surface_destroy(childShell);
		wl_surface_destroy(child);
		return false;
	}
	xdg_positioner_set_size(positioner, logicalWidth, logicalHeight);
	// One-pixel parent anchor at the requested point.
	xdg_positioner_set_anchor_rect(positioner, anchorX, anchorY, 1, 1);
	xdg_positioner_set_anchor(positioner, XDG_POSITIONER_ANCHOR_TOP_LEFT);
	// Grow down-right from the anchor; compositor may flip/slide.
	xdg_positioner_set_gravity(positioner, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
	xdg_positioner_set_constraint_adjustment(positioner,
		XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X |
		XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y |
		XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X |
		XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y);

	xdg_popup *childPopup = xdg_surface_get_popup(childShell, shellSurface,
		positioner);
	xdg_positioner_destroy(positioner);
	if (!childPopup) {
		xdg_surface_destroy(childShell);
		wl_surface_destroy(child);
		return false;
	}
	if (xdg_popup_add_listener(childPopup, &popupListener, this) != 0) {
		xdg_popup_destroy(childPopup);
		xdg_surface_destroy(childShell);
		wl_surface_destroy(child);
		return false;
	}

	// Grab must happen before the initial bufferless commit / map.
	xdg_popup_grab(childPopup, seat, serial);
	xdg_surface_set_window_geometry(childShell, 0, 0, logicalWidth,
		logicalHeight);

	popupSurface = child;
	popupShellSurface = childShell;
	popup = childPopup;
	popupLifecycle.Begin();
	// Initial bufferless commit; wait for configure before attaching.
	wl_surface_commit(popupSurface);
	return true;
}

bool WaylandWindow::AckContextMenuPopupConfigure() {
	if (!popupShellSurface) {
		return false;
	}
	const std::optional<uint32_t> serial = popupLifecycle.TakeAckSerial();
	if (!serial) {
		return false;
	}
	xdg_surface_ack_configure(popupShellSurface, *serial);
	const WaylandPopupConfigure &cfg = popupLifecycle.Configure();
	if (cfg.width > 0 && cfg.height > 0) {
		xdg_surface_set_window_geometry(popupShellSurface, 0, 0, cfg.width,
			cfg.height);
	}
	return popupLifecycle.CanPaint();
}

bool WaylandWindow::EnsureContextMenuPopupEglWindow(int bufferWidth,
	int bufferHeight) {
	if (!popupSurface || bufferWidth <= 0 || bufferHeight <= 0) {
		return false;
	}
	if (!popupEglWindow) {
		popupEglWindow = wl_egl_window_create(popupSurface, bufferWidth,
			bufferHeight);
		return popupEglWindow != nullptr;
	}
	wl_egl_window_resize(popupEglWindow, bufferWidth, bufferHeight, 0, 0);
	return true;
}

void WaylandWindow::DestroyContextMenuPopup() noexcept {
	if (popupEglWindow) {
		wl_egl_window_destroy(popupEglWindow);
		popupEglWindow = nullptr;
	}
	if (popup) {
		xdg_popup_destroy(popup);
		popup = nullptr;
	}
	if (popupShellSurface) {
		xdg_surface_destroy(popupShellSurface);
		popupShellSurface = nullptr;
	}
	if (popupSurface) {
		wl_surface_destroy(popupSurface);
		popupSurface = nullptr;
	}
	(void)popupLifecycle.FinishDestroy();
}

void WaylandWindow::ToplevelConfigure(void *data, xdg_toplevel *, int32_t width_,
	int32_t height_, wl_array *states) {
	auto &window = *static_cast<WaylandWindow *>(data);
	std::vector<uint32_t> copiedStates;
	if (states && states->size > 0) {
		const auto *begin = static_cast<const uint32_t *>(states->data);
		copiedStates.assign(begin, begin + states->size / sizeof(uint32_t));
	}
	window.lifecycle.ProposeToplevel(width_, height_, copiedStates);
}

void WaylandWindow::ToplevelClose(void *data, xdg_toplevel *) {
	static_cast<WaylandWindow *>(data)->lifecycle.RequestClose();
}

void WaylandWindow::ToplevelConfigureBounds(void *data, xdg_toplevel *,
	int32_t width_, int32_t height_) {
	static_cast<WaylandWindow *>(data)->lifecycle.ProposeConfigureBounds(width_, height_);
}

void WaylandWindow::ToplevelWmCapabilities(void *data, xdg_toplevel *,
	wl_array *capabilities) {
	std::vector<uint32_t> copiedCapabilities;
	if (capabilities && capabilities->size > 0) {
		const auto *begin = static_cast<const uint32_t *>(capabilities->data);
		copiedCapabilities.assign(
			begin, begin + capabilities->size / sizeof(uint32_t));
	}
	static_cast<WaylandWindow *>(data)->lifecycle.ProposeWmCapabilities(
		copiedCapabilities);
}

void WaylandWindow::DecorationConfigure(void *data,
	zxdg_toplevel_decoration_v1 *, uint32_t mode) {
	static_cast<WaylandWindow *>(data)->lifecycle.ProposeDecoration(
		mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void WaylandWindow::ExportedHandle(void *data,
	zxdg_exported_v2 *exported_, const char *handle) {
	if (!handle) {
		return;
	}
	auto &window = *static_cast<WaylandWindow *>(data);
	try {
		window.portalParent.DeliverHandle(
			reinterpret_cast<uintptr_t>(exported_), handle);
	} catch (...) {
		window.callbackFailed = true;
	}
}

}
