#include "catch.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "ApplicationEditor.h"
#include "FileErrorCard.h"

using Scalpel::FileErrorCardLayout;
using Scalpel::FileErrorCardPainter;
using Scalpel::HitTestFileErrorCard;
using Scalpel::LayoutFileErrorCard;
using Scintilla::Internal::Point;
using Scintilla::Internal::PRectangle;

namespace {

bool NonEmpty(const PRectangle &rectangle) {
	return rectangle.right > rectangle.left &&
		rectangle.bottom > rectangle.top;
}

Point Center(const PRectangle &rectangle) {
	return Point(
		(rectangle.left + rectangle.right) / 2.0,
		(rectangle.top + rectangle.bottom) / 2.0);
}

}

TEST_CASE("file error card layout fits and exposes one dismiss button") {
	const FileErrorCardLayout layout = LayoutFileErrorCard(800, 600);
	CHECK(layout.scrim == PRectangle::FromInts(0, 0, 800, 600));
	CHECK(NonEmpty(layout.card));
	CHECK(layout.card.left >= 0);
	CHECK(layout.card.top >= 0);
	CHECK(layout.card.right <= 800);
	CHECK(layout.card.bottom <= 600);
	CHECK(layout.card.Contains(layout.title));
	CHECK(layout.card.Contains(layout.path));
	CHECK(layout.card.Contains(layout.dismissButton));
	CHECK(HitTestFileErrorCard(layout, Center(layout.dismissButton)));
	CHECK_FALSE(HitTestFileErrorCard(layout, Point(1, 1)));
}

TEST_CASE("file error card narrow short and zero clients stay bounded") {
	const FileErrorCardLayout narrow = LayoutFileErrorCard(180, 100);
	CHECK(narrow.card.left >= 0);
	CHECK(narrow.card.right <= 180);
	CHECK(narrow.card.top >= 0);
	CHECK(narrow.card.bottom <= 100);
	CHECK(narrow.dismissButton.left >= 0);
	CHECK(narrow.dismissButton.right <= 180);
	CHECK(narrow.dismissButton.top >= 0);
	CHECK(narrow.dismissButton.bottom <= 100);
	CHECK(NonEmpty(narrow.dismissButton));
	CHECK(HitTestFileErrorCard(narrow, Center(narrow.dismissButton)));

	const FileErrorCardLayout empty = LayoutFileErrorCard(0, 0);
	CHECK_FALSE(NonEmpty(empty.card));
	CHECK_FALSE(HitTestFileErrorCard(empty, Point(0, 0)));
}

TEST_CASE("file error card paint draws a card and focused button") {
	Scalpel::ApplicationEditor editor(360, 220);
	editor.LoadInitialBuffer("paint probe\n");
	(void)editor.TakeFrameDamage();

	FileErrorCardPainter painter;
	const FileErrorCardLayout layout = LayoutFileErrorCard(360, 220);
	editor.SetOverlayPainter(
		[&](Scintilla::Internal::Surface &surface, int width, int height) {
			CHECK(width == 360);
			CHECK(height == 220);
			painter.Paint(surface, layout, "Could not open file",
				"/work/missing.txt");
		});
	editor.RenderFrame({PRectangle::FromInts(0, 0, 360, 220)});

	const std::vector<uint8_t> pixels = editor.FramePixels();
	REQUIRE(pixels.size() == 360U * 220U * 4U);
	const Point cardCenter = Center(layout.card);
	const Point buttonCenter = Center(layout.dismissButton);
	const auto offset = [](Point point) {
		return (static_cast<std::size_t>(point.y) * 360U +
			static_cast<std::size_t>(point.x)) * 4U;
	};
	const std::size_t cornerOffset = 0;
	const std::size_t cardOffset = offset(cardCenter);
	const std::size_t buttonOffset = offset(buttonCenter);
	CHECK_FALSE(std::equal(
		pixels.begin() + static_cast<std::ptrdiff_t>(cardOffset),
		pixels.begin() + static_cast<std::ptrdiff_t>(cardOffset) + 4,
		pixels.begin() + static_cast<std::ptrdiff_t>(cornerOffset)));
	CHECK_FALSE(std::equal(
		pixels.begin() + static_cast<std::ptrdiff_t>(buttonOffset),
		pixels.begin() + static_cast<std::ptrdiff_t>(buttonOffset) + 4,
		pixels.begin() + static_cast<std::ptrdiff_t>(cornerOffset)));
}
