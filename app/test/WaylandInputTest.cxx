#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include <linux/input-event-codes.h>
#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon.h>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "WaylandInput.h"

namespace {

struct TestKeymap {
	std::string text;
	uint32_t shiftMask = 0;
	uint32_t controlMask = 0;
};

TestKeymap MakeTestKeymap(const char *variant = nullptr) {
	xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	REQUIRE(context);
	const xkb_rule_names names{nullptr, nullptr, "us", variant, nullptr};
	xkb_keymap *keymap = xkb_keymap_new_from_names(
		context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
	REQUIRE(keymap);
	char *text = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
	REQUIRE(text);
	TestKeymap result;
	result.text = text;
	result.text.push_back('\0');
	std::free(text);
	const xkb_mod_index_t shift = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
	const xkb_mod_index_t control = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
	REQUIRE(shift != XKB_MOD_INVALID);
	REQUIRE(control != XKB_MOD_INVALID);
	result.shiftMask = uint32_t{1} << shift;
	result.controlMask = uint32_t{1} << control;
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	return result;
}

}

TEST_CASE("Wayland keyboard translates presses and releases") {
	const TestKeymap keymap = MakeTestKeymap();
	Scalpel::WaylandInput input;
	CHECK_FALSE(input.SetKeymap({}));
	CHECK_FALSE(input.SetKeymap(std::string_view(keymap.text.data(), keymap.text.size() - 1)));
	REQUIRE(input.SetKeymap(keymap.text));

	input.RecordKey(15, KEY_A, true);
	input.RecordKey(16, KEY_A, false);
	const std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 2);
	const auto &press = std::get<Scalpel::KeyboardInput>(events[0]);
	const auto &release = std::get<Scalpel::KeyboardInput>(events[1]);
	CHECK(press.key == static_cast<Scintilla::Keys>('A'));
	CHECK(press.modifiers == Scintilla::KeyMod::Norm);
	CHECK(press.text == "a");
	CHECK(press.time == 15);
	CHECK(press.pressed);
	CHECK(release.key == static_cast<Scintilla::Keys>('A'));
	CHECK(release.text.empty());
	CHECK_FALSE(release.pressed);
	CHECK(input.TakeInputs().empty());
}

TEST_CASE("Wayland keyboard applies modifiers and maps command keys") {
	const TestKeymap keymap = MakeTestKeymap();
	Scalpel::WaylandInput input;
	REQUIRE(input.SetKeymap(keymap.text));

	input.UpdateModifiers(keymap.shiftMask, 0, 0, 0);
	input.RecordKey(20, KEY_A, true);
	input.UpdateModifiers(keymap.controlMask, 0, 0, 0);
	input.RecordKey(21, KEY_LEFT, true);
	const std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 2);
	const auto &shifted = std::get<Scalpel::KeyboardInput>(events[0]);
	const auto &controlled = std::get<Scalpel::KeyboardInput>(events[1]);
	CHECK(shifted.key == static_cast<Scintilla::Keys>('A'));
	CHECK(shifted.modifiers == Scintilla::KeyMod::Shift);
	CHECK(shifted.text == "A");
	CHECK(controlled.key == Scintilla::Keys::Left);
	CHECK(controlled.modifiers == Scintilla::KeyMod::Ctrl);
	CHECK(controlled.text.empty());
}

TEST_CASE("Wayland keyboard composes locale text") {
	const TestKeymap keymap = MakeTestKeymap("intl");
	Scalpel::WaylandInput input("C.utf8");
	REQUIRE(input.SetKeymap(keymap.text));

	input.RecordKey(22, KEY_APOSTROPHE, true);
	input.RecordKey(23, KEY_E, true);
	const std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 2);
	CHECK(std::get<Scalpel::KeyboardInput>(events[0]).text.empty());
	CHECK(std::get<Scalpel::KeyboardInput>(events[1]).text == "é");
}

