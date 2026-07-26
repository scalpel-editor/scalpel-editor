#include "ApplicationTest.h"

#include "ScrollBar.h"

#include <array>
#include <memory>

#include "DrawSurface.h"
#include "GlContext.h"
#include "Renderer.h"

using Scintilla::Internal::PRectangle;
using Scintilla::Internal::Point;

namespace {

Scalpel::ScrollAxisMetrics VerticalMetrics(Scintilla::Line position,
	Scintilla::Line upperBound, Scintilla::Line pageSize,
	bool visible = true) {
	Scalpel::ScrollAxisMetrics metrics;
	metrics.position = position;
	metrics.upperBound = upperBound;
	metrics.pageSize = pageSize;
	metrics.pageIncrement = std::max(Scintilla::Line{1}, pageSize - 1);
	metrics.visible = visible;
	return metrics;
}

Scalpel::ScrollAxisMetrics HorizontalMetrics(Scintilla::Line position,
	Scintilla::Line upperBound, Scintilla::Line pageSize,
	bool visible = true) {
	Scalpel::ScrollAxisMetrics metrics;
	metrics.position = position;
	metrics.upperBound = upperBound;
	metrics.pageSize = pageSize;
	metrics.pageIncrement = std::max(Scintilla::Line{1}, pageSize / 3);
	metrics.visible = visible;
	return metrics;
}

bool NonEmpty(const PRectangle &rc) {
	return rc.right > rc.left && rc.bottom > rc.top;
}

}

TEST_CASE("scroll bar layout places vertical horizontal and junction") {
	const auto vertical = VerticalMetrics(0, 20, 10);
	const auto horizontal = HorizontalMetrics(0, 400, 200);
	const Scalpel::ScrollBarLayout layout = Scalpel::LayoutScrollBars(
		320, 200, 40, vertical, horizontal);

	CHECK(layout.vertical.track.left == 320 - Scalpel::ScrollBarThickness());
	CHECK(layout.vertical.track.right == 320);
	CHECK(layout.vertical.track.top == 40);
	CHECK(layout.vertical.track.bottom == 200 - Scalpel::ScrollBarThickness());
	CHECK(layout.horizontal.track.left == 0);
	CHECK(layout.horizontal.track.right == 320 - Scalpel::ScrollBarThickness());
	CHECK(layout.horizontal.track.top == 200 - Scalpel::ScrollBarThickness());
	CHECK(layout.horizontal.track.bottom == 200);
	CHECK(layout.junction.left == layout.vertical.track.left);
	CHECK(layout.junction.top == layout.horizontal.track.top);
	CHECK(layout.junction.right == 320);
	CHECK(layout.junction.bottom == 200);
	CHECK(layout.vertical.enabled);
	CHECK(layout.horizontal.enabled);
	CHECK(NonEmpty(layout.vertical.thumb));
	CHECK(NonEmpty(layout.horizontal.thumb));
}

TEST_CASE("scroll bar layout hides axes that are not visible") {
	const auto vertical = VerticalMetrics(0, 10, 5, true);
	const auto hidden = HorizontalMetrics(0, 100, 50, false);
	const Scalpel::ScrollBarLayout onlyVertical = Scalpel::LayoutScrollBars(
		200, 120, 0, vertical, hidden);
	CHECK(NonEmpty(onlyVertical.vertical.track));
	CHECK_FALSE(NonEmpty(onlyVertical.horizontal.track));
	CHECK_FALSE(NonEmpty(onlyVertical.junction));

	const auto hiddenVertical = VerticalMetrics(0, 10, 5, false);
	const auto horizontal = HorizontalMetrics(0, 100, 50, true);
	const Scalpel::ScrollBarLayout onlyHorizontal = Scalpel::LayoutScrollBars(
		200, 120, 0, hiddenVertical, horizontal);
	CHECK_FALSE(NonEmpty(onlyHorizontal.vertical.track));
	CHECK(NonEmpty(onlyHorizontal.horizontal.track));
}

