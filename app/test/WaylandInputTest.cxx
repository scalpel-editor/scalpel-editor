#include <cstdlib>
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

TestKeymap MakeTestKeymap() {
	xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	REQUIRE(context);
	const xkb_rule_names names{nullptr, nullptr, "us", nullptr, nullptr};
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
