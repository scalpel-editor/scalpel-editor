#include "catch.hpp"

#include <type_traits>

#include "ApplicationEditor.h"
#include "LargeFileCard.h"

using Scalpel::CycleLargeFileCardFocus;
using Scalpel::HitTestLargeFileCard;
using Scalpel::LargeFileCardHit;
using Scalpel::LargeFileCardLayout;
using Scalpel::LargeFileCardPainter;
using Scalpel::LayoutLargeFileCard;
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

TEST_CASE("large file card layout fits inside the client") {
	const LargeFileCardLayout layout = LayoutLargeFileCard(800, 600);
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

TEST_CASE("large file card buttons are non-empty and disjoint") {
	const LargeFileCardLayout layout = LayoutLargeFileCard(800, 600);
	CHECK(NonEmpty(layout.openButton));
	CHECK(NonEmpty(layout.cancelButton));
	CHECK(Disjoint(layout.openButton, layout.cancelButton));
	CHECK(layout.card.Contains(layout.openButton));
	CHECK(layout.card.Contains(layout.cancelButton));
}

TEST_CASE("large file card hit-test hits button centers") {
	const LargeFileCardLayout layout = LayoutLargeFileCard(800, 600);
	CHECK(HitTestLargeFileCard(layout, Center(layout.openButton)) ==
		LargeFileCardHit::Open);
	CHECK(HitTestLargeFileCard(layout, Center(layout.cancelButton)) ==
		LargeFileCardHit::Cancel);
}

TEST_CASE("large file card hit-test misses scrim and gaps") {
	const LargeFileCardLayout layout = LayoutLargeFileCard(800, 600);
	CHECK(HitTestLargeFileCard(layout, Point(1, 1)) == LargeFileCardHit::None);
	CHECK(HitTestLargeFileCard(layout, Center(layout.title)) ==
		LargeFileCardHit::None);
	if (layout.openButton.right < layout.cancelButton.left) {
		const Point gap(
			(layout.openButton.right + layout.cancelButton.left) / 2.0,
			Center(layout.openButton).y);
		CHECK(HitTestLargeFileCard(layout, gap) == LargeFileCardHit::None);
	}
}

TEST_CASE("large file card layout on a narrow client still has buttons") {
	const LargeFileCardLayout layout = LayoutLargeFileCard(240, 400);
	CHECK(layout.card.left >= 0);
	CHECK(layout.card.right <= 240);
	CHECK(NonEmpty(layout.openButton));
	CHECK(NonEmpty(layout.cancelButton));
	CHECK(Disjoint(layout.openButton, layout.cancelButton));
}

TEST_CASE("large file card zero-size client yields empty layout") {
	const LargeFileCardLayout layout = LayoutLargeFileCard(0, 0);
	CHECK_FALSE(NonEmpty(layout.card));
	CHECK(HitTestLargeFileCard(layout, Point(0, 0)) == LargeFileCardHit::None);
}

TEST_CASE("large file card focus cycles through two buttons") {
	CHECK(CycleLargeFileCardFocus(0, 1) == 1);
	CHECK(CycleLargeFileCardFocus(1, 1) == 0);
	CHECK(CycleLargeFileCardFocus(0, -1) == 1);
	CHECK(CycleLargeFileCardFocus(1, -1) == 0);
}

TEST_CASE("large file card painter owns its immutable style") {
	const UiStyle source = Scalpel::DefaultUiStyle();
	LargeFileCardPainter painter(source);
	CHECK(&painter.Style() != &source);
	CHECK(painter.Style().cardPad == source.cardPad);
	CHECK(painter.Style().largeFileCardMaxWidth == source.largeFileCardMaxWidth);
}

TEST_CASE("large file card paint draws non-background pixels in the card") {
	Scalpel::ApplicationEditor editor(320, 200);
	editor.LoadInitialBuffer("paint probe\n");
	(void)editor.TakeFrameDamage();

	LargeFileCardPainter painter;
	const LargeFileCardLayout layout = LayoutLargeFileCard(320, 200);
	editor.SetOverlayPainter(
		[&](Scintilla::Internal::Surface &surface, int width, int height) {
			CHECK(width == 320);
			CHECK(height == 200);
			painter.Paint(surface, layout, "File larger than 64 MiB",
				"/tmp/large.bin", 0);
		});
	editor.RenderFrame({PRectangle::FromInts(0, 0, 320, 200)});

	const std::vector<uint8_t> pixels = editor.FramePixels();
	REQUIRE(pixels.size() == 320U * 200U * 4U);
	bool foundNonBackground = false;
	for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
		// Offscreen editor clears to white; card fill and ink differ.
		if (pixels[i] != 255 || pixels[i + 1] != 255 || pixels[i + 2] != 255) {
			foundNonBackground = true;
			break;
		}
	}
	CHECK(foundNonBackground);
}