TEST_CASE("scroll bar layout keeps a client pixel on tiny frames") {
	const auto vertical = VerticalMetrics(0, 5, 2);
	const auto horizontal = HorizontalMetrics(0, 5, 2);
	const Scalpel::ScrollBarLayout layout = Scalpel::LayoutScrollBars(
		3, 3, 0, vertical, horizontal);
	// Thickness clamps so one logical client pixel remains on each axis.
	CHECK(static_cast<int>(layout.vertical.track.Width()) <= 2);
	CHECK(static_cast<int>(layout.horizontal.track.Height()) <= 2);
}

TEST_CASE("scroll bar mapping uses minimum thumb and full-track disabled") {
	const int track = 200;
	const int enabledThumb = Scalpel::ScrollBarThumbLength(50, 10, track);
	CHECK(enabledThumb >= Scalpel::ScrollBarMinThumbLength());
	CHECK(enabledThumb < track);

	const int disabledThumb = Scalpel::ScrollBarThumbLength(0, 10, track);
	CHECK(disabledThumb == track);

	// Minimum thumb applies only when the track is long enough.
	const int shortTrack = 10;
	const int shortThumb = Scalpel::ScrollBarThumbLength(50, 10, shortTrack);
	CHECK(shortThumb > 0);
	CHECK(shortThumb <= shortTrack);
	CHECK(shortThumb < Scalpel::ScrollBarMinThumbLength());

	CHECK(Scalpel::ScrollBarThumbOffset(0, 100, track, enabledThumb) == 0);
	const int endOffset = Scalpel::ScrollBarThumbOffset(100, 100, track, enabledThumb);
	CHECK(endOffset == track - enabledThumb);

	const int midOffset = Scalpel::ScrollBarThumbOffset(50, 100, track, enabledThumb);
	CHECK(midOffset > 0);
	CHECK(midOffset < endOffset);

	// Large line counts must not overflow to negative offsets.
	const Scintilla::Line largeUpper = 1'000'000'000;
	const int largeOffset = Scalpel::ScrollBarThumbOffset(
		largeUpper / 2, largeUpper, track, enabledThumb);
	CHECK(largeOffset >= 0);
	CHECK(largeOffset <= track - enabledThumb);

	const Scintilla::Line fromPointer = Scalpel::ScrollBarPositionFromPointer(
		endOffset + enabledThumb / 2, enabledThumb / 2, track, enabledThumb, 100);
	CHECK(fromPointer == 100);
	const Scintilla::Line fromStart = Scalpel::ScrollBarPositionFromPointer(
		enabledThumb / 2, enabledThumb / 2, track, enabledThumb, 100);
	CHECK(fromStart == 0);
}

