// Input events delivered to the application editor.

#ifndef APPLICATIONINPUT_H
#define APPLICATIONINPUT_H

#include <cstdint>
#include <string>
#include <variant>

#include "EditorInputTypes.h"

namespace Scalpel {

struct KeyboardFocusInput {
	bool focused = false;
};

struct KeyboardInput {
	Scintilla::Keys key = static_cast<Scintilla::Keys>(0);
	Scintilla::KeyMod modifiers = Scintilla::KeyMod::Norm;
	std::string text;
	uint32_t time = 0;
	bool pressed = false;
	/**
	 * Compositor input serial for the key event. Required for xdg_popup.grab
	 * when Shift+F10 opens the context menu; zero when unknown or in tests.
	 */
	uint32_t serial = 0;
};

enum class PointerAction {
	Move,
	Leave,
	Press,
	Release,
	Scroll,
};

/**
 * Which application surface the pointer event targets. Wayland coordinates are
 * local to the entered surface; ContextPopup events use popup-local layout.
 */
enum class PointerSurface {
	Toplevel,
	ContextPopup,
};

struct PointerInput {
	PointerAction action = PointerAction::Move;
	Scintilla::KeyMod modifiers = Scintilla::KeyMod::Norm;
	double x = 0;
	double y = 0;
	double deltaX = 0;
	double deltaY = 0;
	uint32_t time = 0;
	int button = -1;
	/**
	 * Compositor input serial for the button event. Required for xdg_popup.grab
	 * when a right press opens the context menu; zero when unknown or in tests.
	 */
	uint32_t serial = 0;
	PointerSurface surface = PointerSurface::Toplevel;
};

using InputEvent = std::variant<KeyboardFocusInput, KeyboardInput, PointerInput>;

}

#endif