TEST_CASE("Wayland keyboard compose cancellation discards the sequence") {
	const TestKeymap keymap = MakeTestKeymap("intl");
	Scalpel::WaylandInput input("C.utf8");
	REQUIRE(input.SetKeymap(keymap.text));

	input.RecordKey(24, KEY_APOSTROPHE, true);
	input.RecordKey(25, KEY_LEFT, true);
	input.RecordKey(26, KEY_A, true);
	const std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 3);
	CHECK(std::get<Scalpel::KeyboardInput>(events[0]).text.empty());
	CHECK(std::get<Scalpel::KeyboardInput>(events[1]).text.empty());
	CHECK(std::get<Scalpel::KeyboardInput>(events[2]).text == "a");
}

TEST_CASE("Wayland keyboard command keys bypass compose state") {
	const TestKeymap keymap = MakeTestKeymap("intl");
	Scalpel::WaylandInput input("C.utf8");
	REQUIRE(input.SetKeymap(keymap.text));

	input.RecordKey(27, KEY_APOSTROPHE, true);
	input.UpdateModifiers(keymap.controlMask, 0, 0, 0);
	input.RecordKey(28, KEY_A, true);
	input.UpdateModifiers(0, 0, 0, 0);
	input.RecordKey(29, KEY_E, true);
	const std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 3);
	CHECK(std::get<Scalpel::KeyboardInput>(events[1]).key ==
		static_cast<Scintilla::Keys>('A'));
	CHECK(std::get<Scalpel::KeyboardInput>(events[1]).text.empty());
	CHECK(std::get<Scalpel::KeyboardInput>(events[2]).text == "é");
}

TEST_CASE("Wayland keyboard resets unfinished compose state") {
	const TestKeymap keymap = MakeTestKeymap("intl");
	Scalpel::WaylandInput input("C.utf8");
	REQUIRE(input.SetKeymap(keymap.text));

	input.RecordKeyboardFocus(true);
	input.RecordKey(30, KEY_APOSTROPHE, true);
	input.RecordKeyboardFocus(false);
	input.RecordKey(31, KEY_E, true);
	std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 4);
	CHECK(std::get<Scalpel::KeyboardInput>(events[3]).text == "e");

	input.RecordKey(32, KEY_APOSTROPHE, true);
	REQUIRE(input.SetKeymap(keymap.text));
	input.RecordKey(33, KEY_E, true);
	events = input.TakeInputs();
	REQUIRE(events.size() == 2);
	CHECK(std::get<Scalpel::KeyboardInput>(events[1]).text == "e");
}

TEST_CASE("Wayland keyboard keeps direct text when compose locale fails") {
	const TestKeymap keymap = MakeTestKeymap();
	Scalpel::WaylandInput input("not_a_real_compose_locale");
	REQUIRE(input.SetKeymap(keymap.text));

	input.RecordKey(30, KEY_A, true);
	const std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 1);
	CHECK(std::get<Scalpel::KeyboardInput>(events[0]).text == "a");
}

TEST_CASE("Wayland keyboard repeat follows compositor timing") {
	using namespace std::chrono_literals;
	const TestKeymap keymap = MakeTestKeymap();
	Scalpel::WaylandInput::Clock::time_point now{};
	Scalpel::WaylandInput input("C.utf8", [&now] { return now; });
	REQUIRE(input.SetKeymap(keymap.text));
	REQUIRE(input.SetRepeatInfo(25, 400));

	input.RecordKey(10, KEY_A, true);
	REQUIRE(input.TakeInputs().size() == 1);
	CHECK(input.TimeUntilKeyRepeat() == 400ms);
	now += 399ms;
	CHECK_FALSE(input.RunKeyRepeat());
	CHECK(input.TimeUntilKeyRepeat() == 1ms);
	now += 1ms;
	CHECK(input.RunKeyRepeat());
	std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 1);
	CHECK(std::get<Scalpel::KeyboardInput>(events[0]).text == "a");
	CHECK(std::get<Scalpel::KeyboardInput>(events[0]).time == 10);
	CHECK(input.TimeUntilKeyRepeat() == 40ms);

	now += 120ms;
	CHECK(input.RunKeyRepeat());
	events = input.TakeInputs();
	REQUIRE(events.size() == 3);
	CHECK(input.TimeUntilKeyRepeat() == 40ms);
}

