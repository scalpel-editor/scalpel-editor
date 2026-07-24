#include "catch.hpp"

#include "ApplicationEditor.h"
#include "UnsavedChangesCard.h"

using Scalpel::HitTestUnsavedChangesCard;
using Scalpel::LayoutUnsavedChangesCard;
using Scalpel::UnsavedCardHit;
using Scalpel::UnsavedChangesCardLayout;
using Scalpel::UnsavedChangesCardPainter;
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

TEST_CASE("UnsavedChangesCard layout fits inside the client") {
	const UnsavedChangesCardLayout layout = LayoutUnsavedChangesCard(800, 600);
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

TEST_CASE("UnsavedChangesCard buttons are non-empty and disjoint") {
	const UnsavedChangesCardLayout layout = LayoutUnsavedChangesCard(800, 600);
	CHECK(NonEmpty(layout.saveButton));
	CHECK(NonEmpty(layout.discardButton));
	CHECK(NonEmpty(layout.cancelButton));
	CHECK(Disjoint(layout.saveButton, layout.discardButton));
	CHECK(Disjoint(layout.discardButton, layout.cancelButton));
	CHECK(Disjoint(layout.saveButton, layout.cancelButton));
	CHECK(layout.card.Contains(layout.saveButton));
	CHECK(layout.card.Contains(layout.discardButton));
	CHECK(layout.card.Contains(layout.cancelButton));
}

TEST_CASE("UnsavedChangesCard hit-test hits button centers") {
	const UnsavedChangesCardLayout layout = LayoutUnsavedChangesCard(800, 600);
	CHECK(HitTestUnsavedChangesCard(layout, Center(layout.saveButton)) ==
		UnsavedCardHit::Save);
	CHECK(HitTestUnsavedChangesCard(layout, Center(layout.discardButton)) ==
		UnsavedCardHit::Discard);
	CHECK(HitTestUnsavedChangesCard(layout, Center(layout.cancelButton)) ==
		UnsavedCardHit::Cancel);
}

TEST_CASE("UnsavedChangesCard hit-test misses scrim and gaps") {
	const UnsavedChangesCardLayout layout = LayoutUnsavedChangesCard(800, 600);
	// Corner of the scrim, outside the card.
	CHECK(HitTestUnsavedChangesCard(layout, Point(1, 1)) == UnsavedCardHit::None);
	// Center of the card title region (not a button).
	CHECK(HitTestUnsavedChangesCard(layout, Center(layout.title)) ==
		UnsavedCardHit::None);
	// Gap between Save and Discard when there is a gap.
	if (layout.saveButton.right < layout.discardButton.left) {
		const Point gap(
			(layout.saveButton.right + layout.discardButton.left) / 2.0,
			Center(layout.saveButton).y);
		CHECK(HitTestUnsavedChangesCard(layout, gap) == UnsavedCardHit::None);
	}
}

TEST_CASE("UnsavedChangesCard layout on a narrow client still has buttons") {
	const UnsavedChangesCardLayout layout = LayoutUnsavedChangesCard(240, 400);
	CHECK(layout.card.left >= 0);
	CHECK(layout.card.right <= 240);
	CHECK(NonEmpty(layout.saveButton));
	CHECK(NonEmpty(layout.discardButton));
	CHECK(NonEmpty(layout.cancelButton));
	CHECK(Disjoint(layout.saveButton, layout.discardButton));
	CHECK(Disjoint(layout.discardButton, layout.cancelButton));
}

TEST_CASE("UnsavedChangesCard layout on a short client still has buttons") {
	const UnsavedChangesCardLayout layout = LayoutUnsavedChangesCard(800, 100);
	CHECK(NonEmpty(layout.saveButton));
	CHECK(NonEmpty(layout.discardButton));
	CHECK(NonEmpty(layout.cancelButton));
	CHECK(HitTestUnsavedChangesCard(layout, Center(layout.saveButton)) ==
		UnsavedCardHit::Save);
}

TEST_CASE("UnsavedChangesCard zero-size client yields empty layout") {
	const UnsavedChangesCardLayout layout = LayoutUnsavedChangesCard(0, 0);
	CHECK_FALSE(NonEmpty(layout.card));
	CHECK(HitTestUnsavedChangesCard(layout, Point(0, 0)) == UnsavedCardHit::None);
}

TEST_CASE("UnsavedChangesCard focus cycles through three buttons") {
	CHECK(Scalpel::CycleUnsavedCardFocus(0, 1) == 1);
	CHECK(Scalpel::CycleUnsavedCardFocus(1, 1) == 2);
	CHECK(Scalpel::CycleUnsavedCardFocus(2, 1) == 0);
	CHECK(Scalpel::CycleUnsavedCardFocus(0, -1) == 2);
	CHECK(Scalpel::CycleUnsavedCardFocus(2, -1) == 1);
}

TEST_CASE("UnsavedChangesCard paint draws non-background pixels in the card") {
	Scalpel::ApplicationEditor editor(320, 200);
	editor.LoadInitialBuffer("paint probe\n");
	(void)editor.TakeFrameDamage();

	UnsavedChangesCardPainter painter;
	const UnsavedChangesCardLayout layout = LayoutUnsavedChangesCard(320, 200);
	editor.SetOverlayPainter(
		[&](Scintilla::Internal::Surface &surface, int width, int height) {
			CHECK(width == 320);
			CHECK(height == 200);
			painter.Paint(surface, layout, "Save changes?", "Untitled", 0);
		});
	editor.RenderFrame({PRectangle::FromInts(0, 0, 320, 200)});

	const std::vector<uint8_t> pixels = editor.FramePixels();
	REQUIRE(pixels.size() == 320U * 200U * 4U);

	// Sample the card center; the light card fill differs from a dimmed corner.
	const int cardCx = static_cast<int>((layout.card.left + layout.card.right) / 2);
	const int cardCy = static_cast<int>((layout.card.top + layout.card.bottom) / 2);
	const size_t cardOffset =
		(static_cast<size_t>(cardCy) * 320U + static_cast<size_t>(cardCx)) * 4U;
	const size_t cornerOffset = 0;
	const bool cardDiffersFromCorner =
		!std::equal(pixels.begin() + static_cast<std::ptrdiff_t>(cardOffset),
			pixels.begin() + static_cast<std::ptrdiff_t>(cardOffset) + 4,
			pixels.begin() + static_cast<std::ptrdiff_t>(cornerOffset));
	CHECK(cardDiffersFromCorner);

	// Focused Save button center should also be non-empty painted chrome.
	const int btnCx = static_cast<int>(
		(layout.saveButton.left + layout.saveButton.right) / 2);
	const int btnCy = static_cast<int>(
		(layout.saveButton.top + layout.saveButton.bottom) / 2);
	const size_t btnOffset =
		(static_cast<size_t>(btnCy) * 320U + static_cast<size_t>(btnCx)) * 4U;
	const bool buttonDiffersFromCorner =
		!std::equal(pixels.begin() + static_cast<std::ptrdiff_t>(btnOffset),
			pixels.begin() + static_cast<std::ptrdiff_t>(btnOffset) + 4,
			pixels.begin() + static_cast<std::ptrdiff_t>(cornerOffset));
	CHECK(buttonDiffersFromCorner);
}
