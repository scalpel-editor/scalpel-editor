#include "ElementScintilla.h"
#include "TextEditorEventListener.h"
#include "TextEditorWindow.h"
#include <OnlyWayUi/Core.h>
#include <OnlyWayUi/Debugger.h>
#include <OnlyWayUi_Backend.h>
#include <Shell.h>

int main(int argc, char** argv)
{
	const int window_width = 1100;
	const int window_height = 760;
	const char* initial_path = argc > 1 ? argv[1] : nullptr;

	if (!Shell::Initialize())
		return -1;

	if (!Backend::Initialize("Text Editor Sample", window_width, window_height, true))
	{
		Shell::Shutdown();
		return -1;
	}

	OnlyWayUi::SetSystemInterface(Backend::GetSystemInterface());
	OnlyWayUi::SetRenderInterface(Backend::GetRenderInterface());

	OnlyWayUi::Initialise();

	OnlyWayUi::Context* context = OnlyWayUi::CreateContext("main", OnlyWayUi::Vector2i(window_width, window_height));
	if (!context)
	{
		OnlyWayUi::Shutdown();
		Backend::Shutdown();
		Shell::Shutdown();
		return -1;
	}

	OnlyWayUi::Debugger::Initialise(context);
	Shell::LoadFonts();
	OnlyWayUi::ElementInstancerGeneric<OnlyWayUi::Editor::ElementScintilla> scintilla_instancer;
	OnlyWayUi::Factory::RegisterElementInstancer("scintilla", &scintilla_instancer);

	TextEditorWindow editor_window;
	TextEditorEventListenerInstancer event_listener_instancer{&editor_window};
	OnlyWayUi::Factory::RegisterEventListenerInstancer(&event_listener_instancer);

	if (!editor_window.Initialize(context, initial_path))
	{
		OnlyWayUi::Shutdown();
		Backend::Shutdown();
		Shell::Shutdown();
		return -1;
	}

	editor_window.GetDocument()->AddEventListener(OnlyWayUi::EventId::Keydown, &editor_window, true);

	// Handle the compositor close button ourselves so unsaved work can be confirmed before exiting.
	Backend::SetCloseRequestInterception(true);

	bool running = true;
	while (running)
	{
		running = Backend::ProcessEvents(context, &Shell::ProcessKeyDownShortcuts, true);

		// The compositor close button is a request, not a command. Confirm before losing unsaved work; otherwise exit.
		if (Backend::ConsumeCloseRequest())
		{
			if (editor_window.HasUnsavedChanges())
				editor_window.ShowCloseConfirmation();
			else
				Backend::RequestExit();
		}

		context->Update();
		Backend::RenderFrame(context);
	}

	editor_window.Shutdown();

	OnlyWayUi::Shutdown();
	Backend::Shutdown();
	Shell::Shutdown();

	return 0;
}
