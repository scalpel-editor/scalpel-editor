#include "catch.hpp"

#include "ApplicationInput.h"
#include "EmojiCatalog.h"
#include "EmojiCompletion.h"

using Scalpel::CloseEmojiCompletion;
using Scalpel::EmojiCompletionHit;
using Scalpel::EmojiCompletionLayout;
using Scalpel::EmojiCompletionModel;
using Scalpel::EmojiMatch;
using Scalpel::HandleEmojiCompletionKeyboard;
using Scalpel::HandleEmojiCompletionPointer;
using Scalpel::HitTestEmojiCompletion;
using Scalpel::KeyboardInput;
using Scalpel::LayoutEmojiCompletion;
using Scalpel::MatchEmojiPrefix;
using Scalpel::PointerAction;
using Scalpel::PointerInput;
using Scintilla::Internal::PRectangle;
using Scintilla::Internal::Point;
using Scintilla::KeyMod;
using Scintilla::Keys;

namespace {

PointerInput MakePointer(PointerAction action, double x, double y,
	int button = -1) {
	PointerInput input;
	input.action = action;
	input.x = x;
	input.y = y;
	input.button = button;
	return input;
}

KeyboardInput MakeKey(Keys key, KeyMod modifiers = KeyMod::Norm) {
	KeyboardInput input;
	input.key = key;
	input.modifiers = modifiers;
	input.pressed = true;
	return input;
}

void OpenWith(EmojiCompletionModel &model, std::string_view query) {
	model.open = true;
	model.matches = MatchEmojiPrefix(query);
	REQUIRE_FALSE(model.matches.empty());
	if (model.matches.size() > 8) {
		model.matches.resize(8);
	}
	model.selected = 0;
	model.colonPos = 0;
}

}

TEST_CASE("emoji completion layout sits below the caret inside the client") {
	EmojiCompletionModel model;
	OpenWith(model, "thumb");
	const PRectangle client = PRectangle::FromInts(10, 20, 400, 300);
	const EmojiCompletionLayout layout =
		LayoutEmojiCompletion(model, 40, 80, 18, client);
	REQUIRE(layout.panel.Width() > 0);
	REQUIRE(layout.panel.Height() > 0);
	CHECK(layout.panel.top >= 80 + 18);
	CHECK(layout.panel.left >= client.left);
	CHECK(layout.panel.right <= client.right);
	CHECK(layout.panel.bottom <= client.bottom);
	CHECK_FALSE(layout.aboveCaret);
	REQUIRE(layout.items.size() >= 2);
	bool sawThumbsUp = false;
	for (const auto &item : layout.items) {
		if (item.label.find("👍") != std::string::npos) {
			sawThumbsUp = true;
		}
	}
	CHECK(sawThumbsUp);
}

TEST_CASE("emoji completion layout flips above the caret near the bottom") {
	EmojiCompletionModel model;
	OpenWith(model, "thumb");
	const PRectangle client = PRectangle::FromInts(0, 0, 400, 120);
	const EmojiCompletionLayout layout =
		LayoutEmojiCompletion(model, 20, 90, 18, client);
	CHECK(layout.aboveCaret);
	CHECK(layout.panel.bottom <= 90);
	CHECK(layout.panel.top >= client.top);
}

TEST_CASE("emoji completion layout uses the roomier side when neither side fits") {
	EmojiCompletionModel model;
	OpenWith(model, "");
	const PRectangle client = PRectangle::FromInts(0, 0, 400, 120);
	const EmojiCompletionLayout layout =
		LayoutEmojiCompletion(model, 20, 80, 18, client);
	CHECK(layout.aboveCaret);
	CHECK(layout.panel.bottom <= 80);
	CHECK(layout.items.size() > 1);
}

TEST_CASE("emoji completion keyboard moves, completes, and dismisses") {
	EmojiCompletionModel model;
	OpenWith(model, "thumb");
	REQUIRE(model.matches.size() >= 2);
	const EmojiMatch first = model.matches[0];
	const EmojiMatch second = model.matches[1];

	auto down = HandleEmojiCompletionKeyboard(model, MakeKey(Keys::Down));
	CHECK(down.consumed);
	CHECK(model.selected == 1);
	CHECK_FALSE(down.completed.has_value());

	auto complete = HandleEmojiCompletionKeyboard(model, MakeKey(Keys::Return));
	CHECK(complete.consumed);
	REQUIRE(complete.completed.has_value());
	CHECK(complete.completed->emoji == second.emoji);
	CHECK(model.open);

	auto tab = HandleEmojiCompletionKeyboard(model, MakeKey(Keys::Tab));
	CHECK(tab.consumed);
	REQUIRE(tab.completed.has_value());
	CHECK(tab.completed->emoji == second.emoji);

	model.selected = 0;
	auto escape = HandleEmojiCompletionKeyboard(model, MakeKey(Keys::Escape));
	CHECK(escape.consumed);
	CHECK(escape.dismissed);
	CHECK_FALSE(model.open);
	CHECK(first.emoji.size() > 0);
}

TEST_CASE("emoji completion pointer click-through dismisses outside the list") {
	EmojiCompletionModel model;
	OpenWith(model, "+1");
	const PRectangle client = PRectangle::FromInts(0, 0, 400, 300);
	const EmojiCompletionLayout layout =
		LayoutEmojiCompletion(model, 20, 40, 18, client);
	REQUIRE(layout.items.size() == 1);
	const Point onRow(
		(layout.items[0].row.left + layout.items[0].row.right) / 2.0,
		(layout.items[0].row.top + layout.items[0].row.bottom) / 2.0);

	auto press = HandleEmojiCompletionPointer(model, layout,
		MakePointer(PointerAction::Press, onRow.x, onRow.y, 0));
	CHECK(press.consumed);
	auto release = HandleEmojiCompletionPointer(model, layout,
		MakePointer(PointerAction::Release, onRow.x, onRow.y, 0));
	CHECK(release.consumed);
	REQUIRE(release.completed.has_value());
	CHECK(release.completed->emoji == "👍");

	OpenWith(model, "+1");
	auto outside = HandleEmojiCompletionPointer(model, layout,
		MakePointer(PointerAction::Press, 390, 290, 0));
	CHECK_FALSE(outside.consumed);
	CHECK(outside.dismissed);
	CHECK_FALSE(model.open);
}

TEST_CASE("closed emoji completion has an empty layout") {
	EmojiCompletionModel model;
	const PRectangle client = PRectangle::FromInts(0, 0, 400, 300);
	const EmojiCompletionLayout layout =
		LayoutEmojiCompletion(model, 20, 40, 18, client);
	CHECK(layout.panel.Width() == 0);
	CHECK(layout.items.empty());
	CloseEmojiCompletion(model);
}
