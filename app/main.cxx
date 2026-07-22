#include <exception>
#include <iostream>
#include <string_view>

#include "ApplicationEditor.h"

int main() {
	try {
		Scalpel::ApplicationEditor editor;
		constexpr std::string_view initialText =
			"scalpel-editor\n\n"
			"A direct Scintilla editor for Wayland.\n"
			"The first application frame is rendered offscreen.\n";
		editor.LoadInitialBuffer(initialText);
		editor.SetKeyboardFocus(true);
		editor.RunPendingWork();
		editor.RenderFrame();
		std::cout << "Rendered " << editor.FrameWidth() << 'x' << editor.FrameHeight()
			<< " editor frame (" << editor.FramePixels().size() << " RGBA bytes).\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "scalpel-editor: " << error.what() << '\n';
		return 1;
	}
}
