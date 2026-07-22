#include <cstdlib>
#include <string>

#include <linux/input-event-codes.h>
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
	REQUIRE(input.SetKeymap(keymap.text));

	input.RecordKey(15, KEY_A, true);
	input.RecordKey(16, KEY_A, false);
	const std::vector<Scalpel::KeyboardInput> events = input.TakeKeyboardInputs();
	REQUIRE(events.size() == 2);
	CHECK(events[0].key == static_cast<Scintilla::Keys>('A'));
	CHECK(events[0].modifiers == Scintilla::KeyMod::Norm);
	CHECK(events[0].text == "a");
	CHECK(events[0].time == 15);
	CHECK(events[0].pressed);
	CHECK(events[1].key == static_cast<Scintilla::Keys>('A'));
	CHECK(events[1].text.empty());
	CHECK_FALSE(events[1].pressed);
	CHECK(input.TakeKeyboardInputs().empty());
}

TEST_CASE("Wayland keyboard applies modifiers and maps command keys") {
	const TestKeymap keymap = MakeTestKeymap();
	Scalpel::WaylandInput input;
	REQUIRE(input.SetKeymap(keymap.text));

	input.UpdateModifiers(keymap.shiftMask, 0, 0, 0);
	input.RecordKey(20, KEY_A, true);
	input.UpdateModifiers(keymap.controlMask, 0, 0, 0);
	input.RecordKey(21, KEY_LEFT, true);
	const std::vector<Scalpel::KeyboardInput> events = input.TakeKeyboardInputs();
	REQUIRE(events.size() == 2);
	CHECK(events[0].key == static_cast<Scintilla::Keys>('A'));
	CHECK(events[0].modifiers == Scintilla::KeyMod::Shift);
	CHECK(events[0].text == "A");
	CHECK(events[1].key == Scintilla::Keys::Left);
	CHECK(events[1].modifiers == Scintilla::KeyMod::Ctrl);
	CHECK(events[1].text.empty());
}
