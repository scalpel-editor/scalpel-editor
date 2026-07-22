#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>

#include "ApplicationEditor.h"
#include "GlContext.h"
#include "WaylandWindow.h"

int main() {
	try {
		Scalpel::WaylandWindow window("scalpel-editor", 800, 600);
		auto glContext = std::make_unique<Scintilla::Internal::GlContext>(
			window.Display(), window.EglWindow());
		Scalpel::ApplicationEditor editor(
			std::move(glContext), window.Width(), window.Height());
		constexpr std::string_view initialText =
			"scalpel-editor\n\n"
			"A direct Scintilla editor for Wayland.\n"
			"The first application frame is rendered in an xdg-toplevel.\n";
		editor.LoadInitialBuffer(initialText);
		while (!window.CloseRequested()) {
			for (const Scalpel::KeyboardInput &input : window.TakeKeyboardInputs()) {
				editor.HandleKeyboardInput(input);
			}
			if (const std::optional<Scalpel::WindowSize> resize = window.TakeResize()) {
				editor.Resize(resize->width, resize->height);
			}
			if (const std::optional<bool> focused = window.TakeKeyboardFocus()) {
				editor.SetKeyboardFocus(*focused);
			}
			editor.RunPendingWork();
			if (editor.NeedsRedraw()) {
				editor.PresentFrame();
			}
			if (!window.CloseRequested()) {
				window.WaitForEvents(editor.TimeUntilNextWork());
			}
		}
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "scalpel-editor: " << error.what() << '\n';
		return 1;
	}
}
