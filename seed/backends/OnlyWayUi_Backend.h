#pragma once

#include <OnlyWayUi/Config/Config.h>
#include <OnlyWayUi/Core/CoreFwd.h>
#include <OnlyWayUi/Core/Input.h>

namespace OnlyWayUi {
class RenderInterface;
class SystemInterface;
} // namespace OnlyWayUi

using KeyDownCallback = bool (*)(OnlyWayUi::Context* context, OnlyWayUi::Input::KeyIdentifier key, int key_modifier, float native_dp_ratio, bool priority);

/**
    Wayland GL3 backend entry point for samples and backend-dependent tests.

    This interface may be used directly for simple applications and testing. For applications with more advanced event
    loops, use the Wayland platform and GL3 renderer code as a starting point and move the needed pieces into the
    application's main loop.
 */
namespace Backend {

// Initializes the backend, including the custom system and render interfaces, and opens a window for rendering the OnlyWayUi context.
bool Initialize(const char* window_name, int width, int height, bool allow_resize);
// Closes the window and release all resources owned by the backend, including the system and render interfaces.
void Shutdown();

// Returns a pointer to the custom system interface which should be provided to OnlyWayUi.
OnlyWayUi::SystemInterface* GetSystemInterface();
// Returns a pointer to the custom render interface which should be provided to OnlyWayUi.
OnlyWayUi::RenderInterface* GetRenderInterface();

// Polls and processes events from the current platform, and applies any relevant events to the provided OnlyWayUi context and the key down callback.
// @return False to indicate that the application should be closed.
bool ProcessEvents(OnlyWayUi::Context* context, KeyDownCallback key_down_callback = nullptr, bool power_save = false);
// Request application closure during the next event processing call.
void RequestExit();
// By default a compositor close request (decoration close button) exits the application at the next ProcessEvents call.
// Enable interception to instead have the application handle those requests: ProcessEvents keeps returning true, and the
// application must poll ConsumeCloseRequest and decide when to exit via RequestExit. Used to confirm unsaved work first.
void SetCloseRequestInterception(bool intercept);
// Returns true once if the compositor has requested the window be closed (for example, the decoration close button)
// since the last call, then clears the request. Only meaningful when close-request interception is enabled; the
// application decides whether to honor it, typically after confirming unsaved work, by calling RequestExit. Ignoring the
// request keeps the window open, which Wayland allows.
bool ConsumeCloseRequest();

// Prepares the render state to accept rendering commands from OnlyWayUi, call before rendering the OnlyWayUi context.
void BeginFrame();
// Presents the rendered frame to the screen, call after rendering the OnlyWayUi context.
void PresentFrame();
// Renders and presents the context if it has visual changes pending.
bool RenderFrame(OnlyWayUi::Context* context);

// Whether the dialog asks the user to open existing files or to choose a save location.
enum class FileDialogMode { Open, Save };

// One selectable filter in the dialog, e.g. name "Images" with patterns {"*.png", "*.jpg"}. Patterns are glob-style and
// case-sensitive, matching the portal's convention.
struct FileDialogFilter {
	OnlyWayUi::String name;
	OnlyWayUi::StringList patterns;
};

struct FileDialogRequest {
	FileDialogMode mode = FileDialogMode::Open;
	OnlyWayUi::String title;
	// Optional starting directory as a local filesystem path. Empty lets the portal decide.
	OnlyWayUi::String current_folder;
	// Save mode only: the default file name shown in the dialog.
	OnlyWayUi::String suggested_name;
	OnlyWayUi::Vector<FileDialogFilter> filters;
	// Open mode only: allow selecting more than one file.
	bool allow_multiple = false;
};

struct FileDialogResult {
	// False when the user cancelled or the dialog could not complete; paths is then empty.
	bool accepted = false;
	// Chosen files as decoded local filesystem paths.
	OnlyWayUi::StringList paths;
};

using FileDialogCallback = OnlyWayUi::Function<void(const FileDialogResult&)>;

// Opens a desktop file chooser (through xdg-desktop-portal). This may briefly contact the session bus to start and check
// the portal, but it does not wait for user input. The callback fires later, exactly once, during a ProcessEvents call,
// with the user's choice or a cancellation. Returns false if no dialog could be started, for example when no session bus
// or portal is available; the callback is not invoked in that case, so the caller can fall back. A non-empty callback is
// required.
bool ShowFileDialog(const FileDialogRequest& request, FileDialogCallback callback);

} // namespace Backend
