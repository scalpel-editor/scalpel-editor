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

ScrollBarPaintState ScrollBarPaintFromInteraction(
	const ScrollBarInteraction &interaction) noexcept {
	ScrollBarPaintState paint;
	paint.hover = interaction.hover;
	paint.hoverAxis = interaction.hoverAxis;
	paint.pressed = interaction.pressed;
	paint.pressedAxis = interaction.pressedAxis;
	if (interaction.dragging) {
		paint.pressed = ScrollBarHit::Thumb;
		paint.pressedAxis = interaction.dragAxis;
	}
	return paint;
}

void CancelScrollBarInteraction(ScrollBarInteraction &interaction) noexcept {
	interaction.hover = ScrollBarHit::None;
	interaction.pressed = ScrollBarHit::None;
	interaction.dragging = false;
	interaction.grabOffset = 0;
	interaction.verticalWheelRemainder = 0.0;
	interaction.horizontalWheelRemainder = 0.0;
}

namespace {

int PointerAlongTrack(const ScrollBarAxisLayout &axisLayout, ScrollBarAxis axis,
	Point point) noexcept {
	if (axis == ScrollBarAxis::Vertical) {
		return static_cast<int>(point.y - axisLayout.track.top);
	}
	return static_cast<int>(point.x - axisLayout.track.left);
}

int ThumbOriginAlongTrack(const ScrollBarAxisLayout &axisLayout,
	ScrollBarAxis axis) noexcept {
	if (axis == ScrollBarAxis::Vertical) {
		return static_cast<int>(axisLayout.thumb.top - axisLayout.track.top);
	}
	return static_cast<int>(axisLayout.thumb.left - axisLayout.track.left);
}

int AxisTrackLength(const ScrollBarAxisLayout &axisLayout,
	ScrollBarAxis axis) noexcept {
	if (!NonEmpty(axisLayout.track)) {
		return 0;
	}
	if (axis == ScrollBarAxis::Vertical) {
		return ClampToInt(static_cast<int64_t>(
			axisLayout.track.bottom - axisLayout.track.top));
	}
	return ClampToInt(static_cast<int64_t>(
		axisLayout.track.right - axisLayout.track.left));
}

const ScrollBarAxisLayout &AxisLayout(const ScrollBarLayout &layout,
	ScrollBarAxis axis) noexcept {
	return axis == ScrollBarAxis::Vertical ? layout.vertical : layout.horizontal;
}

Scintilla::Line ClampPosition(Scintilla::Line position,
	Scintilla::Line upperBound) noexcept {
	if (position < 0) {
		return 0;
	}
	if (position > upperBound) {
		return upperBound;
	}
	return position;
}

Scintilla::Line PageStepPosition(const ScrollAxisMetrics &metrics,
	bool towardEnd) noexcept {
	const Scintilla::Line step = std::max(Scintilla::Line{1}, metrics.pageIncrement);
	return ClampPosition(
		towardEnd ? metrics.position + step : metrics.position - step,
		metrics.upperBound);
}

void ApplyScrollRequest(ScrollBarPointerResult &result, ScrollBarAxis axis,
	Scintilla::Line position) noexcept {
	result.request.kind = axis == ScrollBarAxis::Vertical ?
		ScrollBarRequestKind::SetVertical : ScrollBarRequestKind::SetHorizontal;
	result.request.position = position;
}

}

