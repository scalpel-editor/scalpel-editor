#pragma once

// Offscreen renderer coverage uses a headless EGL context, without Wayland:
// primitive bounds, nested clips, alpha blending, image placement, gradients,
// pixmap copies, pattern fills, context creation, and clear/readback.
// Text coverage includes DrawText variants, glyph caching and placement, shaped
// widths, fallback faces, ink bounds, selection, carets, indicators, margins,
// scrolling, and synthetic pen offsets. Fixture fonts contain ASCII and U+2603;
// combining marks remain out of scope.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "DrawSurface.h"
#include "FontPlatform.h"
#include "Geometry.h"
#include "GlContext.h"
#include "Platform.h"
#include "Renderer.h"
#include "ShapedRun.h"

#include "catch.hpp"

using namespace Scintilla::Internal;

namespace {

inline bool ExactColour(ColourRGBA a, ColourRGBA b) {
	return a.GetRed() == b.GetRed() && a.GetGreen() == b.GetGreen() &&
		a.GetBlue() == b.GetBlue() && a.GetAlpha() == b.GetAlpha();
}

inline ColourRGBA PixelAt(
	const std::vector<uint8_t> &topDown, int width, int x, int y) {
	const size_t i =
		(static_cast<size_t>(y) * static_cast<size_t>(width) +
			static_cast<size_t>(x)) *
		4u;
	return ColourRGBA(topDown[i], topDown[i + 1], topDown[i + 2], topDown[i + 3]);
}

inline bool HasNonBackgroundInk(const ColourBuffer &buffer, ColourRGBA bg) {
	for (int y = 0; y < buffer.Height(); y++) {
		for (int x = 0; x < buffer.Width(); x++) {
			if (!ExactColour(buffer.ReadPixel(x, y), bg)) {
				return true;
			}
		}
	}
	return false;
}

/** Half-open ink bounds of non-bg pixels; empty when right <= left. */
struct InkBounds {
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;

	bool Empty() const noexcept { return right <= left || bottom <= top; }
};

inline InkBounds FindInkBounds(const ColourBuffer &buffer, ColourRGBA bg) {
	InkBounds bounds{buffer.Width(), buffer.Height(), 0, 0};
	bool any = false;
	for (int y = 0; y < buffer.Height(); y++) {
		for (int x = 0; x < buffer.Width(); x++) {
			if (!ExactColour(buffer.ReadPixel(x, y), bg)) {
				any = true;
				bounds.left = std::min(bounds.left, x);
				bounds.top = std::min(bounds.top, y);
				bounds.right = std::max(bounds.right, x + 1);
				bounds.bottom = std::max(bounds.bottom, y + 1);
			}
		}
	}
	return any ? bounds : InkBounds{};
}

}
