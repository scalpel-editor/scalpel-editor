// Input translation for the current Wayland seat.

#ifndef WAYLANDINPUT_H
#define WAYLANDINPUT_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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
 * Key repeat and compose state are added in later Phase 7 steps.
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
	void SetPointerVersion(uint32_t version) noexcept;
	void RecordKeyboardFocus(bool focused);
	void UpdateModifiers(uint32_t depressed, uint32_t latched, uint32_t locked,
		uint32_t group);
	void RecordKey(uint32_t time, uint32_t key, bool pressed);
	void RecordPointerMotion(uint32_t time, double x, double y);
	void RecordPointerLeave();
	void RecordPointerButton(uint32_t time, uint32_t button, bool pressed);
	void RecordPointerAxis(uint32_t time, uint32_t axis, double value);
	void RecordPointerFrame();
	void RecordPointerAxisSource(uint32_t source) noexcept;
	void RecordPointerAxisStop(uint32_t time, uint32_t axis) noexcept;
	void RecordPointerAxisDiscrete(uint32_t axis, int32_t discrete) noexcept;
	void RecordPointerAxisValue120(uint32_t axis, int32_t value120) noexcept;
	void RecordPointerAxisRelativeDirection(uint32_t axis, uint32_t direction) noexcept;
	[[nodiscard]] std::vector<InputEvent> TakeInputs();

private:
	struct PointerAxisState {
		double continuous = 0;
		int32_t discrete = 0;
		int32_t value120 = 0;
		std::optional<uint32_t> relativeDirection;
		bool hasContinuous = false;
		bool hasDiscrete = false;
		bool hasValue120 = false;
		bool stopped = false;
	};

	[[nodiscard]] Scintilla::KeyMod CurrentModifiers() const;
	[[nodiscard]] static std::optional<size_t> PointerAxisIndex(uint32_t axis) noexcept;
	void ResetPointerFrame() noexcept;

	xkb_context *context = nullptr;
	xkb_keymap *keymap = nullptr;
	xkb_state *state = nullptr;
	double pointerX = 0;
	double pointerY = 0;
	std::array<bool, 3> pointerButtons{};
	std::array<PointerAxisState, 2> pointerAxes{};
	std::optional<uint32_t> pointerAxisSource;
	uint32_t pointerAxisTime = 0;
	bool pointerFrames = false;
	bool keyboardFocused = false;
	bool pointerFocused = false;
	std::vector<InputEvent> inputs;
};

}

#endif
