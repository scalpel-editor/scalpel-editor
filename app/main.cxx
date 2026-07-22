#include <exception>
#include <iostream>
#include <memory>
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
		editor.RunPendingWork();
		editor.PresentFrame();
		window.RoundTrip();
		std::cout << "Presented " << editor.FrameWidth() << 'x' << editor.FrameHeight()
			<< " editor frame to Wayland.\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "scalpel-editor: " << error.what() << '\n';
		return 1;
	}
}
