#include "catch.hpp"

#include "ApplicationEditor.h"
#include "ExternalChangeCard.h"

using Scalpel::CycleExternalChangeCardFocus;
using Scalpel::ExternalChangeCardHit;
using Scalpel::ExternalChangeCardLayout;
using Scalpel::ExternalChangeCardPainter;
using Scalpel::HitTestExternalChangeCard;
using Scalpel::LayoutExternalChangeCard;
using Scalpel::UiStyle;
using Scintilla::Internal::Point;
using Scintilla::Internal::PRectangle;

namespace {

bool NonEmpty(const PRectangle &rc) {
	return rc.right > rc.left && rc.bottom > rc.top;
}

bool Disjoint(const PRectangle &a, const PRectangle &b) {
	return !a.Intersects(b);
}

Point Center(const PRectangle &rc) {
	return Point((rc.left + rc.right) / 2.0, (rc.top + rc.bottom) / 2.0);
}

}

TEST_CASE("external change card layout fits inside the client") {
	const ExternalChangeCardLayout layout = LayoutExternalChangeCard(800, 600);
	CHECK(layout.scrim.left == 0);
	CHECK(layout.scrim.top == 0);
	CHECK(layout.scrim.right == 800);
	CHECK(layout.scrim.bottom == 600);

	CHECK(layout.card.left >= 0);
	CHECK(layout.card.top >= 0);
	CHECK(layout.card.right <= 800);
	CHECK(layout.card.bottom <= 600);
	CHECK(NonEmpty(layout.card));
	CHECK(layout.scrim.Contains(layout.card));
}

TEST_CASE("external change card buttons are non-empty and disjoint") {
	const ExternalChangeCardLayout layout = LayoutExternalChangeCard(800, 600);
	CHECK(NonEmpty(layout.overwriteButton));
	CHECK(NonEmpty(layout.reloadButton));
	CHECK(NonEmpty(layout.saveAsButton));
	CHECK(NonEmpty(layout.cancelButton));
	CHECK(Disjoint(layout.overwriteButton, layout.reloadButton));
	CHECK(Disjoint(layout.reloadButton, layout.saveAsButton));
	CHECK(Disjoint(layout.saveAsButton, layout.cancelButton));
	CHECK(layout.card.Contains(layout.overwriteButton));
	CHECK(layout.card.Contains(layout.cancelButton));
}

TEST_CASE("external change card hit-test hits button centers") {
	const ExternalChangeCardLayout layout = LayoutExternalChangeCard(800, 600);
	CHECK(HitTestExternalChangeCard(layout, Center(layout.overwriteButton)) ==
		ExternalChangeCardHit::Overwrite);
	CHECK(HitTestExternalChangeCard(layout, Center(layout.reloadButton)) ==
		ExternalChangeCardHit::Reload);
	CHECK(HitTestExternalChangeCard(layout, Center(layout.saveAsButton)) ==
		ExternalChangeCardHit::SaveAs);
	CHECK(HitTestExternalChangeCard(layout, Center(layout.cancelButton)) ==
		ExternalChangeCardHit::Cancel);
}

TEST_CASE("external change card hit-test misses scrim and gaps") {
	const ExternalChangeCardLayout layout = LayoutExternalChangeCard(800, 600);
	CHECK(HitTestExternalChangeCard(layout, Point(1, 1)) ==
		ExternalChangeCardHit::None);
	CHECK(HitTestExternalChangeCard(layout, Center(layout.title)) ==
		ExternalChangeCardHit::None);
	if (layout.overwriteButton.right < layout.reloadButton.left) {
		const Point gap(
			(layout.overwriteButton.right + layout.reloadButton.left) / 2.0,
			Center(layout.overwriteButton).y);
		CHECK(HitTestExternalChangeCard(layout, gap) ==
			ExternalChangeCardHit::None);
	}
}

