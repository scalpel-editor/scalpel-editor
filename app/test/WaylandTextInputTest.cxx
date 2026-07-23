#include <string>

#include "catch.hpp"

#include "WaylandTextInput.h"

namespace {

Scalpel::WaylandTextInputClientState TextInputClientState() {
	return {"before \xC3\xA9 after", 9, 9, {12, 18, 1, 16},
		Scalpel::WaylandTextChangeCause::Other};
}

}

TEST_CASE("Wayland text input enables only with focus entry and editor state") {
	Scalpel::WaylandTextInputState input;

	CHECK(input.Attach().empty());
	CHECK(input.SetKeyboardFocus(true).empty());
	CHECK(input.Enter().empty());
	const auto enabled = input.UpdateClientState(TextInputClientState());
	REQUIRE(enabled.size() == 3);
	CHECK(enabled[0].type == Scalpel::WaylandTextInputRequestType::Enable);
	CHECK(enabled[1].type == Scalpel::WaylandTextInputRequestType::State);
	REQUIRE(enabled[1].state.has_value());
	CHECK(enabled[1].state->surroundingText ==
		std::optional<std::string>{"before \xC3\xA9 after"});
	CHECK(enabled[2].type == Scalpel::WaylandTextInputRequestType::Commit);
	CHECK(input.CommitSerial() == 1);
	CHECK(input.Enabled());

	const auto disabled = input.SetKeyboardFocus(false);
	REQUIRE(disabled.size() == 2);
	CHECK(disabled[0].type == Scalpel::WaylandTextInputRequestType::Disable);
	CHECK(disabled[1].type == Scalpel::WaylandTextInputRequestType::Commit);
	CHECK(input.CommitSerial() == 2);
	CHECK_FALSE(input.Enabled());
	REQUIRE(input.TakeBatches().size() == 1);
	CHECK(input.TakeBatches().empty());
}

TEST_CASE("Wayland text input batches events at done in protocol order") {
	Scalpel::WaylandTextInputState input;
	(void)input.Attach();
	(void)input.UpdateClientState(TextInputClientState());
	(void)input.SetKeyboardFocus(true);
	REQUIRE(input.Enter().size() == 3);

	input.RecordPreedit("first", 5, 5);
	input.RecordPreedit("\xE6\x96\x87", 3, 3);
	input.RecordCommit("\xE7\xB5\x90");
	input.RecordDelete(2, 4);
	input.Done(0);
	auto batches = input.TakeBatches();
	REQUIRE(batches.size() == 1);
	REQUIRE(batches[0].preedit.has_value());
	CHECK(batches[0].preedit->text == "\xE6\x96\x87");
	CHECK(batches[0].preedit->cursorBegin == 3);
	REQUIRE(batches[0].commit.has_value());
	CHECK(*batches[0].commit == "\xE7\xB5\x90");
	REQUIRE(batches[0].deletion.has_value());
	CHECK(batches[0].deletion->beforeLength == 2);
	CHECK(batches[0].deletion->afterLength == 4);
	CHECK_FALSE(batches[0].refreshState);
	CHECK_FALSE(batches[0].cancel);

	auto changed = TextInputClientState();
	changed.cursorRectangle.x++;
	CHECK(input.UpdateClientState(changed).empty());
	input.Done(1);
	batches = input.TakeBatches();
	REQUIRE(batches.size() == 1);
	CHECK(batches[0].refreshState);
	const auto refreshed = input.UpdateClientState(changed);
	REQUIRE(refreshed.size() == 2);
	CHECK(refreshed[0].type == Scalpel::WaylandTextInputRequestType::State);
	CHECK(refreshed[1].type == Scalpel::WaylandTextInputRequestType::Commit);
	CHECK(input.CommitSerial() == 2);
}

TEST_CASE("Wayland text input quiesces after an unchanged acknowledgement") {
	Scalpel::WaylandTextInputState input;
	(void)input.Attach();
	(void)input.UpdateClientState(TextInputClientState());
	(void)input.SetKeyboardFocus(true);
	REQUIRE(input.Enter().size() == 3);

	input.Done(1);
	auto batches = input.TakeBatches();
	REQUIRE(batches.size() == 1);
	CHECK_FALSE(batches[0].refreshState);
	CHECK(input.UpdateClientState(TextInputClientState()).empty());
	CHECK(input.CommitSerial() == 1);

	input.RecordPreedit("compose", 7, 7);
	input.Done(1);
	batches = input.TakeBatches();
	REQUIRE(batches.size() == 1);
	CHECK(batches[0].refreshState);
}

TEST_CASE("Wayland text input resets changing surrounding support") {
	Scalpel::WaylandTextInputState input;
	(void)input.Attach();
	(void)input.SetKeyboardFocus(true);
	(void)input.Enter();
	REQUIRE(input.UpdateClientState(TextInputClientState()).size() == 3);

	auto unavailable = TextInputClientState();
	unavailable.surroundingText.reset();
	const auto reset = input.UpdateClientState(unavailable);
	REQUIRE(reset.size() == 3);
	CHECK(reset[0].type == Scalpel::WaylandTextInputRequestType::Enable);
	CHECK(reset[1].type == Scalpel::WaylandTextInputRequestType::State);
	REQUIRE(reset[1].state.has_value());
	CHECK_FALSE(reset[1].state->surroundingText.has_value());
	CHECK(reset[2].type == Scalpel::WaylandTextInputRequestType::Commit);
}

TEST_CASE("Wayland text input rejects invalid UTF-8 state and events") {
	Scalpel::WaylandTextInputState input;
	(void)input.Attach();

	auto invalid = TextInputClientState();
	invalid.cursor = 8;
	CHECK_THROWS_WITH(input.UpdateClientState(invalid),
		"invalid Wayland text input client state");
	invalid = TextInputClientState();
	invalid.surroundingText = std::string(4001, 'a');
	invalid.cursor = 4001;
	invalid.anchor = 4001;
	CHECK_THROWS_WITH(input.UpdateClientState(invalid),
		"invalid Wayland text input client state");

	(void)input.UpdateClientState(TextInputClientState());
	(void)input.SetKeyboardFocus(true);
	(void)input.Enter();
	input.RecordPreedit("\xC3\xA9", 1, 1);
	input.RecordCommit("\x80");
	input.Done(1);
	const auto batches = input.TakeBatches();
	REQUIRE(batches.size() == 1);
	CHECK(batches[0].cancel);
	CHECK_FALSE(batches[0].preedit.has_value());
	CHECK_FALSE(batches[0].commit.has_value());
}

TEST_CASE("Wayland text input cancels batches on leave and detach") {
	Scalpel::WaylandTextInputState input;
	(void)input.Attach();
	(void)input.UpdateClientState(TextInputClientState());
	(void)input.SetKeyboardFocus(true);
	(void)input.Enter();
	input.RecordPreedit("compose", 7, 7);
	input.Leave();
	auto batches = input.TakeBatches();
	REQUIRE(batches.size() == 1);
	CHECK(batches[0].cancel);

	(void)input.Enter();
	input.RecordCommit("stale");
	input.Detach();
	batches = input.TakeBatches();
	REQUIRE(batches.size() == 1);
	CHECK(batches[0].cancel);
	CHECK(input.SetKeyboardFocus(false).empty());
}
