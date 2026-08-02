#include "RendererTest.h"

#include "FixedGlyphBitmapReduce.h"

#include <limits>

namespace {

std::vector<uint8_t> MakeGray(int width, int height, uint8_t value) {
	return std::vector<uint8_t>(static_cast<size_t>(width) * static_cast<size_t>(height), value);
}

std::vector<uint8_t> MakeRgba(int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
	for (size_t i = 0; i < pixels.size(); i += 4u) {
		pixels[i + 0] = r;
		pixels[i + 1] = g;
		pixels[i + 2] = b;
		pixels[i + 3] = a;
	}
	return pixels;
}

/** Premultiplied solid colour (r,g,b already multiplied by a/255). */
std::vector<uint8_t> MakePremultRgba(int width, int height,
	uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	return MakeRgba(width, height, r, g, b, a);
}

int AbsDiff(int a, int b) {
	return a > b ? a - b : b - a;
}

bool ChannelsNear(const uint8_t *a, const uint8_t *b, int channels, int tol) {
	for (int c = 0; c < channels; c++) {
		if (AbsDiff(a[c], b[c]) > tol) {
			return false;
		}
	}
	return true;
}

} // namespace

TEST_CASE("Fixed glyph bitmap area reduction rejects invalid inputs") {
	FixedGlyphBitmapReduceResult out;
	const uint8_t pixel = 255;

	REQUIRE_FALSE(ReduceFixedGlyphBitmapArea(&pixel, 1, 1, 2, 1, 1, out));
	REQUIRE(out.pixels.empty());

	REQUIRE_FALSE(ReduceFixedGlyphBitmapArea(&pixel, 1, 1, 1, 0, 1, out));
	REQUIRE_FALSE(ReduceFixedGlyphBitmapArea(&pixel, 1, 1, 1, 1, -1, out));
	REQUIRE_FALSE(ReduceFixedGlyphBitmapArea(&pixel, -1, 1, 1, 1, 1, out));
	REQUIRE_FALSE(ReduceFixedGlyphBitmapArea(nullptr, 1, 1, 1, 1, 1, out));
	REQUIRE_FALSE(ReduceFixedGlyphBitmapArea(nullptr, 0, 0, 1, 0, 1, out));

	// A float intermediate can exceed vector capacity even when its element
	// count fits in size_t. This must fail before the source is read or memory
	// is allocated.
	const int maxInt = (std::numeric_limits<int>::max)();
	REQUIRE_FALSE(ReduceFixedGlyphBitmapArea(&pixel, maxInt, maxInt, 1,
		maxInt, maxInt - 1, out));
	REQUIRE(out.width == 0);
	REQUIRE(out.height == 0);
	REQUIRE(out.pixels.empty());

	// Empty source with valid destination succeeds with empty output.
	REQUIRE(ReduceFixedGlyphBitmapArea(nullptr, 0, 0, 1, 1, 1, out));
	REQUIRE(out.width == 0);
	REQUIRE(out.height == 0);
	REQUIRE(out.pixels.empty());
}

TEST_CASE("Fixed glyph bitmap area reduction identity and no-upscale") {
	const auto gray = MakeGray(4, 3, 200);
	FixedGlyphBitmapReduceResult out;

	// Exact identity.
	REQUIRE(ReduceFixedGlyphBitmapArea(gray.data(), 4, 3, 1, 4, 3, out));
	REQUIRE(out.width == 4);
	REQUIRE(out.height == 3);
	REQUIRE(out.pixels == gray);

	// Requested magnification keeps the source size and bytes.
	REQUIRE(ReduceFixedGlyphBitmapArea(gray.data(), 4, 3, 1, 8, 6, out));
	REQUIRE(out.width == 4);
	REQUIRE(out.height == 3);
	REQUIRE(out.pixels == gray);

	// Mixed: one axis would grow, the other shrinks — clamp growth only.
	REQUIRE(ReduceFixedGlyphBitmapArea(gray.data(), 4, 3, 1, 8, 2, out));
	REQUIRE(out.width == 4);
	REQUIRE(out.height == 2);
	REQUIRE(out.pixels.size() == 8u);
}