TEST_CASE("Wayland keyboard repeat applies timing changes") {
	using namespace std::chrono_literals;
	const TestKeymap keymap = MakeTestKeymap();
	Scalpel::WaylandInput::Clock::time_point now{};
	Scalpel::WaylandInput input("C.utf8", [&now] { return now; });
	REQUIRE(input.SetKeymap(keymap.text));
	REQUIRE(input.SetRepeatInfo(25, 400));
	input.RecordKey(11, KEY_A, true);
	REQUIRE(input.TakeInputs().size() == 1);

	now += 100ms;
	REQUIRE(input.SetRepeatInfo(50, 200));
	CHECK(input.TimeUntilKeyRepeat() == 200ms);
	now += 200ms;
	CHECK(input.RunKeyRepeat());
	REQUIRE(input.TakeInputs().size() == 1);
	CHECK(input.TimeUntilKeyRepeat() == 20ms);
	REQUIRE(input.SetRepeatInfo(0, 0));
	CHECK_FALSE(input.TimeUntilKeyRepeat().has_value());
	input.RecordKey(11, KEY_A, false);
	REQUIRE(input.TakeInputs().size() == 1);
	REQUIRE(input.SetRepeatInfo(50, 100));
	input.RecordKey(11, KEY_A, true);
	REQUIRE(input.TakeInputs().size() == 1);
	CHECK_FALSE(input.SetRepeatInfo(-1, 0));
	CHECK_FALSE(input.TimeUntilKeyRepeat().has_value());
	input.RecordKey(11, KEY_A, false);
	input.RecordKey(11, KEY_A, true);
	REQUIRE(input.TakeInputs().size() == 2);
	now += 100ms;
	CHECK_FALSE(input.RunKeyRepeat());
	REQUIRE(input.SetRepeatInfo(20, 100));
	CHECK_FALSE(input.SetRepeatInfo(20, -1));
	input.RecordKey(11, KEY_A, false);
	input.RecordKey(11, KEY_A, true);
	REQUIRE(input.TakeInputs().size() == 2);
	now += 100ms;
	CHECK_FALSE(input.RunKeyRepeat());
}

TEST_CASE("Wayland keyboard repeat clock exceptions propagate") {
	const TestKeymap keymap = MakeTestKeymap();
	Scalpel::WaylandInput::Clock::time_point now{};
	bool clockFails = false;
	Scalpel::WaylandInput input("C.utf8", [&] {
		if (clockFails) {
			throw std::runtime_error("clock failed");
		}
		return now;
	});
	REQUIRE(input.SetKeymap(keymap.text));
	REQUIRE(input.SetRepeatInfo(25, 400));
	input.RecordKey(11, KEY_A, true);
	REQUIRE(input.TakeInputs().size() == 1);

	clockFails = true;
	CHECK_THROWS_AS(input.SetRepeatInfo(50, 200), std::runtime_error);
	clockFails = false;
	CHECK(input.TimeUntilKeyRepeat() == std::chrono::milliseconds(400));
}

