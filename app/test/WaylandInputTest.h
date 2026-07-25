#pragma once

#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include <linux/input-event-codes.h>
#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon.h>

#include "catch.hpp"

#include "WaylandInput.h"

namespace {

struct TestKeymap {
	std::string text;
	uint32_t shiftMask = 0;
	uint32_t controlMask = 0;
	uint32_t altMask = 0;
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
	const xkb_mod_index_t alt = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_ALT);
	REQUIRE(shift != XKB_MOD_INVALID);
	REQUIRE(control != XKB_MOD_INVALID);
	REQUIRE(alt != XKB_MOD_INVALID);
	result.shiftMask = uint32_t{1} << shift;
	result.controlMask = uint32_t{1} << control;
	result.altMask = uint32_t{1} << alt;
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	return result;
}

}