TEST_CASE("Fixed glyph bitmap area reduction extent helper is stable") {
	REQUIRE(FixedGlyphReducedExtent(0, 0.5) == 0);
	REQUIRE(FixedGlyphReducedExtent(10, 0.0) == 10);
	REQUIRE(FixedGlyphReducedExtent(10, 1.0) == 10);
	REQUIRE(FixedGlyphReducedExtent(10, 1.5) == 10);
	REQUIRE(FixedGlyphReducedExtent(10, -0.5) == 10);

	// ceil(109 * (16/109)) at 1x physical for a 16 px face over a 109 ppem strike.
	REQUIRE(FixedGlyphReducedExtent(109, 16.0 / 109.0) == 16);
	// Half scale of a 16 px logical destination over the same strike.
	REQUIRE(FixedGlyphReducedExtent(109, 8.0 / 109.0) == 8);
	// Odd ratios: ceil toward at least one pixel, never above source.
	REQUIRE(FixedGlyphReducedExtent(5, 0.5) == 3);
	REQUIRE(FixedGlyphReducedExtent(7, 1.0 / 3.0) == 3);
	REQUIRE(FixedGlyphReducedExtent(3, 0.1) == 1);
	REQUIRE(FixedGlyphReducedExtent(1, 0.5) == 1);
}

TEST_CASE("Fixed glyph bitmap area reduction preserves uniform gray coverage") {
	const auto gray = MakeGray(6, 4, 180);
	FixedGlyphBitmapReduceResult out;
	REQUIRE(ReduceFixedGlyphBitmapArea(gray.data(), 6, 4, 1, 3, 2, out));
	REQUIRE(out.width == 3);
	REQUIRE(out.height == 2);
	REQUIRE(out.pixels.size() == 6u);
	for (uint8_t sample : out.pixels) {
		REQUIRE(sample == 180);
	}
}

TEST_CASE("Fixed glyph bitmap area reduction preserves uniform premultiplied colour") {
	// Pure red at half alpha, already premultiplied: (128, 0, 0, 128).
	const auto rgba = MakePremultRgba(4, 4, 128, 0, 0, 128);
	FixedGlyphBitmapReduceResult out;
	REQUIRE(ReduceFixedGlyphBitmapArea(rgba.data(), 4, 4, 4, 2, 2, out));
	REQUIRE(out.width == 2);
	REQUIRE(out.height == 2);
	REQUIRE(out.pixels.size() == 16u);
	for (size_t i = 0; i < out.pixels.size(); i += 4u) {
		REQUIRE(out.pixels[i + 0] == 128);
		REQUIRE(out.pixels[i + 1] == 0);
		REQUIRE(out.pixels[i + 2] == 0);
		REQUIRE(out.pixels[i + 3] == 128);
	}
}

TEST_CASE("Fixed glyph bitmap area reduction handles odd ratios and conserves mass") {
	// 5x1 gray ramp; reduce to 2 samples. Exact area means:
	// dest0 covers [0, 2.5): pixels 0,1 and half of 2
	// dest1 covers [2.5, 5): half of 2 plus 3,4
	std::vector<uint8_t> ramp = {10, 20, 30, 40, 50};
	FixedGlyphBitmapReduceResult out;
	REQUIRE(ReduceFixedGlyphBitmapArea(ramp.data(), 5, 1, 1, 2, 1, out));
	REQUIRE(out.width == 2);
	REQUIRE(out.height == 1);
	REQUIRE(out.pixels.size() == 2u);

	// Expected: (10+20+15)/2.5 = 18, (15+40+50)/2.5 = 42.
	REQUIRE(out.pixels[0] == 18);
	REQUIRE(out.pixels[1] == 42);

	// Total mass: input sum 150 over width 5; output average * 5 ≈ 150.
	const int outSum = static_cast<int>(out.pixels[0]) + static_cast<int>(out.pixels[1]);
	// Mean preserved: (18+42)/2 = 30, input mean = 30.
	REQUIRE(outSum == 60);

	// 3x3 -> 2x2 fully opaque white: every output stays 255.
	const auto white = MakeGray(3, 3, 255);
	REQUIRE(ReduceFixedGlyphBitmapArea(white.data(), 3, 3, 1, 2, 2, out));
	REQUIRE(out.width == 2);
	REQUIRE(out.height == 2);
	for (uint8_t sample : out.pixels) {
		REQUIRE(sample == 255);
	}
}