TEST_CASE("external change card layout on a narrow client still has buttons") {
	const ExternalChangeCardLayout layout = LayoutExternalChangeCard(320, 400);
	CHECK(layout.card.left >= 0);
	CHECK(layout.card.right <= 320);
	CHECK(NonEmpty(layout.overwriteButton));
	CHECK(NonEmpty(layout.cancelButton));
	CHECK(Disjoint(layout.overwriteButton, layout.cancelButton));
}

TEST_CASE("external change card short client keeps buttons inside the card") {
	const ExternalChangeCardLayout layout = LayoutExternalChangeCard(800, 100);
	CHECK(layout.card.top >= 0);
	CHECK(layout.card.bottom <= 100);
	CHECK(NonEmpty(layout.overwriteButton));
	CHECK(NonEmpty(layout.cancelButton));
	CHECK(layout.card.Contains(layout.overwriteButton));
	CHECK(layout.card.Contains(layout.cancelButton));
	CHECK(HitTestExternalChangeCard(layout, Center(layout.overwriteButton)) ==
		ExternalChangeCardHit::Overwrite);
}

TEST_CASE("external change card tiny client keeps layout bounds inside the card") {
	const ExternalChangeCardLayout layout = LayoutExternalChangeCard(1, 1);
	CHECK(layout.card.left >= 0);
	CHECK(layout.card.top >= 0);
	CHECK(layout.card.right <= 1);
	CHECK(layout.card.bottom <= 1);
	CHECK(layout.card.Contains(layout.title));
	CHECK(layout.card.Contains(layout.path));
	CHECK(layout.card.Contains(layout.overwriteButton));
	CHECK(layout.card.Contains(layout.cancelButton));
}

TEST_CASE("external change card zero-size client yields empty layout") {
	const ExternalChangeCardLayout layout = LayoutExternalChangeCard(0, 0);
	CHECK_FALSE(NonEmpty(layout.card));
	CHECK(HitTestExternalChangeCard(layout, Point(0, 0)) ==
		ExternalChangeCardHit::None);
}

TEST_CASE("external change card focus cycles through four buttons") {
	CHECK(CycleExternalChangeCardFocus(0, 1) == 1);
	CHECK(CycleExternalChangeCardFocus(1, 1) == 2);
	CHECK(CycleExternalChangeCardFocus(2, 1) == 3);
	CHECK(CycleExternalChangeCardFocus(3, 1) == 0);
	CHECK(CycleExternalChangeCardFocus(0, -1) == 3);
	CHECK(CycleExternalChangeCardFocus(3, -1) == 2);
}

TEST_CASE("external change card painter owns its immutable style") {
	const UiStyle source = Scalpel::DefaultUiStyle();
	ExternalChangeCardPainter painter(source);
	CHECK(&painter.Style() != &source);
	CHECK(painter.Style().cardPad == source.cardPad);
	CHECK(painter.Style().externalChangeCardMaxWidth ==
		source.externalChangeCardMaxWidth);
}

TEST_CASE("external change card paint draws non-background pixels in the card") {
	Scalpel::ApplicationEditor editor(320, 200);
	editor.LoadInitialBuffer("paint probe\n");
	(void)editor.TakeFrameDamage();

	ExternalChangeCardPainter painter;
	const ExternalChangeCardLayout layout = LayoutExternalChangeCard(320, 200);
	editor.SetOverlayPainter(
		[&](Scintilla::Internal::Surface &surface, int width, int height) {
			CHECK(width == 320);
			CHECK(height == 200);
			painter.Paint(surface, layout, "File changed on disk",
				"/tmp/note.txt", 0);
		});
	editor.RenderFrame({PRectangle::FromInts(0, 0, 320, 200)});

	const std::vector<uint8_t> pixels = editor.FramePixels();
	REQUIRE(pixels.size() == 320U * 200U * 4U);
	bool foundNonBackground = false;
	for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
		if (pixels[i] != 255 || pixels[i + 1] != 255 || pixels[i + 2] != 255) {
			foundNonBackground = true;
			break;
		}
	}
	CHECK(foundNonBackground);
}
