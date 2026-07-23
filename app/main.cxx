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
			if (const std::optional<Scalpel::WindowSize> resize = window.TakeResize()) {
				editor.Resize(resize->width, resize->height);
			}
			for (const Scalpel::InputEvent &input : window.TakeInputs()) {
				if (const auto *focus = std::get_if<Scalpel::KeyboardFocusInput>(&input)) {
					editor.SetKeyboardFocus(focus->focused);
				} else if (const auto *keyboard = std::get_if<Scalpel::KeyboardInput>(&input)) {
					editor.HandleKeyboardInput(*keyboard);
				} else {
					editor.HandlePointerInput(std::get<Scalpel::PointerInput>(input));
				}
			}
			editor.RunPendingWork();
			window.SetCursor(editor.WindowState().cursor);
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
