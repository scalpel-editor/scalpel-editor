// Axis-neutral layout, hit testing, paint, and pointer transitions for fixed
// in-window scrollbars. ApplicationEditor owns ranges and positions;
// ApplicationUi owns one ScrollBarInteraction and the shell applies returned
// position requests. This unit stays free of Wayland and Scintilla editor
// internals.

#ifndef SCROLLBAR_H
#define SCROLLBAR_H

#include <cstdint>

#include "ApplicationInput.h"
#include "Geometry.h"
#include "Platform.h"
#include "Position.h"
#include "ScrollMetrics.h"

namespace Scalpel {

/** Fixed bar thickness in logical pixels (vertical width / horizontal height). */
[[nodiscard]] int ScrollBarThickness() noexcept;

/** Preferred minimum thumb length when the track is long enough. */
[[nodiscard]] int ScrollBarMinThumbLength() noexcept;

/** Which bar axis a layout, hit, or interaction targets. */
enum class ScrollBarAxis {
	Vertical,
	Horizontal,
};

/** Pointer hit within a single bar's track or thumb. */
enum class ScrollBarHit {
	None,
	TrackBefore,
	Thumb,
	TrackAfter,
	/** Corner where vertical and horizontal bars meet; input is consumed only. */
	Junction,
};

struct ScrollBarHitResult {
	ScrollBarHit hit = ScrollBarHit::None;
	ScrollBarAxis axis = ScrollBarAxis::Vertical;
};

/**
 * Layout for one axis in full-frame logical coordinates.
 * Thumb is empty when the track itself is empty.
 */
struct ScrollBarAxisLayout {
	Scintilla::Internal::PRectangle track;
	Scintilla::Internal::PRectangle thumb;
	ScrollAxisMetrics metrics;
	bool enabled = false;
};

struct ScrollBarLayout {
	ScrollBarAxisLayout vertical;
	ScrollBarAxisLayout horizontal;
	Scintilla::Internal::PRectangle junction;
};

/** Transient hover / press paint state for both bars. */
struct ScrollBarPaintState {
	ScrollBarHit hover = ScrollBarHit::None;
	ScrollBarAxis hoverAxis = ScrollBarAxis::Vertical;
	ScrollBarHit pressed = ScrollBarHit::None;
	ScrollBarAxis pressedAxis = ScrollBarAxis::Vertical;
};

/**
 * Pointer interaction state owned by ApplicationUi (or a test harness).
 * Does not store editor positions; drag motion produces set-position requests.
 */
struct ScrollBarInteraction {
	ScrollBarHit hover = ScrollBarHit::None;
	ScrollBarAxis hoverAxis = ScrollBarAxis::Vertical;
	ScrollBarHit pressed = ScrollBarHit::None;
	ScrollBarAxis pressedAxis = ScrollBarAxis::Vertical;
	bool dragging = false;
	ScrollBarAxis dragAxis = ScrollBarAxis::Vertical;
	/** Pointer coordinate along the track minus thumb origin at press. */
	int grabOffset = 0;
	/** Fractional smooth-scroll motion retained until it reaches one line. */
	double verticalWheelRemainder = 0.0;
	/** Fractional smooth-scroll motion retained until it reaches one pixel. */
	double horizontalWheelRemainder = 0.0;
};

/** Exact scroll request produced by pointer handling. */
enum class ScrollBarRequestKind {
	None,
	SetVertical,
	SetHorizontal,
};

struct ScrollBarRequest {
	ScrollBarRequestKind kind = ScrollBarRequestKind::None;
	Scintilla::Line position = 0;
};

/**
 * Result of applying one pointer event to the scrollbar interaction model.
 * consumed means the event must not reach the editor. barDirty means a bar
 * needs repaint (hover or thumb motion).
 */
struct ScrollBarPointerResult {
	bool consumed = false;
	bool barDirty = false;
	ScrollBarRequest request;
	/** Pointer is over a track, thumb, or junction (arrow cursor). */
	bool pointerOverScrollBar = false;
};

/**
 * Map position to thumb origin along a track of trackLength with thumbLength.
 * Returns 0 when travel is zero. Overflow-safe for large line counts.
 */
[[nodiscard]] int ScrollBarThumbOffset(Scintilla::Line position,
	Scintilla::Line upperBound, int trackLength, int thumbLength) noexcept;

/**
 * Thumb length for a track given page size and range. Uses the minimum thumb
 * when the track is long enough; otherwise fills the track. When upperBound is
 * zero the thumb fills the track (disabled full-track appearance).
 */
[[nodiscard]] int ScrollBarThumbLength(Scintilla::Line upperBound,
	Scintilla::Line pageSize, int trackLength) noexcept;

/**
 * Map a pointer coordinate along the track (with retained grab offset) to a
 * scroll position. grabOffset is pointer position minus thumb origin at press.
 */
[[nodiscard]] Scintilla::Line ScrollBarPositionFromPointer(int pointerAlongTrack,
	int grabOffset, int trackLength, int thumbLength,
	Scintilla::Line upperBound) noexcept;

/**
 * Lay out vertical and horizontal bars inside the frame below topChrome.
 * Vertical sits on the right below top chrome; horizontal on the bottom to the
 * left of the vertical bar. Both bars and the junction may be empty when the
 * matching metrics are not visible or the frame is too small.
 */
[[nodiscard]] ScrollBarLayout LayoutScrollBars(int frameWidth, int frameHeight,
	int topChrome, const ScrollAxisMetrics &vertical,
	const ScrollAxisMetrics &horizontal) noexcept;

[[nodiscard]] ScrollBarHitResult HitTestScrollBars(const ScrollBarLayout &layout,
	Scintilla::Internal::Point point) noexcept;

void PaintScrollBars(Scintilla::Internal::Surface &surface,
	const ScrollBarLayout &layout, const ScrollBarPaintState &paint) noexcept;

[[nodiscard]] ScrollBarPaintState ScrollBarPaintFromInteraction(
	const ScrollBarInteraction &interaction) noexcept;

/** Clear drag / press / hover (focus loss, resize, modal open, device loss). */
void CancelScrollBarInteraction(ScrollBarInteraction &interaction) noexcept;

/**
 * Apply one pointer event. layout metrics must match the editor positions that
 * main will update after applying request. A thumb drag keeps ownership after
 * the pointer leaves the track until release or CancelScrollBarInteraction.
 */
[[nodiscard]] ScrollBarPointerResult HandleScrollBarPointer(
	ScrollBarInteraction &interaction, const ScrollBarLayout &layout,
	const PointerInput &input) noexcept;

}

#endif