TEST_CASE("scroll bar layout hit tests track thumb and junction") {
	const auto vertical = VerticalMetrics(10, 40, 10);
	const auto horizontal = HorizontalMetrics(100, 400, 200);
	const Scalpel::ScrollBarLayout layout = Scalpel::LayoutScrollBars(
		320, 200, 0, vertical, horizontal);

	const auto junction = Scalpel::HitTestScrollBars(layout,
		Point::FromInts(
			static_cast<int>((layout.junction.left + layout.junction.right) / 2),
			static_cast<int>((layout.junction.top + layout.junction.bottom) / 2)));
	CHECK(junction.hit == Scalpel::ScrollBarHit::Junction);

	const auto thumb = Scalpel::HitTestScrollBars(layout,
		Point::FromInts(
			static_cast<int>((layout.vertical.thumb.left + layout.vertical.thumb.right) / 2),
			static_cast<int>((layout.vertical.thumb.top + layout.vertical.thumb.bottom) / 2)));
	CHECK(thumb.hit == Scalpel::ScrollBarHit::Thumb);
	CHECK(thumb.axis == Scalpel::ScrollBarAxis::Vertical);

	const int beforeY = static_cast<int>(layout.vertical.track.top + 1);
	if (beforeY < layout.vertical.thumb.top) {
		const auto before = Scalpel::HitTestScrollBars(layout,
			Point::FromInts(
				static_cast<int>((layout.vertical.track.left + layout.vertical.track.right) / 2),
				beforeY));
		CHECK(before.hit == Scalpel::ScrollBarHit::TrackBefore);
	}

	const int afterY = static_cast<int>(layout.vertical.track.bottom - 1);
	if (afterY >= layout.vertical.thumb.bottom) {
		const auto after = Scalpel::HitTestScrollBars(layout,
			Point::FromInts(
				static_cast<int>((layout.vertical.track.left + layout.vertical.track.right) / 2),
				afterY));
		CHECK(after.hit == Scalpel::ScrollBarHit::TrackAfter);
	}

	const auto none = Scalpel::HitTestScrollBars(layout, Point::FromInts(10, 10));
	CHECK(none.hit == Scalpel::ScrollBarHit::None);

	// Disabled axis still reports a consumable hit over the track.
	auto disabledVertical = vertical;
	disabledVertical.upperBound = 0;
	const Scalpel::ScrollBarLayout disabled = Scalpel::LayoutScrollBars(
		320, 200, 0, disabledVertical, horizontal);
	CHECK_FALSE(disabled.vertical.enabled);
	const auto disabledHit = Scalpel::HitTestScrollBars(disabled,
		Point::FromInts(
			static_cast<int>((disabled.vertical.track.left + disabled.vertical.track.right) / 2),
			static_cast<int>((disabled.vertical.track.top + disabled.vertical.track.bottom) / 2)));
	CHECK(disabledHit.hit == Scalpel::ScrollBarHit::Thumb);
}

