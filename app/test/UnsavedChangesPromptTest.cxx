#include "catch.hpp"

#include "UnsavedChangesPrompt.h"

using Scalpel::UnsavedChangesPrompt;
using Scalpel::UnsavedChoice;
using Scalpel::UnsavedOutcome;
using Scalpel::UnsavedPending;

TEST_CASE("UnsavedChangesPrompt begins Close when inactive") {
	UnsavedChangesPrompt prompt;
	CHECK_FALSE(prompt.Active());
	CHECK(prompt.Pending() == UnsavedPending::None);

	CHECK(prompt.TryBegin(UnsavedPending::Close));
	CHECK(prompt.Active());
	CHECK(prompt.Pending() == UnsavedPending::Close);
	CHECK_FALSE(prompt.AwaitingSaveAs());
}

TEST_CASE("UnsavedChangesPrompt ignores second Begin while active") {
	UnsavedChangesPrompt prompt;
	REQUIRE(prompt.TryBegin(UnsavedPending::Close));
	CHECK_FALSE(prompt.TryBegin(UnsavedPending::Close));
	CHECK(prompt.Pending() == UnsavedPending::Close);
}

TEST_CASE("UnsavedChangesPrompt Cancel dismisses") {
	UnsavedChangesPrompt prompt;
	REQUIRE(prompt.TryBegin(UnsavedPending::Close));
	CHECK(prompt.Choose(UnsavedChoice::Cancel, true) == UnsavedOutcome::Dismissed);
	CHECK_FALSE(prompt.Active());
	CHECK(prompt.Pending() == UnsavedPending::None);
}

TEST_CASE("UnsavedChangesPrompt Discard with Close performs close") {
	UnsavedChangesPrompt prompt;
	REQUIRE(prompt.TryBegin(UnsavedPending::Close));
	CHECK(prompt.Choose(UnsavedChoice::Discard, false) ==
		UnsavedOutcome::PerformClose);
	CHECK_FALSE(prompt.Active());
}

TEST_CASE("UnsavedChangesPrompt Save with path performs pending") {
	UnsavedChangesPrompt prompt;
	REQUIRE(prompt.TryBegin(UnsavedPending::Close));
	CHECK(prompt.Choose(UnsavedChoice::Save, true) ==
		UnsavedOutcome::PerformClose);
	CHECK_FALSE(prompt.Active());
}

TEST_CASE("UnsavedChangesPrompt Save without path needs Save As") {
	UnsavedChangesPrompt prompt;
	REQUIRE(prompt.TryBegin(UnsavedPending::Close));
	CHECK(prompt.Choose(UnsavedChoice::Save, false) == UnsavedOutcome::NeedSaveAs);
	CHECK(prompt.Active());
	CHECK(prompt.Pending() == UnsavedPending::Close);
	CHECK(prompt.AwaitingSaveAs());
}

TEST_CASE("UnsavedChangesPrompt NotifySaved after NeedSaveAs performs pending") {
	UnsavedChangesPrompt prompt;
	REQUIRE(prompt.TryBegin(UnsavedPending::Close));
	REQUIRE(prompt.Choose(UnsavedChoice::Save, false) ==
		UnsavedOutcome::NeedSaveAs);
	CHECK(prompt.NotifySaved() == UnsavedOutcome::PerformClose);
	CHECK_FALSE(prompt.Active());
	CHECK_FALSE(prompt.AwaitingSaveAs());
}

TEST_CASE("UnsavedChangesPrompt NotifySaveIncomplete keeps pending") {
	UnsavedChangesPrompt prompt;
	REQUIRE(prompt.TryBegin(UnsavedPending::Close));
	REQUIRE(prompt.Choose(UnsavedChoice::Save, false) ==
		UnsavedOutcome::NeedSaveAs);
	REQUIRE(prompt.AwaitingSaveAs());

	prompt.NotifySaveIncomplete();
	CHECK(prompt.Active());
	CHECK(prompt.Pending() == UnsavedPending::Close);
	CHECK_FALSE(prompt.AwaitingSaveAs());
}

TEST_CASE("UnsavedChangesPrompt Cancel while awaiting Save As dismisses") {
	UnsavedChangesPrompt prompt;
	REQUIRE(prompt.TryBegin(UnsavedPending::Close));
	REQUIRE(prompt.Choose(UnsavedChoice::Save, false) ==
		UnsavedOutcome::NeedSaveAs);
	CHECK(prompt.Choose(UnsavedChoice::Cancel, false) == UnsavedOutcome::Dismissed);
	CHECK_FALSE(prompt.Active());
	CHECK_FALSE(prompt.AwaitingSaveAs());
}

TEST_CASE("UnsavedChangesPrompt inactive Choose and Notify are no-ops") {
	UnsavedChangesPrompt prompt;
	CHECK(prompt.Choose(UnsavedChoice::Save, true) == UnsavedOutcome::None);
	CHECK(prompt.Choose(UnsavedChoice::Discard, true) == UnsavedOutcome::None);
	CHECK(prompt.Choose(UnsavedChoice::Cancel, true) == UnsavedOutcome::None);
	CHECK(prompt.NotifySaved() == UnsavedOutcome::None);
	prompt.NotifySaveIncomplete();
	CHECK_FALSE(prompt.Active());
}

TEST_CASE("UnsavedChangesPrompt Dismiss clears active state") {
	UnsavedChangesPrompt prompt;
	REQUIRE(prompt.TryBegin(UnsavedPending::Close));
	REQUIRE(prompt.Choose(UnsavedChoice::Save, false) ==
		UnsavedOutcome::NeedSaveAs);
	prompt.Dismiss();
	CHECK_FALSE(prompt.Active());
	CHECK_FALSE(prompt.AwaitingSaveAs());
	CHECK(prompt.Pending() == UnsavedPending::None);
}
