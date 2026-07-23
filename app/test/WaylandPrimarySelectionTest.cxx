#include <optional>
#include <string>

#include "catch.hpp"

#include "WaylandClipboard.h"
#include "WaylandPrimarySelection.h"

TEST_CASE("Wayland primary selection requires its own service seat and serial") {
	Scalpel::WaylandPrimarySelectionState selection;
	CHECK_FALSE(selection.Publish(std::string{"unavailable"}));
	CHECK(selection.ChoosePaste().kind ==
		Scalpel::WaylandPrimaryPasteChoice::Kind::Unavailable);

	selection.SetManagerAvailable(true);
	selection.SetSeatAvailable(true);
	CHECK_FALSE(selection.Publish(std::string{"no serial"}));
	selection.RecordSerial(51);
	REQUIRE(selection.Publish(std::string{"primary text"}));
	CHECK(selection.CanPaste());
	CHECK(selection.ChoosePaste().kind ==
		Scalpel::WaylandPrimaryPasteChoice::Kind::OwnedText);
	CHECK(selection.ChoosePaste().text == "primary text");

	REQUIRE(selection.Publish(std::nullopt));
	CHECK_FALSE(selection.CanPaste());
	CHECK(selection.ChoosePaste().kind ==
		Scalpel::WaylandPrimaryPasteChoice::Kind::NoText);

	selection.SetSeatAvailable(false);
	CHECK_FALSE(selection.Serial().has_value());
	CHECK_FALSE(selection.Publish(std::string{"stale serial"}));
}

TEST_CASE("Wayland primary selection replaces stale offers") {
	Scalpel::WaylandPrimarySelectionState selection;
	selection.SetManagerAvailable(true);
	selection.SetSeatAvailable(true);
	selection.AddOffer(10);
	selection.OfferMime(10, "text/plain");
	selection.AddOffer(11);
	selection.OfferMime(11, "UTF8_STRING");
	selection.OfferMime(11, "text/plain;charset=UTF-8");
	selection.SelectOffer(11);

	CHECK(selection.SelectionOffer() == 11);
	CHECK(selection.ChoosePaste().kind ==
		Scalpel::WaylandPrimaryPasteChoice::Kind::Receive);
	CHECK(selection.ChoosePaste().mimeType == "text/plain;charset=UTF-8");
	selection.RecordSerial(52);
	REQUIRE(selection.Publish(std::nullopt));
	CHECK(selection.SelectionOffer() == 11);

	selection.SelectOffer(10);
	CHECK_FALSE(selection.SelectionOffer().has_value());
	CHECK(selection.ChoosePaste().kind ==
		Scalpel::WaylandPrimaryPasteChoice::Kind::NoText);
	selection.SetManagerAvailable(false);
	CHECK(selection.ChoosePaste().kind ==
		Scalpel::WaylandPrimaryPasteChoice::Kind::Unavailable);
}

TEST_CASE("Wayland clipboard and primary ownership stay independent") {
	Scalpel::WaylandClipboardState clipboard;
	Scalpel::WaylandPrimarySelectionState primary;
	clipboard.SetManagerAvailable(true);
	clipboard.SetSeatAvailable(true);
	clipboard.RecordSerial(61);
	primary.SetManagerAvailable(true);
	primary.SetSeatAvailable(true);
	primary.RecordSerial(62);

	REQUIRE(clipboard.Publish("clipboard text"));
	REQUIRE(primary.Publish(std::string{"primary text"}));
	primary.CancelOwnership();
	CHECK(clipboard.ChoosePaste().text == "clipboard text");
	CHECK(primary.ChoosePaste().kind ==
		Scalpel::WaylandPrimaryPasteChoice::Kind::NoText);
}

TEST_CASE("Wayland primary selection reports unavailable operations") {
	Scalpel::WaylandPrimarySelection selection;
	selection.PublishText(17, std::string{"text"});
	selection.PasteText(18);
	const std::vector<Scalpel::PrimarySelectionResult> results =
		selection.TakeResults();
	REQUIRE(results.size() == 2);
	CHECK(results[0].request == 17);
	CHECK(results[0].operation ==
		Scalpel::PrimarySelectionOperation::Publish);
	CHECK(results[0].status ==
		Scalpel::PrimarySelectionResultStatus::Unavailable);
	CHECK(results[1].request == 18);
	CHECK(results[1].operation == Scalpel::PrimarySelectionOperation::Paste);
	CHECK(results[1].status ==
		Scalpel::PrimarySelectionResultStatus::Unavailable);
}
