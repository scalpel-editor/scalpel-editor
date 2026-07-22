// Input translation for the current Wayland seat.

#ifndef WAYLANDINPUT_H
#define WAYLANDINPUT_H

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "ApplicationInput.h"

struct xkb_context;
struct xkb_keymap;
struct xkb_state;

namespace Scalpel {

/**
 * xkbcommon state and translated events for the active Wayland seat.
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

	/** Load a Wayland keymap buffer whose size includes its trailing NUL. */
	[[nodiscard]] bool SetKeymap(std::string_view keymapText);
	void ResetKeyboardState();
	void ResetKeyboardDevice();
	void ResetPointerDevice();
	void RecordKeyboardFocus(bool focused);
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
	std::array<bool, 3> pointerButtons{};
	bool keyboardFocused = false;
	bool pointerFocused = false;
	std::vector<InputEvent> inputs;
};

}

#endif
