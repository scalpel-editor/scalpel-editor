#include "catch.hpp"

#include "UnsavedChangesCard.h"

using Scalpel::HitTestUnsavedChangesCard;
using Scalpel::LayoutUnsavedChangesCard;
using Scalpel::UnsavedCardHit;
using Scalpel::UnsavedChangesCardLayout;
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