TEST_CASE("Wayland keyboard repeat respects key repeatability and cancellation") {
	using namespace std::chrono_literals;
	const TestKeymap keymap = MakeTestKeymap();
	Scalpel::WaylandInput::Clock::time_point now{};
	Scalpel::WaylandInput input("C.utf8", [&now] { return now; });
	REQUIRE(input.SetKeymap(keymap.text));
	REQUIRE(input.SetRepeatInfo(100, 10));

	input.RecordKey(12, KEY_A, true);
	input.RecordKey(13, KEY_A, false);
	REQUIRE(input.TakeInputs().size() == 2);
	now += 10ms;
	CHECK_FALSE(input.RunKeyRepeat());

	input.RecordKey(14, KEY_A, true);
	REQUIRE(input.TakeInputs().size() == 1);
	input.RecordKeyboardFocus(false);
	now += 10ms;
	CHECK_FALSE(input.RunKeyRepeat());

	input.RecordKey(15, KEY_A, true);
	REQUIRE(input.TakeInputs().size() == 1);
	input.ResetKeyboardState();
	now += 10ms;
	CHECK_FALSE(input.RunKeyRepeat());

	input.RecordKey(16, KEY_A, true);
	REQUIRE(input.TakeInputs().size() == 1);
	REQUIRE(input.SetKeymap(keymap.text));
	now += 10ms;
	CHECK_FALSE(input.RunKeyRepeat());

	input.RecordKey(17, KEY_A, true);
	input.RecordKey(18, KEY_LEFTSHIFT, true);
	REQUIRE(input.TakeInputs().size() == 2);
	now += 10ms;
	CHECK_FALSE(input.RunKeyRepeat());

	input.RecordKey(19, KEY_A, true);
	REQUIRE(input.TakeInputs().size() == 1);
	input.ResetKeyboardDevice();
	now += 10ms;
	CHECK_FALSE(input.RunKeyRepeat());
}

TEST_CASE("Wayland keyboard repeat limits deadline catch-up") {
	using namespace std::chrono_literals;
	const TestKeymap keymap = MakeTestKeymap();
	Scalpel::WaylandInput::Clock::time_point now{};
	Scalpel::WaylandInput input("C.utf8", [&now] { return now; });
	REQUIRE(input.SetKeymap(keymap.text));
	REQUIRE(input.SetRepeatInfo(100, 0));
	input.RecordKey(20, KEY_A, true);
	REQUIRE(input.TakeInputs().size() == 1);

	now += 1s;
	CHECK(input.RunKeyRepeat());
	CHECK(input.TakeInputs().size() == 32);
	CHECK(input.TimeUntilKeyRepeat() == 10ms);
}

TEST_CASE("Wayland keyboard retains focus and key event order") {
	const TestKeymap keymap = MakeTestKeymap();
	Scalpel::WaylandInput input;
	REQUIRE(input.SetKeymap(keymap.text));

	input.RecordKeyboardFocus(true);
	input.RecordKey(25, KEY_A, true);
	input.RecordKeyboardFocus(false);
	input.RecordKeyboardFocus(false);
	const std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 3);
	CHECK(std::get<Scalpel::KeyboardFocusInput>(events[0]).focused);
	CHECK(std::get<Scalpel::KeyboardInput>(events[1]).time == 25);
	CHECK_FALSE(std::get<Scalpel::KeyboardFocusInput>(events[2]).focused);
}

TEST_CASE("Wayland keyboard teardown removes stale input and reports focus loss") {
	const TestKeymap keymap = MakeTestKeymap();
	Scalpel::WaylandInput input;
	REQUIRE(input.SetKeymap(keymap.text));

	input.RecordKeyboardFocus(true);
	input.UpdateModifiers(keymap.shiftMask, 0, 0, 0);
	input.RecordKey(25, KEY_A, true);
	input.ResetKeyboardDevice();
	const std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 1);
	CHECK_FALSE(std::get<Scalpel::KeyboardFocusInput>(events[0]).focused);
	input.RecordKey(26, KEY_A, true);
	CHECK(input.TakeInputs().empty());

	input.RecordPointerMotion(30, 12.5, 18.25);
	const auto pointer = std::get<Scalpel::PointerInput>(input.TakeInputs().front());
	CHECK(pointer.modifiers == Scintilla::KeyMod::Norm);
}

