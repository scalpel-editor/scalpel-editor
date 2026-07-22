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
};

enum class PointerAction {
	Move,
	Leave,
	Press,
	Release,
	Scroll,
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
};

using InputEvent = std::variant<KeyboardFocusInput, KeyboardInput, PointerInput>;

}

#endif
