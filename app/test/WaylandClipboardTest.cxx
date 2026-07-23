#include <string>

#include "catch.hpp"

#include "WaylandClipboard.h"

TEST_CASE("Wayland clipboard state prefers explicit UTF-8 offers") {
	Scalpel::WaylandClipboardState clipboard;
	clipboard.SetManagerAvailable(true);
	clipboard.SetSeatAvailable(true);
	clipboard.AddOffer(10);
	clipboard.OfferMime(10, "image/png");
	clipboard.OfferMime(10, "text/plain");
	clipboard.OfferMime(10, "UTF8_STRING");
	clipboard.OfferMime(10, "text/plain;charset=UTF-8");
	clipboard.SelectOffer(10);

	const Scalpel::WaylandClipboardPasteChoice choice = clipboard.ChoosePaste();
	CHECK(choice.kind == Scalpel::WaylandClipboardPasteChoice::Kind::Receive);
	CHECK(choice.mimeType == "text/plain;charset=UTF-8");
	CHECK(clipboard.CanPaste());

	clipboard.AddOffer(11);
	clipboard.SelectOffer(11);
	CHECK_FALSE(clipboard.CanPaste());
	CHECK(clipboard.ChoosePaste().kind ==
		Scalpel::WaylandClipboardPasteChoice::Kind::NoText);
}

TEST_CASE("Wayland clipboard state requires a service seat and serial to publish") {
	Scalpel::WaylandClipboardState clipboard;
	CHECK_FALSE(clipboard.Publish("unavailable"));
	clipboard.SetManagerAvailable(true);
	CHECK_FALSE(clipboard.Publish("no seat"));
	clipboard.SetSeatAvailable(true);
	CHECK_FALSE(clipboard.Publish("no serial"));
	clipboard.RecordSerial(42);
	REQUIRE(clipboard.Publish("owned text"));
	CHECK(clipboard.CanPaste());
	CHECK(clipboard.ChoosePaste().text == "owned text");

	clipboard.AddOffer(10);
	clipboard.OfferMime(10, "text/plain");
	clipboard.SelectOffer(10);
	CHECK(clipboard.ChoosePaste().kind ==
		Scalpel::WaylandClipboardPasteChoice::Kind::OwnedText);
	CHECK(clipboard.ChoosePaste().text == "owned text");

	clipboard.CancelOwnership();
	CHECK(clipboard.ChoosePaste().kind ==
		Scalpel::WaylandClipboardPasteChoice::Kind::Receive);
	CHECK(clipboard.ChoosePaste().mimeType == "text/plain");

	clipboard.SetSeatAvailable(false);
	CHECK_FALSE(clipboard.CanPaste());
	CHECK_FALSE(clipboard.Serial().has_value());
	clipboard.SetSeatAvailable(true);
	CHECK_FALSE(clipboard.Publish("stale serial"));
}

TEST_CASE("Wayland clipboard validates complete UTF-8 text") {
	CHECK(Scalpel::IsValidWaylandText(""));
	CHECK(Scalpel::IsValidWaylandText("plain"));
	CHECK(Scalpel::IsValidWaylandText("\xE2\x98\x83"));
	CHECK_FALSE(Scalpel::IsValidWaylandText("\xC0\xAF"));
	CHECK_FALSE(Scalpel::IsValidWaylandText("\xE2\x98"));
	CHECK_FALSE(Scalpel::IsValidWaylandText("\xED\xA0\x80"));
}

TEST_CASE("Wayland clipboard reports unavailable operations without protocol objects") {
	Scalpel::WaylandClipboard clipboard;
	clipboard.CopyText(7, "text");
	clipboard.PasteText(8);
	const std::vector<Scalpel::ClipboardResult> results = clipboard.TakeResults();
	REQUIRE(results.size() == 2);
	CHECK(results[0].request == 7);
	CHECK(results[0].status == Scalpel::ClipboardResultStatus::Unavailable);
	CHECK(results[1].request == 8);
	CHECK(results[1].status == Scalpel::ClipboardResultStatus::Unavailable);
}