TEST_CASE("Wayland pointer retains coordinates modifiers buttons and axes") {
	const TestKeymap keymap = MakeTestKeymap();
	Scalpel::WaylandInput input;
	REQUIRE(input.SetKeymap(keymap.text));
	input.UpdateModifiers(keymap.shiftMask, 0, 0, 0);

	input.RecordPointerMotion(30, 12.5, 18.25);
	input.RecordPointerButton(31, BTN_LEFT, true);
	input.RecordPointerAxis(32, WL_POINTER_AXIS_VERTICAL_SCROLL, 10.0);
	input.RecordPointerButton(33, BTN_LEFT, false);
	input.RecordPointerLeave();
	const std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 5);
	const auto &motion = std::get<Scalpel::PointerInput>(events[0]);
	CHECK(motion.action == Scalpel::PointerAction::Move);
	CHECK(motion.modifiers == Scintilla::KeyMod::Shift);
	CHECK(motion.x == 12.5);
	CHECK(motion.y == 18.25);
	const auto &press = std::get<Scalpel::PointerInput>(events[1]);
	CHECK(press.action == Scalpel::PointerAction::Press);
	CHECK(press.button == 0);
	CHECK(press.time == 31);
	const auto &scroll = std::get<Scalpel::PointerInput>(events[2]);
	CHECK(scroll.action == Scalpel::PointerAction::Scroll);
	CHECK(scroll.deltaX == 0);
	CHECK(scroll.deltaY == 10.0);
	CHECK(std::get<Scalpel::PointerInput>(events[3]).action ==
		Scalpel::PointerAction::Release);
	CHECK(std::get<Scalpel::PointerInput>(events[4]).action ==
		Scalpel::PointerAction::Leave);
}

TEST_CASE("Wayland pointer coalesces current axis frames") {
	Scalpel::WaylandInput input;
	input.SetPointerVersion(9);
	input.RecordPointerMotion(30, 12.5, 18.25);
	REQUIRE(input.TakeInputs().size() == 1);

	input.RecordPointerAxisSource(WL_POINTER_AXIS_SOURCE_WHEEL);
	input.RecordPointerAxis(31, WL_POINTER_AXIS_HORIZONTAL_SCROLL, 40.0);
	input.RecordPointerAxisDiscrete(WL_POINTER_AXIS_HORIZONTAL_SCROLL, 2);
	input.RecordPointerAxisValue120(WL_POINTER_AXIS_HORIZONTAL_SCROLL, 120);
	input.RecordPointerAxisRelativeDirection(WL_POINTER_AXIS_HORIZONTAL_SCROLL,
		WL_POINTER_AXIS_RELATIVE_DIRECTION_INVERTED);
	input.RecordPointerAxis(32, WL_POINTER_AXIS_VERTICAL_SCROLL, -30.0);
	input.RecordPointerAxisValue120(WL_POINTER_AXIS_VERTICAL_SCROLL, -60);
	CHECK(input.TakeInputs().empty());
	input.RecordPointerFrame();

	const std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 1);
	const auto &scroll = std::get<Scalpel::PointerInput>(events.front());
	CHECK(scroll.action == Scalpel::PointerAction::Scroll);
	CHECK(scroll.x == 12.5);
	CHECK(scroll.y == 18.25);
	CHECK(scroll.deltaX == 10.0);
	CHECK(scroll.deltaY == -5.0);
	CHECK(scroll.time == 32);
}