TEST_CASE("Fixed glyph bitmap area reduction keeps premultiplied channel bounds") {
	// Left half opaque pure green premultiplied, right half fully transparent.
	// After a 4->2 horizontal shrink, left sample is green; right is transparent.
	std::vector<uint8_t> src(4 * 1 * 4);
	for (int x = 0; x < 4; x++) {
		const size_t i = static_cast<size_t>(x) * 4u;
		if (x < 2) {
			src[i + 0] = 0;
			src[i + 1] = 255;
			src[i + 2] = 0;
			src[i + 3] = 255;
		} else {
			src[i + 0] = 0;
			src[i + 1] = 0;
			src[i + 2] = 0;
			src[i + 3] = 0;
		}
	}
	FixedGlyphBitmapReduceResult out;
	REQUIRE(ReduceFixedGlyphBitmapArea(src.data(), 4, 1, 4, 2, 1, out));
	REQUIRE(out.width == 2);
	REQUIRE(out.height == 1);
	REQUIRE(out.pixels.size() == 8u);

	// Left output covers source [0,2): solid green.
	REQUIRE(out.pixels[0] == 0);
	REQUIRE(out.pixels[1] == 255);
	REQUIRE(out.pixels[2] == 0);
	REQUIRE(out.pixels[3] == 255);
	// Right output covers source [2,4): solid transparent.
	REQUIRE(out.pixels[4] == 0);
	REQUIRE(out.pixels[5] == 0);
	REQUIRE(out.pixels[6] == 0);
	REQUIRE(out.pixels[7] == 0);

	// Premultiplied invariant for pure green: G == A on every pixel.
	for (size_t i = 0; i < out.pixels.size(); i += 4u) {
		REQUIRE(out.pixels[i + 0] == 0);
		REQUIRE(out.pixels[i + 2] == 0);
		REQUIRE(out.pixels[i + 1] == out.pixels[i + 3]);
	}
}

TEST_CASE("Fixed glyph bitmap area reduction avoids transparent-edge colour leakage") {
	// Sharp premultiplied edge: opaque blue then fully transparent.
	// A straight-alpha filter would smear blue RGB into zero-alpha neighbours.
	// Premultiplied area filtering must keep fully transparent outputs at zero RGB.
	std::vector<uint8_t> src(8 * 1 * 4);
	for (int x = 0; x < 8; x++) {
		const size_t i = static_cast<size_t>(x) * 4u;
		if (x < 4) {
			src[i + 0] = 0;
			src[i + 1] = 0;
			src[i + 2] = 255;
			src[i + 3] = 255;
		} else {
			src[i + 0] = 0;
			src[i + 1] = 0;
			src[i + 2] = 0;
			src[i + 3] = 0;
		}
	}
	FixedGlyphBitmapReduceResult out;
	REQUIRE(ReduceFixedGlyphBitmapArea(src.data(), 8, 1, 4, 4, 1, out));
	REQUIRE(out.width == 4);
	REQUIRE(out.height == 1);

	// Outputs 0,1 cover the opaque half; 2,3 cover the transparent half.
	const uint8_t opaque[4] = {0, 0, 255, 255};
	const uint8_t clear[4] = {0, 0, 0, 0};
	REQUIRE(ChannelsNear(out.pixels.data() + 0, opaque, 4, 0));
	REQUIRE(ChannelsNear(out.pixels.data() + 4, opaque, 4, 0));
	REQUIRE(ChannelsNear(out.pixels.data() + 8, clear, 4, 0));
	REQUIRE(ChannelsNear(out.pixels.data() + 12, clear, 4, 0));

	// Partial-coverage strip: alternate premultiplied red (128,0,0,128) and clear.
	// Reduced 4->1 should average to (64,0,0,64) — still R == A, no dark fringe.
	std::vector<uint8_t> stripe(4 * 1 * 4);
	for (int x = 0; x < 4; x++) {
		const size_t i = static_cast<size_t>(x) * 4u;
		if ((x % 2) == 0) {
			stripe[i + 0] = 128;
			stripe[i + 1] = 0;
			stripe[i + 2] = 0;
			stripe[i + 3] = 128;
		}
	}
	REQUIRE(ReduceFixedGlyphBitmapArea(stripe.data(), 4, 1, 4, 1, 1, out));
	REQUIRE(out.width == 1);
	REQUIRE(out.height == 1);
	REQUIRE(out.pixels[0] == 64);
	REQUIRE(out.pixels[1] == 0);
	REQUIRE(out.pixels[2] == 0);
	REQUIRE(out.pixels[3] == 64);
}

