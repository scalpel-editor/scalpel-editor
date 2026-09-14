// Scintilla source code edit control
/** @file Geometry.cxx
 ** Helper functions for geometric calculations.
 **/
// Copyright 2020 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cstdint>
#include <cmath>

#include <algorithm>

#include "Geometry.h"

namespace {

constexpr unsigned int Mixed(unsigned char a, unsigned char b, double proportion) noexcept {
	return static_cast<unsigned int>(a + proportion * (b - a));
}

}

namespace Scintilla::Internal {

PRectangle RectangleBounds(const std::vector<PRectangle> &rectangles) noexcept {
	PRectangle bounds;
	for (const PRectangle rc : rectangles) {
		if (!rc.Empty()) {
			bounds = bounds.Empty() ? rc : PRectangle(std::min(bounds.left, rc.left),
				std::min(bounds.top, rc.top), std::max(bounds.right, rc.right),
				std::max(bounds.bottom, rc.bottom));
		}
	}
	return bounds;
}

std::vector<PRectangle> NormalizeRectangles(const std::vector<PRectangle> &rectangles,
	PRectangle clip, size_t maximumRectangles) {
	if (!maximumRectangles) {
		throw std::invalid_argument("repaint rectangle limit must be positive");
	}
	std::vector<PRectangle> clipped;
	std::vector<XYPOSITION> edges;
	for (const PRectangle rc : rectangles) {
		const PRectangle intersection(std::max(rc.left, clip.left), std::max(rc.top, clip.top),
			std::min(rc.right, clip.right), std::min(rc.bottom, clip.bottom));
		if (!intersection.Empty()) {
			clipped.push_back(intersection);
			edges.push_back(intersection.top);
			edges.push_back(intersection.bottom);
		}
	}
	std::sort(edges.begin(), edges.end());
	edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
	std::vector<PRectangle> result;
	for (size_t edge = 1; edge < edges.size(); ++edge) {
		std::vector<Interval> spans;
		for (const PRectangle rc : clipped) {
			if (rc.top <= edges[edge - 1] && rc.bottom >= edges[edge]) {
				spans.push_back({rc.left, rc.right});
			}
		}
		std::sort(spans.begin(), spans.end(), [](Interval a, Interval b) { return a.left < b.left; });
		for (size_t span = 0; span < spans.size();) {
			Interval merged = spans[span++];
			while (span < spans.size() && spans[span].left <= merged.right) {
				merged.right = std::max(merged.right, spans[span++].right);
			}
			// Coalesce vertically adjacent strips with identical horizontal coverage.
			const auto previous = std::find_if(result.begin(), result.end(), [&](PRectangle rc) {
				return rc.left == merged.left && rc.right == merged.right && rc.bottom == edges[edge - 1];
			});
			if (previous != result.end()) {
				previous->bottom = edges[edge];
			} else {
				result.emplace_back(merged.left, edges[edge - 1], merged.right, edges[edge]);
			}
			if (result.size() > maximumRectangles) {
				return {RectangleBounds(clipped)};
			}
		}
	}
	return result;
}

bool RectanglesContain(const std::vector<PRectangle> &rectangles, PRectangle rectangle) {
	if (rectangle.Empty()) {
		return true;
	}
	// No bounding fallback is allowed when deciding actual coverage.
	const auto covered = NormalizeRectangles(rectangles, rectangle,
		static_cast<size_t>(-1));
	return covered.size() == 1 && covered.front() == rectangle;
}

PRectangle Clamp(PRectangle rc, Edge edge, XYPOSITION position) noexcept {
	switch (edge) {
	case Edge::left:
		return PRectangle(std::clamp(position, rc.left, rc.right), rc.top, rc.right, rc.bottom);
	case Edge::top:
		return PRectangle(rc.left, std::clamp(position, rc.top, rc.bottom), rc.right, rc.bottom);
	case Edge::right:
		return PRectangle(rc.left, rc.top, std::clamp(position, rc.left, rc.right), rc.bottom);
	case Edge::bottom:
	default:
		return PRectangle(rc.left, rc.top, rc.right, std::clamp(position, rc.top, rc.bottom));
	}
}

PRectangle Side(PRectangle rc, Edge edge, XYPOSITION size) noexcept {
	switch (edge) {
	case Edge::left:
		return PRectangle(rc.left, rc.top, std::min(rc.left + size, rc.right), rc.bottom);
	case Edge::top:
		return PRectangle(rc.left, rc.top, rc.right, std::min(rc.top + size, rc.bottom));
	case Edge::right:
		return PRectangle(std::max(rc.left, rc.right - size), rc.top, rc.right, rc.bottom);
	case Edge::bottom:
	default:
		return PRectangle(rc.left, std::max(rc.top, rc.bottom - size), rc.right, rc.bottom);
	}
}

Interval Intersection(Interval a, Interval b) noexcept {
	const XYPOSITION leftMax = std::max(a.left, b.left);
	const XYPOSITION rightMin = std::min(a.right, b.right);
	// If the result would have a negative width. make empty instead.
	const XYPOSITION rightResult = (rightMin >= leftMax) ? rightMin : leftMax;
	return { leftMax, rightResult };
}

PRectangle Intersection(PRectangle rc, Interval horizontalBounds) noexcept {
	const Interval intersection = Intersection(HorizontalBounds(rc), horizontalBounds);
	return PRectangle(intersection.left, rc.top, intersection.right, rc.bottom);
}

Interval HorizontalBounds(PRectangle rc) noexcept {
	return { rc.left, rc.right };
}

XYPOSITION PixelAlign(XYPOSITION xy, int pixelDivisions) noexcept {
	return std::round(xy * pixelDivisions) / pixelDivisions;
}

XYPOSITION PixelAlignFloor(XYPOSITION xy, int pixelDivisions) noexcept {
	return std::floor(xy * pixelDivisions) / pixelDivisions;
}

XYPOSITION PixelAlignCeil(XYPOSITION xy, int pixelDivisions) noexcept {
	return std::ceil(xy * pixelDivisions) / pixelDivisions;
}

Point PixelAlign(const Point &pt, int pixelDivisions) noexcept {
	return Point(
		PixelAlign(pt.x, pixelDivisions),
		PixelAlign(pt.y, pixelDivisions));
}

PRectangle PixelAlign(const PRectangle &rc, int pixelDivisions) noexcept {
	// Move left and right side to nearest pixel to avoid blurry visuals.
	// The top and bottom should be integers but floor them to make sure.
	// `pixelDivisions` is commonly 1 except for 'retina' displays where it is 2.
	// On retina displays, the positions should be moved to the nearest device
	// pixel which is the nearest half logical pixel.
	return PRectangle(
		PixelAlign(rc.left, pixelDivisions),
		PixelAlignFloor(rc.top, pixelDivisions),
		PixelAlign(rc.right, pixelDivisions),
		PixelAlignFloor(rc.bottom, pixelDivisions));
}

PRectangle PixelAlignOutside(const PRectangle &rc, int pixelDivisions) noexcept {
	// Move left and right side to extremes (floor(left) ceil(right)) to avoid blurry visuals.
	return PRectangle(
		PixelAlignFloor(rc.left, pixelDivisions),
		PixelAlignFloor(rc.top, pixelDivisions),
		PixelAlignCeil(rc.right, pixelDivisions),
		PixelAlignFloor(rc.bottom, pixelDivisions));
}

ColourRGBA ColourRGBA::MixedWith(ColourRGBA other) const noexcept {
	const unsigned int red = (GetRed() + other.GetRed()) / 2;
	const unsigned int green = (GetGreen() + other.GetGreen()) / 2;
	const unsigned int blue = (GetBlue() + other.GetBlue()) / 2;
	const unsigned int alpha = (GetAlpha() + other.GetAlpha()) / 2;
	return ColourRGBA(red, green, blue, alpha);
}

ColourRGBA ColourRGBA::MixedWith(ColourRGBA other, double proportion) const noexcept {
	return ColourRGBA(
		Mixed(GetRed(), other.GetRed(), proportion),
		Mixed(GetGreen(), other.GetGreen(), proportion),
		Mixed(GetBlue(), other.GetBlue(), proportion),
		Mixed(GetAlpha(), other.GetAlpha(), proportion));
}

}