TEST_CASE("Wayland pointer uses discrete and continuous frame fallbacks") {
	Scalpel::WaylandInput input;
	input.SetPointerVersion(7);
	input.RecordPointerAxis(40, WL_POINTER_AXIS_HORIZONTAL_SCROLL, 3.0);
	input.RecordPointerAxis(41, WL_POINTER_AXIS_HORIZONTAL_SCROLL, 4.0);
	input.RecordPointerAxisDiscrete(WL_POINTER_AXIS_HORIZONTAL_SCROLL, 2);
	input.RecordPointerAxis(42, WL_POINTER_AXIS_VERTICAL_SCROLL, 8.0);
	input.RecordPointerAxis(42, WL_POINTER_AXIS_VERTICAL_SCROLL, 2.0);
	input.RecordPointerFrame();

	std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 1);
	const auto &scroll = std::get<Scalpel::PointerInput>(events.front());
	CHECK(scroll.deltaX == 20.0);
	CHECK(scroll.deltaY == 10.0);
	CHECK(scroll.time == 42);

	input.RecordPointerAxisStop(43, WL_POINTER_AXIS_VERTICAL_SCROLL);
	input.RecordPointerAxisSource(WL_POINTER_AXIS_SOURCE_FINGER);
	input.RecordPointerFrame();
	CHECK(input.TakeInputs().empty());
	input.RecordPointerAxis(44, WL_POINTER_AXIS_VERTICAL_SCROLL, 6.0);
	input.RecordPointerFrame();
	events = input.TakeInputs();
	REQUIRE(events.size() == 1);
	CHECK(std::get<Scalpel::PointerInput>(events.front()).deltaY == 6.0);
}

TEST_CASE("Wayland pointer keeps legacy axis delivery immediate") {
	Scalpel::WaylandInput input;
	input.SetPointerVersion(4);
	input.RecordPointerAxis(50, WL_POINTER_AXIS_VERTICAL_SCROLL, 10.0);
	REQUIRE(input.TakeInputs().size() == 1);
	input.RecordPointerFrame();
	CHECK(input.TakeInputs().empty());
}

TEST_CASE("Wayland pointer teardown drops an unfinished axis frame") {
	Scalpel::WaylandInput input;
	input.SetPointerVersion(9);
	input.RecordPointerMotion(60, 20, 25);
	REQUIRE(input.TakeInputs().size() == 1);
	input.RecordPointerAxis(61, WL_POINTER_AXIS_VERTICAL_SCROLL, 10.0);
	input.ResetPointerDevice();
	const std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 1);
	CHECK(std::get<Scalpel::PointerInput>(events.front()).action ==
		Scalpel::PointerAction::Leave);
}

TEST_CASE("Wayland pointer teardown removes stale input and reports leave") {
	Scalpel::WaylandInput input;
	input.RecordPointerMotion(30, 12.5, 18.25);
	input.RecordPointerButton(31, BTN_LEFT, true);
	input.RecordPointerButton(32, BTN_RIGHT, true);
	REQUIRE(input.TakeInputs().size() == 3);
	input.RecordPointerAxis(33, WL_POINTER_AXIS_VERTICAL_SCROLL, 10.0);
	input.ResetPointerDevice();
	const std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 3);
	const auto &leftRelease = std::get<Scalpel::PointerInput>(events[0]);
	CHECK(leftRelease.action == Scalpel::PointerAction::Release);
	CHECK(leftRelease.button == 0);
	CHECK(leftRelease.x == 12.5);
	CHECK(leftRelease.y == 18.25);
	const auto &rightRelease = std::get<Scalpel::PointerInput>(events[1]);
	CHECK(rightRelease.action == Scalpel::PointerAction::Release);
	CHECK(rightRelease.button == 1);
	const auto &leave = std::get<Scalpel::PointerInput>(events[2]);
	CHECK(leave.action == Scalpel::PointerAction::Leave);
	CHECK(leave.x == 12.5);
	CHECK(leave.y == 18.25);

	input.ResetPointerDevice();
	CHECK(input.TakeInputs().empty());

	input.RecordPointerMotion(32, 20, 25);
	input.RecordPointerLeave();
	input.ResetPointerDevice();
	input.ResetPointerDevice();
	const std::vector<Scalpel::InputEvent> pendingLeave = input.TakeInputs();
	REQUIRE(pendingLeave.size() == 1);
	const auto &repeatedLeave = std::get<Scalpel::PointerInput>(pendingLeave[0]);
	CHECK(repeatedLeave.action == Scalpel::PointerAction::Leave);
	CHECK(repeatedLeave.x == 20);
	CHECK(repeatedLeave.y == 25);
}
