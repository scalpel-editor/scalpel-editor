// Input translation for the current Wayland seat.

#ifndef WAYLANDINPUT_H
#define WAYLANDINPUT_H

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "EditorInputTypes.h"

struct xkb_context;
struct xkb_keymap;
struct xkb_state;

namespace Scalpel {

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

using InputEvent = std::variant<KeyboardInput, PointerInput>;

/**
 * xkbcommon state and translated events for one current Wayland seat.
 *
 * Key repeat and compose state deliberately stay out of this phase-6 type.
 */
class WaylandInput final {
public:
	WaylandInput();
	~WaylandInput() noexcept;

	WaylandInput(const WaylandInput &) = delete;
	WaylandInput(WaylandInput &&) = delete;
	WaylandInput &operator=(const WaylandInput &) = delete;
	WaylandInput &operator=(WaylandInput &&) = delete;

	[[nodiscard]] bool SetKeymap(std::string_view keymapText);
	void ResetKeyboardState();
	void UpdateModifiers(uint32_t depressed, uint32_t latched, uint32_t locked,
		uint32_t group);
	void RecordKey(uint32_t time, uint32_t key, bool pressed);
	void RecordPointerMotion(uint32_t time, double x, double y);
	void RecordPointerLeave();
	void RecordPointerButton(uint32_t time, uint32_t button, bool pressed);
	void RecordPointerAxis(uint32_t time, uint32_t axis, double value);
	[[nodiscard]] std::vector<InputEvent> TakeInputs();

private:
	[[nodiscard]] Scintilla::KeyMod CurrentModifiers() const;

	xkb_context *context = nullptr;
	xkb_keymap *keymap = nullptr;
	xkb_state *state = nullptr;
	double pointerX = 0;
	double pointerY = 0;
	std::vector<InputEvent> inputs;
};

}

#endif
