#include "ScrollBar.h"

#include <algorithm>
#include <limits>

namespace Scalpel {

namespace {

using Scintilla::Internal::ColourRGBA;
using Scintilla::Internal::Fill;
using Scintilla::Internal::PRectangle;
using Scintilla::Internal::Point;
using Scintilla::Internal::Surface;

constexpr int kThickness = 14;
constexpr int kMinThumb = 24;

// Quiet track, higher-contrast thumb, with hover and pressed steps.
const ColourRGBA kTrackFill(0xd8, 0xd8, 0xd8, 0xff);
const ColourRGBA kThumbFill(0xa0, 0xa0, 0xa0, 0xff);
const ColourRGBA kThumbHover(0x88, 0x88, 0x88, 0xff);
const ColourRGBA kThumbPressed(0x70, 0x70, 0x70, 0xff);
const ColourRGBA kThumbDisabled(0xc0, 0xc0, 0xc0, 0xff);
const ColourRGBA kJunctionFill(0xd0, 0xd0, 0xd0, 0xff);

bool NonEmpty(const PRectangle &rc) noexcept {
	return rc.right > rc.left && rc.bottom > rc.top;
}

bool NonEmptyContains(const PRectangle &rc, Point point) noexcept {
	return NonEmpty(rc) &&
		point.x >= rc.left && point.x < rc.right &&
		point.y >= rc.top && point.y < rc.bottom;
}

int ClampToInt(int64_t value) noexcept {
	return static_cast<int>(std::clamp(value,
		static_cast<int64_t>(std::numeric_limits<int>::min()),
		static_cast<int64_t>(std::numeric_limits<int>::max())));
}

int TrackLengthAlongAxis(const PRectangle &track, ScrollBarAxis axis) noexcept {
	if (!NonEmpty(track)) {
		return 0;
	}
	if (axis == ScrollBarAxis::Vertical) {
		return ClampToInt(static_cast<int64_t>(track.bottom - track.top));
	}
	return ClampToInt(static_cast<int64_t>(track.right - track.left));
}

ColourRGBA ThumbColour(const ScrollBarPaintState &paint, ScrollBarAxis axis,
	bool enabled, ScrollBarHit part) noexcept {
	if (!enabled) {
		return kThumbDisabled;
	}
	if (paint.pressed != ScrollBarHit::None && paint.pressedAxis == axis &&
		paint.pressed == part) {
		return kThumbPressed;
	}
	if (paint.hover != ScrollBarHit::None && paint.hoverAxis == axis &&
		paint.hover == part) {
		return kThumbHover;
	}
	return kThumbFill;
}

PRectangle MakeThumbRect(const PRectangle &track, ScrollBarAxis axis,
	int thumbOffset, int thumbLength) noexcept {
	if (!NonEmpty(track) || thumbLength <= 0) {
		return PRectangle::FromInts(0, 0, 0, 0);
	}
	if (axis == ScrollBarAxis::Vertical) {
		const int top = ClampToInt(static_cast<int64_t>(track.top) + thumbOffset);
		const int bottom = ClampToInt(static_cast<int64_t>(top) + thumbLength);
		return PRectangle::FromInts(
			static_cast<int>(track.left), top,
			static_cast<int>(track.right), bottom);
	}
	const int left = ClampToInt(static_cast<int64_t>(track.left) + thumbOffset);
	const int right = ClampToInt(static_cast<int64_t>(left) + thumbLength);
	return PRectangle::FromInts(
		left, static_cast<int>(track.top),
		right, static_cast<int>(track.bottom));
}

void FillAxisLayout(ScrollBarAxisLayout &axisLayout, ScrollBarAxis axis) noexcept {
	const int trackLength = TrackLengthAlongAxis(axisLayout.track, axis);
	const ScrollAxisMetrics &metrics = axisLayout.metrics;
	axisLayout.enabled = metrics.visible && metrics.upperBound > 0;
	if (!NonEmpty(axisLayout.track)) {
		axisLayout.thumb = PRectangle::FromInts(0, 0, 0, 0);
		return;
	}
	const int thumbLength = ScrollBarThumbLength(
		metrics.upperBound, metrics.pageSize, trackLength);
	const int thumbOffset = ScrollBarThumbOffset(
		metrics.position, metrics.upperBound, trackLength, thumbLength);
	axisLayout.thumb = MakeThumbRect(
		axisLayout.track, axis, thumbOffset, thumbLength);
}

}

int ScrollBarThickness() noexcept {
	return kThickness;
}

int ScrollBarMinThumbLength() noexcept {
	return kMinThumb;
}

int ScrollBarThumbLength(Scintilla::Line upperBound, Scintilla::Line pageSize,
	int trackLength) noexcept {
	if (trackLength <= 0) {
		return 0;
	}
	if (upperBound <= 0) {
		// Disabled full-track thumb.
		return trackLength;
	}
	// thumb = track * page / (page + range); range is upperBound.
	const int64_t page = std::max(Scintilla::Line{1}, pageSize);
	const int64_t total = page + upperBound;
	int64_t length = (static_cast<int64_t>(trackLength) * page) / total;
	if (length < 1) {
		length = 1;
	}
	if (trackLength >= kMinThumb) {
		length = std::max(length, static_cast<int64_t>(kMinThumb));
	}
	if (length > trackLength) {
		length = trackLength;
	}
	return static_cast<int>(length);
}

int ScrollBarThumbOffset(Scintilla::Line position, Scintilla::Line upperBound,
	int trackLength, int thumbLength) noexcept {
	if (trackLength <= 0 || thumbLength <= 0) {
		return 0;
	}
	const int travel = trackLength - thumbLength;
	if (travel <= 0 || upperBound <= 0) {
		return 0;
	}
	Scintilla::Line pos = position;
	if (pos < 0) {
		pos = 0;
	}
	if (pos > upperBound) {
		pos = upperBound;
	}
	// offset = travel * position / upperBound, rounded to nearest.
	const int64_t numer =
		static_cast<int64_t>(travel) * static_cast<int64_t>(pos);
	const int64_t offset = (numer + upperBound / 2) / upperBound;
	return ClampToInt(std::clamp(offset, int64_t{0}, static_cast<int64_t>(travel)));
}

Scintilla::Line ScrollBarPositionFromPointer(int pointerAlongTrack,
	int grabOffset, int trackLength, int thumbLength,
	Scintilla::Line upperBound) noexcept {
	if (upperBound <= 0) {
		return 0;
	}
	const int travel = trackLength - thumbLength;
	if (travel <= 0) {
		return 0;
	}
	const int64_t thumbOrigin =
		static_cast<int64_t>(pointerAlongTrack) - grabOffset;
	int64_t clampedOrigin = std::clamp(thumbOrigin, int64_t{0},
		static_cast<int64_t>(travel));
	// position = upperBound * origin / travel, rounded to nearest.
	const int64_t numer = clampedOrigin * static_cast<int64_t>(upperBound);
	const int64_t position = (numer + travel / 2) / travel;
	return static_cast<Scintilla::Line>(std::clamp(
		position, int64_t{0}, static_cast<int64_t>(upperBound)));
}

ScrollBarLayout LayoutScrollBars(int frameWidth, int frameHeight, int topChrome,
	const ScrollAxisMetrics &vertical, const ScrollAxisMetrics &horizontal) noexcept {
	ScrollBarLayout layout;
	layout.vertical.metrics = vertical;
	layout.horizontal.metrics = horizontal;

	if (frameWidth <= 0 || frameHeight <= 0) {
		return layout;
	}

	const int thickness = ScrollBarThickness();
	const int chrome = std::clamp(topChrome, 0, std::max(0, frameHeight - 1));

	// Prefer a full-thickness bar; keep at least one logical client pixel when
	// the frame permits it so the editor client does not collapse to empty.
	int verticalWidth = 0;
	int horizontalHeight = 0;
	if (vertical.visible && frameWidth > 1) {
		verticalWidth = std::min(thickness, frameWidth - 1);
	}
	if (horizontal.visible) {
		const int availableBelowChrome = std::max(0, frameHeight - chrome);
		if (availableBelowChrome > 1) {
			horizontalHeight = std::min(thickness, availableBelowChrome - 1);
		}
	}

	const int verticalLeft = frameWidth - verticalWidth;
	const int horizontalTop = frameHeight - horizontalHeight;

	if (verticalWidth > 0) {
		const int trackTop = chrome;
		const int trackBottom = horizontalHeight > 0 ? horizontalTop : frameHeight;
		if (trackBottom > trackTop) {
			layout.vertical.track = PRectangle::FromInts(
				verticalLeft, trackTop, frameWidth, trackBottom);
		}
	}
	if (horizontalHeight > 0) {
		const int trackRight = verticalWidth > 0 ? verticalLeft : frameWidth;
		if (trackRight > 0) {
			layout.horizontal.track = PRectangle::FromInts(
				0, horizontalTop, trackRight, frameHeight);
		}
	}
	if (verticalWidth > 0 && horizontalHeight > 0) {
		layout.junction = PRectangle::FromInts(
			verticalLeft, horizontalTop, frameWidth, frameHeight);
	}

	FillAxisLayout(layout.vertical, ScrollBarAxis::Vertical);
	FillAxisLayout(layout.horizontal, ScrollBarAxis::Horizontal);
	return layout;
}

ScrollBarHitResult HitTestScrollBars(const ScrollBarLayout &layout,
	Point point) noexcept {
	ScrollBarHitResult result;
	if (NonEmptyContains(layout.junction, point)) {
		result.hit = ScrollBarHit::Junction;
		return result;
	}
	if (NonEmptyContains(layout.vertical.track, point)) {
		result.axis = ScrollBarAxis::Vertical;
		if (!layout.vertical.enabled) {
			// Disabled bar still consumes the hit so editor selection does not start.
			result.hit = ScrollBarHit::Thumb;
			return result;
		}
		if (NonEmptyContains(layout.vertical.thumb, point)) {
			result.hit = ScrollBarHit::Thumb;
		} else if (point.y < layout.vertical.thumb.top) {
			result.hit = ScrollBarHit::TrackBefore;
		} else {
			result.hit = ScrollBarHit::TrackAfter;
		}
		return result;
	}
	if (NonEmptyContains(layout.horizontal.track, point)) {
		result.axis = ScrollBarAxis::Horizontal;
		if (!layout.horizontal.enabled) {
			result.hit = ScrollBarHit::Thumb;
			return result;
		}
		if (NonEmptyContains(layout.horizontal.thumb, point)) {
			result.hit = ScrollBarHit::Thumb;
		} else if (point.x < layout.horizontal.thumb.left) {
			result.hit = ScrollBarHit::TrackBefore;
		} else {
			result.hit = ScrollBarHit::TrackAfter;
		}
		return result;
	}
	return result;
}

void PaintScrollBars(Surface &surface, const ScrollBarLayout &layout,
	const ScrollBarPaintState &paint) noexcept {
	if (NonEmpty(layout.vertical.track)) {
		surface.FillRectangle(layout.vertical.track, Fill(kTrackFill));
		if (NonEmpty(layout.vertical.thumb)) {
			const ColourRGBA colour = ThumbColour(paint, ScrollBarAxis::Vertical,
				layout.vertical.enabled, ScrollBarHit::Thumb);
			surface.FillRectangle(layout.vertical.thumb, Fill(colour));
		}
	}
	if (NonEmpty(layout.horizontal.track)) {
		surface.FillRectangle(layout.horizontal.track, Fill(kTrackFill));
		if (NonEmpty(layout.horizontal.thumb)) {
			const ColourRGBA colour = ThumbColour(paint, ScrollBarAxis::Horizontal,
				layout.horizontal.enabled, ScrollBarHit::Thumb);
			surface.FillRectangle(layout.horizontal.thumb, Fill(colour));
		}
	}
	if (NonEmpty(layout.junction)) {
		surface.FillRectangle(layout.junction, Fill(kJunctionFill));
	}
}

}