ScrollBarPointerResult HandleScrollBarPointer(ScrollBarInteraction &interaction,
	const ScrollBarLayout &layout, const PointerInput &input) noexcept {
	ScrollBarPointerResult result;
	const Point point = Point::FromInts(static_cast<int>(input.x),
		static_cast<int>(input.y));

	if (input.action == PointerAction::Leave) {
		if (interaction.dragging) {
			// Surface leave during an implicit grab keeps the drag until release.
			result.consumed = true;
			return result;
		}
		if (interaction.hover != ScrollBarHit::None ||
			interaction.pressed != ScrollBarHit::None) {
			result.barDirty = true;
		}
		CancelScrollBarInteraction(interaction);
		return result;
	}

	if (input.action == PointerAction::Scroll) {
		const ScrollBarHitResult hit = HitTestScrollBars(layout, point);
		if (hit.hit == ScrollBarHit::None) {
			return result;
		}
		result.consumed = true;
		result.pointerOverScrollBar = true;
		if (hit.hit == ScrollBarHit::Junction) {
			return result;
		}
		const ScrollBarAxisLayout &axisLayout = AxisLayout(layout, hit.axis);
		if (!axisLayout.enabled) {
			return result;
		}
		// Wheel over a bar always scrolls that axis (no Shift remap).
		if (hit.axis == ScrollBarAxis::Vertical) {
			interaction.verticalWheelRemainder += input.deltaY * 0.3;
			const Scintilla::Line lines = static_cast<Scintilla::Line>(
				interaction.verticalWheelRemainder);
			interaction.verticalWheelRemainder -= lines;
			if (lines != 0) {
				ApplyScrollRequest(result, ScrollBarAxis::Vertical,
					ClampPosition(axisLayout.metrics.position + lines,
						axisLayout.metrics.upperBound));
			}
		} else {
			const double amount = std::abs(input.deltaX) > std::abs(input.deltaY) ?
				input.deltaX : input.deltaY;
			interaction.horizontalWheelRemainder += amount * 4.0;
			const int pixels =
				static_cast<int>(interaction.horizontalWheelRemainder);
			interaction.horizontalWheelRemainder -= pixels;
			if (pixels != 0) {
				ApplyScrollRequest(result, ScrollBarAxis::Horizontal,
					ClampPosition(axisLayout.metrics.position + pixels,
						axisLayout.metrics.upperBound));
			}
		}
		return result;
	}

	if (interaction.dragging) {
		result.consumed = true;
		result.pointerOverScrollBar = true;
		const ScrollBarAxisLayout &axisLayout =
			AxisLayout(layout, interaction.dragAxis);
		if (input.action == PointerAction::Release && input.button == 0) {
			interaction.dragging = false;
			interaction.pressed = ScrollBarHit::None;
			result.barDirty = true;
			const ScrollBarHitResult hit = HitTestScrollBars(layout, point);
			interaction.hover = hit.hit;
			interaction.hoverAxis = hit.axis;
			return result;
		}
		if (input.action == PointerAction::Move ||
			input.action == PointerAction::Press) {
			if (!axisLayout.enabled) {
				return result;
			}
			const int trackLength = AxisTrackLength(
				axisLayout, interaction.dragAxis);
			const int thumbLength = ScrollBarThumbLength(
				axisLayout.metrics.upperBound, axisLayout.metrics.pageSize,
				trackLength);
			const int along = PointerAlongTrack(
				axisLayout, interaction.dragAxis, point);
			ApplyScrollRequest(result, interaction.dragAxis,
				ScrollBarPositionFromPointer(along, interaction.grabOffset,
					trackLength, thumbLength, axisLayout.metrics.upperBound));
			result.barDirty = true;
		}
		return result;
	}

	const ScrollBarHitResult hit = HitTestScrollBars(layout, point);
	result.pointerOverScrollBar = hit.hit != ScrollBarHit::None;

	if (input.action == PointerAction::Move) {
		if (interaction.hover != hit.hit ||
			(hit.hit != ScrollBarHit::None &&
				interaction.hoverAxis != hit.axis)) {
			result.barDirty = true;
		}
		interaction.hover = hit.hit;
		interaction.hoverAxis = hit.axis;
		if (hit.hit != ScrollBarHit::None) {
			result.consumed = true;
		}
		return result;
	}

	if (input.action == PointerAction::Press) {
		if (hit.hit == ScrollBarHit::None) {
			return result;
		}
		result.consumed = true;
		// Non-left buttons over chrome are consumed without scrolling or selecting.
		if (input.button != 0) {
			return result;
		}
		if (hit.hit == ScrollBarHit::Junction) {
			interaction.pressed = ScrollBarHit::Junction;
			result.barDirty = true;
			return result;
		}
		const ScrollBarAxisLayout &axisLayout = AxisLayout(layout, hit.axis);
		if (!axisLayout.enabled) {
			// Disabled full-track thumb: ignore thumb and track presses.
			interaction.pressed = hit.hit;
			interaction.pressedAxis = hit.axis;
			result.barDirty = true;
			return result;
		}
		if (hit.hit == ScrollBarHit::Thumb) {
			interaction.dragging = true;
			interaction.dragAxis = hit.axis;
			interaction.pressed = ScrollBarHit::Thumb;
			interaction.pressedAxis = hit.axis;
			interaction.grabOffset = PointerAlongTrack(axisLayout, hit.axis, point) -
				ThumbOriginAlongTrack(axisLayout, hit.axis);
			result.barDirty = true;
			return result;
		}
		if (hit.hit == ScrollBarHit::TrackBefore ||
			hit.hit == ScrollBarHit::TrackAfter) {
			interaction.pressed = hit.hit;
			interaction.pressedAxis = hit.axis;
			result.barDirty = true;
			ApplyScrollRequest(result, hit.axis,
				PageStepPosition(axisLayout.metrics,
					hit.hit == ScrollBarHit::TrackAfter));
			return result;
		}
		return result;
	}

	if (input.action == PointerAction::Release) {
		if (interaction.pressed != ScrollBarHit::None || hit.hit != ScrollBarHit::None) {
			result.consumed = true;
			result.pointerOverScrollBar = hit.hit != ScrollBarHit::None;
		}
		if (interaction.pressed != ScrollBarHit::None) {
			interaction.pressed = ScrollBarHit::None;
			result.barDirty = true;
		}
		interaction.hover = hit.hit;
		interaction.hoverAxis = hit.axis;
		return result;
	}

	return result;
}

}