TEST_CASE("scroll bar pointer pages track and drags thumb") {
	const auto vertical = VerticalMetrics(10, 40, 10);
	const auto horizontal = HorizontalMetrics(0, 0, 200, false);
	const Scalpel::ScrollBarLayout layout = Scalpel::LayoutScrollBars(
		200, 200, 0, vertical, horizontal);
	REQUIRE(layout.vertical.enabled);
	REQUIRE(NonEmpty(layout.vertical.thumb));

	Scalpel::ScrollBarInteraction interaction;
	const int trackX = static_cast<int>(
		(layout.vertical.track.left + layout.vertical.track.right) / 2);

	// Track after the thumb pages once toward the end.
	const int afterY = static_cast<int>(layout.vertical.track.bottom - 2);
	REQUIRE(afterY >= layout.vertical.thumb.bottom);
	const Scalpel::ScrollBarPointerResult page =
		Scalpel::HandleScrollBarPointer(interaction, layout,
			{Scalpel::PointerAction::Press, Scintilla::KeyMod::Norm,
				static_cast<double>(trackX), static_cast<double>(afterY),
				0, 0, 1, 0});
	CHECK(page.consumed);
	CHECK(page.request.kind == Scalpel::ScrollBarRequestKind::SetVertical);
	CHECK(page.request.position ==
		std::min(vertical.upperBound, vertical.position + vertical.pageIncrement));

	// Cancel press state before a drag sequence.
	Scalpel::CancelScrollBarInteraction(interaction);

	// Thumb drag retains grab offset outside the track.
	const int thumbY = static_cast<int>(
		(layout.vertical.thumb.top + layout.vertical.thumb.bottom) / 2);
	const Scalpel::ScrollBarPointerResult press =
		Scalpel::HandleScrollBarPointer(interaction, layout,
			{Scalpel::PointerAction::Press, Scintilla::KeyMod::Norm,
				static_cast<double>(trackX), static_cast<double>(thumbY),
				0, 0, 2, 0});
	CHECK(press.consumed);
	CHECK(interaction.dragging);
	CHECK(press.request.kind == Scalpel::ScrollBarRequestKind::None);

	const Scalpel::ScrollBarPointerResult drag =
		Scalpel::HandleScrollBarPointer(interaction, layout,
			{Scalpel::PointerAction::Move, Scintilla::KeyMod::Norm,
				static_cast<double>(trackX + 40),
				static_cast<double>(layout.vertical.track.bottom + 30),
				0, 0, 3, -1});
	CHECK(drag.consumed);
	CHECK(drag.request.kind == Scalpel::ScrollBarRequestKind::SetVertical);
	CHECK(drag.request.position == vertical.upperBound);

	const Scalpel::ScrollBarPointerResult release =
		Scalpel::HandleScrollBarPointer(interaction, layout,
			{Scalpel::PointerAction::Release, Scintilla::KeyMod::Norm,
				10, 10, 0, 0, 4, 0});
	CHECK(release.consumed);
	CHECK_FALSE(interaction.dragging);

	// Disabled bar rejects track paging.
	auto disabled = vertical;
	disabled.upperBound = 0;
	const Scalpel::ScrollBarLayout dead = Scalpel::LayoutScrollBars(
		200, 200, 0, disabled, horizontal);
	Scalpel::ScrollBarInteraction idle;
	const Scalpel::ScrollBarPointerResult ignored =
		Scalpel::HandleScrollBarPointer(idle, dead,
			{Scalpel::PointerAction::Press, Scintilla::KeyMod::Norm,
				static_cast<double>(
					(dead.vertical.track.left + dead.vertical.track.right) / 2),
				static_cast<double>(
					(dead.vertical.track.top + dead.vertical.track.bottom) / 2),
				0, 0, 5, 0});
	CHECK(ignored.consumed);
	CHECK(ignored.request.kind == Scalpel::ScrollBarRequestKind::None);

	// Junction is consumed only.
	const auto both = HorizontalMetrics(0, 100, 50, true);
	const Scalpel::ScrollBarLayout withJunction = Scalpel::LayoutScrollBars(
		200, 200, 0, vertical, both);
	REQUIRE(NonEmpty(withJunction.junction));
	Scalpel::ScrollBarInteraction junctionState;
	const Scalpel::ScrollBarPointerResult junction =
		Scalpel::HandleScrollBarPointer(junctionState, withJunction,
			{Scalpel::PointerAction::Press, Scintilla::KeyMod::Norm,
				static_cast<double>(
					(withJunction.junction.left + withJunction.junction.right) / 2),
				static_cast<double>(
					(withJunction.junction.top + withJunction.junction.bottom) / 2),
				0, 0, 6, 0});
	CHECK(junction.consumed);
	CHECK(junction.request.kind == Scalpel::ScrollBarRequestKind::None);
}

TEST_CASE("scroll bar pointer wheel scrolls the bar axis") {
	const auto vertical = VerticalMetrics(0, 30, 10);
	const auto horizontal = HorizontalMetrics(0, 300, 100);
	const Scalpel::ScrollBarLayout layout = Scalpel::LayoutScrollBars(
		220, 180, 0, vertical, horizontal);
	Scalpel::ScrollBarInteraction interaction;

	const int vX = static_cast<int>(
		(layout.vertical.track.left + layout.vertical.track.right) / 2);
	const int vY = static_cast<int>(
		(layout.vertical.track.top + layout.vertical.track.bottom) / 2);
	const Scalpel::ScrollBarPointerResult verticalWheel =
		Scalpel::HandleScrollBarPointer(interaction, layout,
			{Scalpel::PointerAction::Scroll, Scintilla::KeyMod::Norm,
				static_cast<double>(vX), static_cast<double>(vY),
				0, 20, 1, -1});
	CHECK(verticalWheel.consumed);
	CHECK(verticalWheel.request.kind == Scalpel::ScrollBarRequestKind::SetVertical);
	CHECK(verticalWheel.request.position > 0);

	const int hX = static_cast<int>(
		(layout.horizontal.track.left + layout.horizontal.track.right) / 2);
	const int hY = static_cast<int>(
		(layout.horizontal.track.top + layout.horizontal.track.bottom) / 2);
	const Scalpel::ScrollBarPointerResult horizontalWheel =
		Scalpel::HandleScrollBarPointer(interaction, layout,
			{Scalpel::PointerAction::Scroll, Scintilla::KeyMod::Norm,
				static_cast<double>(hX), static_cast<double>(hY),
				0, 10, 2, -1});
	CHECK(horizontalWheel.consumed);
	CHECK(horizontalWheel.request.kind ==
		Scalpel::ScrollBarRequestKind::SetHorizontal);
	CHECK(horizontalWheel.request.position > 0);
}