TEST_CASE("Fixed glyph bitmap area reduction conserves colour mass within tolerance") {
	// Checker of two premultiplied colours on a 4x4 grid, reduced to 2x2.
	// Each 2x2 source block becomes one output sample equal to the block mean.
	std::vector<uint8_t> src(4 * 4 * 4);
	for (int y = 0; y < 4; y++) {
		for (int x = 0; x < 4; x++) {
			const size_t i = (static_cast<size_t>(y) * 4u + static_cast<size_t>(x)) * 4u;
			const bool a = ((x / 2) ^ (y / 2)) == 0;
			if (a) {
				// Premultiplied orange at full alpha.
				src[i + 0] = 200;
				src[i + 1] = 100;
				src[i + 2] = 0;
				src[i + 3] = 255;
			} else {
				// Premultiplied cyan at full alpha.
				src[i + 0] = 0;
				src[i + 1] = 180;
				src[i + 2] = 180;
				src[i + 3] = 255;
			}
		}
	}
	FixedGlyphBitmapReduceResult out;
	REQUIRE(ReduceFixedGlyphBitmapArea(src.data(), 4, 4, 4, 2, 2, out));
	REQUIRE(out.width == 2);
	REQUIRE(out.height == 2);

	// Each output block is uniform in the source, so values are exact.
	const uint8_t orange[4] = {200, 100, 0, 255};
	const uint8_t cyan[4] = {0, 180, 180, 255};
	REQUIRE(ChannelsNear(out.pixels.data() + 0, orange, 4, 0));
	REQUIRE(ChannelsNear(out.pixels.data() + 4, cyan, 4, 0));
	REQUIRE(ChannelsNear(out.pixels.data() + 8, cyan, 4, 0));
	REQUIRE(ChannelsNear(out.pixels.data() + 12, orange, 4, 0));

	// Fully transparent source stays transparent with zero RGB.
	const auto clear = MakePremultRgba(5, 5, 0, 0, 0, 0);
	REQUIRE(ReduceFixedGlyphBitmapArea(clear.data(), 5, 5, 4, 2, 2, out));
	for (uint8_t sample : out.pixels) {
		REQUIRE(sample == 0);
	}
}

TEST_CASE("Fixed glyph bitmap area reduction partially transparent coverage") {
	// Gray coverage: left opaque, right clear, odd width 5 -> 2.
	std::vector<uint8_t> gray(5);
	for (int x = 0; x < 5; x++) {
		gray[static_cast<size_t>(x)] = x < 3 ? 255 : 0;
	}
	FixedGlyphBitmapReduceResult out;
	REQUIRE(ReduceFixedGlyphBitmapArea(gray.data(), 5, 1, 1, 2, 1, out));
	// dest0: [0, 2.5) -> (255+255+127.5)/2.5 = 255
	// dest1: [2.5, 5) -> (127.5+0+0)/2.5 = 51
	REQUIRE(out.pixels[0] == 255);
	REQUIRE(out.pixels[1] == 51);

	// Same geometry as premultiplied white (coverage in alpha, RGB == alpha).
	std::vector<uint8_t> rgba(5 * 4);
	for (int x = 0; x < 5; x++) {
		const uint8_t a = gray[static_cast<size_t>(x)];
		const size_t i = static_cast<size_t>(x) * 4u;
		rgba[i + 0] = a;
		rgba[i + 1] = a;
		rgba[i + 2] = a;
		rgba[i + 3] = a;
	}
	REQUIRE(ReduceFixedGlyphBitmapArea(rgba.data(), 5, 1, 4, 2, 1, out));
	REQUIRE(out.pixels[0] == 255);
	REQUIRE(out.pixels[1] == 255);
	REQUIRE(out.pixels[2] == 255);
	REQUIRE(out.pixels[3] == 255);
	REQUIRE(out.pixels[4] == 51);
	REQUIRE(out.pixels[5] == 51);
	REQUIRE(out.pixels[6] == 51);
	REQUIRE(out.pixels[7] == 51);
}
