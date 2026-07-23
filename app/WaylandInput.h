// Input translation for the current Wayland seat.

#ifndef WAYLANDINPUT_H
#define WAYLANDINPUT_H

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ApplicationInput.h"

struct xkb_context;
struct xkb_compose_state;
struct xkb_compose_table;
struct xkb_keymap;
struct xkb_state;

namespace Scalpel {

/**
 * xkbcommon state and translated events for the active Wayland seat.
 *
 * Compose state is locale-aware and falls back to direct xkb text when the
 * locale has no compose table. Repeat timing follows the active compositor
 * configuration and a controlled monotonic clock.
 */
class WaylandInput final {
public:
	using Clock = std::chrono::steady_clock;
	using NowFunction = std::function<Clock::time_point()>;

	WaylandInput();
	explicit WaylandInput(std::string_view composeLocale);
	WaylandInput(std::string_view composeLocale, NowFunction now);
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
	[[nodiscard]] bool SetRepeatInfo(int32_t rate, int32_t delay) noexcept;
	[[nodiscard]] std::optional<std::chrono::milliseconds> TimeUntilKeyRepeat() const;
	/** Emit repeats due at the current clock, returning whether any were emitted. */
	[[nodiscard]] bool RunKeyRepeat();
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
		bool hasContinuous = false;
		bool hasDiscrete = false;
		bool hasValue120 = false;
	};
	struct RepeatState {
		Clock::time_point next;
		std::chrono::milliseconds interval{0};
		uint32_t key = 0;
		uint32_t time = 0;
		int32_t rate = 0;
		int32_t delay = 0;
		bool active = false;
	};

	[[nodiscard]] Scintilla::KeyMod CurrentModifiers() const;
	void AppendKey(uint32_t time, uint32_t key, bool pressed);
	void StartRepeat(uint32_t time, uint32_t key);
	void StopRepeat() noexcept;
	[[nodiscard]] std::string TextForKey(uint32_t keycode, uint32_t keysym,
		Scintilla::KeyMod modifiers);
	void ResetCompose() noexcept;
	[[nodiscard]] static std::optional<size_t> PointerAxisIndex(uint32_t axis) noexcept;
	void ResetPointerFrame() noexcept;

	NowFunction now;
	xkb_context *context = nullptr;
	xkb_compose_table *composeTable = nullptr;
	xkb_compose_state *composeState = nullptr;
	xkb_keymap *keymap = nullptr;
	xkb_state *state = nullptr;
	double pointerX = 0;
	double pointerY = 0;
	std::array<bool, 3> pointerButtons{};
	std::array<PointerAxisState, 2> pointerAxes{};
	RepeatState repeat;
	uint32_t pointerAxisTime = 0;
	bool pointerFrames = false;
	bool keyboardFocused = false;
	bool pointerFocused = false;
	std::vector<InputEvent> inputs;
};

}

#endif
