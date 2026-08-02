// Area reduction for fixed-strike glyph bitmaps (gray coverage and premultiplied RGBA).

#ifndef FIXEDGLYPHBITMAPREDUCE_H
#define FIXEDGLYPHBITMAPREDUCE_H

#include <cstdint>
#include <vector>

namespace Scintilla::Internal {

/**
 * Pixel buffer after CPU area reduction of a fixed glyph bitmap.
 *
 * width and height are the actual output size (the source size when no shrink
 * is performed). pixels is row-major and tightly packed with the same channel
 * count as the source. Bearings, logical layout size, and metricsScale stay
 * with the caller; this result carries only reduced samples.
 */
struct FixedGlyphBitmapReduceResult {
	int width = 0;
	int height = 0;
	std::vector<uint8_t> pixels;
};

/**
 * Choose one axis of a reduced fixed-bitmap texture extent.
 *
 * Computes ceil(sourceExtent * reductionFactor), then clamps into
 * [1, sourceExtent] so the result never exceeds the strike. reductionFactor is
 * the uniform physical shrink ratio (metricsScale * RasterScale). Non-positive
 * source extents yield 0. Factors outside (0, 1) yield sourceExtent (no shrink).
 */
[[nodiscard]] int FixedGlyphReducedExtent(int sourceExtent, double reductionFactor) noexcept;

/**
 * Area-reduce fixed glyph bitmap pixels.
 *
 * channels must be 1 (gray coverage) or 4 (premultiplied RGBA). Premultiplied
 * colour is filtered as-is without converting to straight alpha. Uses separable
 * horizontal then vertical box filters with float intermediates and a single
 * round to uint8 per output component.
 *
 * Never upscales: each destination axis is clamped to the source size. When
 * both axes match the source, the source is copied and reported unchanged.
 *
 * Returns false for invalid dimensions, unsupported channel counts, a null
 * source with non-empty area, or an unsatisfied storage request, and leaves out
 * empty.
 * Empty source (zero width or height) succeeds with an empty result.
 */
[[nodiscard]] bool ReduceFixedGlyphBitmapArea(
	const uint8_t *source,
	int sourceWidth,
	int sourceHeight,
	int channels,
	int destWidth,
	int destHeight,
	FixedGlyphBitmapReduceResult &out);

}

#endif