TEST_CASE("scroll bar paint fills track thumb and junction") {
	const auto vertical = VerticalMetrics(5, 20, 8);
	const auto horizontal = HorizontalMetrics(50, 200, 100);
	const Scalpel::ScrollBarLayout layout = Scalpel::LayoutScrollBars(
		160, 100, 0, vertical, horizontal);
	REQUIRE(NonEmpty(layout.vertical.thumb));
	REQUIRE(NonEmpty(layout.junction));

	auto context = std::make_unique<Scintilla::Internal::GlContext>();
	Scintilla::Internal::Renderer renderer(*context);
	auto surface = Scintilla::Internal::CreateDrawSurface(renderer, 160, 100);
	REQUIRE(surface);
	Scalpel::ScrollBarPaintState paint;
	Scalpel::PaintScrollBars(*surface, layout, paint);

	renderer.MakeCurrent();
	const std::vector<uint8_t> pixels =
		static_cast<Scintilla::Internal::DrawSurface &>(*surface)
			.Buffer()
			.ReadPixelsTopDown();
	REQUIRE(pixels.size() == 160U * 100U * 4U);

	const auto sample = [&](int x, int y) {
		const size_t offset =
			(static_cast<size_t>(y) * 160U + static_cast<size_t>(x)) * 4U;
		return std::array<uint8_t, 3>{
			pixels[offset], pixels[offset + 1], pixels[offset + 2]};
	};

	const int trackX = static_cast<int>(
		(layout.vertical.track.left + layout.vertical.track.right) / 2);
	// A track pixel outside the thumb should differ from a thumb pixel.
	int trackY = static_cast<int>(layout.vertical.track.top + 1);
	if (trackY >= layout.vertical.thumb.top &&
		trackY < layout.vertical.thumb.bottom) {
		trackY = static_cast<int>(layout.vertical.thumb.bottom);
		if (trackY >= layout.vertical.track.bottom) {
			trackY = static_cast<int>(layout.vertical.track.top);
		}
	}
	const int thumbX = static_cast<int>(
		(layout.vertical.thumb.left + layout.vertical.thumb.right) / 2);
	const int thumbY = static_cast<int>(
		(layout.vertical.thumb.top + layout.vertical.thumb.bottom) / 2);
	const auto trackPixel = sample(trackX, trackY);
	const auto thumbPixel = sample(thumbX, thumbY);
	CHECK((trackPixel[0] != thumbPixel[0] || trackPixel[1] != thumbPixel[1] ||
		trackPixel[2] != thumbPixel[2]));

	const int junctionX = static_cast<int>(
		(layout.junction.left + layout.junction.right) / 2);
	const int junctionY = static_cast<int>(
		(layout.junction.top + layout.junction.bottom) / 2);
	const auto junctionPixel = sample(junctionX, junctionY);
	// Junction is painted (not left as clear black).
	CHECK((junctionPixel[0] != 0 || junctionPixel[1] != 0 ||
		junctionPixel[2] != 0));
}
