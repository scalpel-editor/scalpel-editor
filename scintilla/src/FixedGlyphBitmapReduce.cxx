// Area reduction for fixed-strike glyph bitmaps (gray coverage and premultiplied RGBA).

#include "FixedGlyphBitmapReduce.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Scintilla::Internal {

namespace {

constexpr int kGrayChannels = 1;
constexpr int kRgbaChannels = 4;

[[nodiscard]] bool ChannelsSupported(int channels) noexcept {
	return channels == kGrayChannels || channels == kRgbaChannels;
}

/** True when a * b fits in size_t; writes the product on success. */
[[nodiscard]] bool MulSize(size_t a, size_t b, size_t &out) noexcept {
	if (a != 0 && b > (std::numeric_limits<size_t>::max)() / a) {
		return false;
	}
	out = a * b;
	return true;
}

/** True when a * b * c fits in size_t; writes the product on success. */
[[nodiscard]] bool MulSize3(size_t a, size_t b, size_t c, size_t &out) noexcept {
	size_t ab = 0;
	if (!MulSize(a, b, ab)) {
		return false;
	}
	return MulSize(ab, c, out);
}

[[nodiscard]] uint8_t RoundToU8(double value) noexcept {
	if (!(value > 0.0)) {
		return 0;
	}
	if (value >= 255.0) {
		return 255;
	}
	return static_cast<uint8_t>(value + 0.5);
}

/**
 * 1D box (area) reduction along a contiguous sample line.
 *
 * Each sample has `channels` consecutive components. srcCount samples become
 * destCount samples. When counts match, values are copied (uint8->float) or
 * rounded (float->uint8). Caller guarantees destCount > 0, srcCount > 0, and
 * destCount <= srcCount.
 */
void ReduceLineU8ToF(const uint8_t *src, int srcCount, float *dest, int destCount,
	int channels) {
	if (destCount == srcCount) {
		const int n = srcCount * channels;
		for (int i = 0; i < n; i++) {
			dest[i] = static_cast<float>(src[i]);
		}
		return;
	}
	const double scale = static_cast<double>(srcCount) / static_cast<double>(destCount);
	const double invWidth = 1.0 / scale;
	for (int i = 0; i < destCount; i++) {
		const double left = static_cast<double>(i) * scale;
		const double right = static_cast<double>(i + 1) * scale;
		for (int c = 0; c < channels; c++) {
			double sum = 0.0;
			double x = left;
			while (x < right) {
				int idx = static_cast<int>(x);
				if (idx >= srcCount) {
					idx = srcCount - 1;
				}
				const double nextBoundary = static_cast<double>(idx + 1);
				const double segmentEnd = nextBoundary < right ? nextBoundary : right;
				const double weight = segmentEnd - x;
				sum += static_cast<double>(src[idx * channels + c]) * weight;
				x = segmentEnd;
			}
			dest[i * channels + c] = static_cast<float>(sum * invWidth);
		}
	}
}

void ReduceLineFToU8(const float *src, int srcCount, uint8_t *dest, int destCount,
	int channels) {
	if (destCount == srcCount) {
		const int n = srcCount * channels;
		for (int i = 0; i < n; i++) {
			dest[i] = RoundToU8(static_cast<double>(src[i]));
		}
		return;
	}
	const double scale = static_cast<double>(srcCount) / static_cast<double>(destCount);
	const double invWidth = 1.0 / scale;
	for (int i = 0; i < destCount; i++) {
		const double left = static_cast<double>(i) * scale;
		const double right = static_cast<double>(i + 1) * scale;
		for (int c = 0; c < channels; c++) {
			double sum = 0.0;
			double x = left;
			while (x < right) {
				int idx = static_cast<int>(x);
				if (idx >= srcCount) {
					idx = srcCount - 1;
				}
				const double nextBoundary = static_cast<double>(idx + 1);
				const double segmentEnd = nextBoundary < right ? nextBoundary : right;
				const double weight = segmentEnd - x;
				sum += static_cast<double>(src[idx * channels + c]) * weight;
				x = segmentEnd;
			}
			dest[i * channels + c] = RoundToU8(sum * invWidth);
		}
	}
}

} // namespace

int FixedGlyphReducedExtent(int sourceExtent, double reductionFactor) noexcept {
	if (sourceExtent <= 0) {
		return 0;
	}
	if (!(reductionFactor > 0.0) || !(reductionFactor < 1.0)) {
		return sourceExtent;
	}
	const double product = static_cast<double>(sourceExtent) * reductionFactor;
	// ceil(product) without depending on floating noise just under an integer.
	int reduced = static_cast<int>(product);
	if (static_cast<double>(reduced) < product) {
		++reduced;
	}
	if (reduced < 1) {
		reduced = 1;
	}
	if (reduced > sourceExtent) {
		reduced = sourceExtent;
	}
	return reduced;
}

bool ReduceFixedGlyphBitmapArea(
	const uint8_t *source,
	int sourceWidth,
	int sourceHeight,
	int channels,
	int destWidth,
	int destHeight,
	FixedGlyphBitmapReduceResult &out) {
	out = FixedGlyphBitmapReduceResult{};

	if (!ChannelsSupported(channels)) {
		return false;
	}
	if (sourceWidth < 0 || sourceHeight < 0) {
		return false;
	}
	if (sourceWidth == 0 || sourceHeight == 0) {
		return true;
	}
	if (source == nullptr) {
		return false;
	}
	if (destWidth <= 0 || destHeight <= 0) {
		return false;
	}

	// Never CPU-upscale: clamp each axis to the source size.
	const int outWidth = destWidth < sourceWidth ? destWidth : sourceWidth;
	const int outHeight = destHeight < sourceHeight ? destHeight : sourceHeight;

	size_t outBytes = 0;
	if (!MulSize3(static_cast<size_t>(outWidth), static_cast<size_t>(outHeight),
			static_cast<size_t>(channels), outBytes)) {
		return false;
	}

	if (outWidth == sourceWidth && outHeight == sourceHeight) {
		out.width = sourceWidth;
		out.height = sourceHeight;
		out.pixels.assign(source, source + outBytes);
		return true;
	}

	// Intermediate: outWidth * sourceHeight * channels floats (after H pass).
	size_t intermediateCount = 0;
	if (!MulSize3(static_cast<size_t>(outWidth), static_cast<size_t>(sourceHeight),
			static_cast<size_t>(channels), intermediateCount)) {
		return false;
	}

	std::vector<float> intermediate(intermediateCount);
	const size_t srcRowElements =
		static_cast<size_t>(sourceWidth) * static_cast<size_t>(channels);
	const size_t midRowElements =
		static_cast<size_t>(outWidth) * static_cast<size_t>(channels);

	// Horizontal pass: each source row -> outWidth samples.
	for (int y = 0; y < sourceHeight; y++) {
		const uint8_t *srcRow = source + static_cast<size_t>(y) * srcRowElements;
		float *midRow = intermediate.data() + static_cast<size_t>(y) * midRowElements;
		ReduceLineU8ToF(srcRow, sourceWidth, midRow, outWidth, channels);
	}

	// Vertical pass: gather each column into a contiguous line, then reduce.
	out.width = outWidth;
	out.height = outHeight;
	out.pixels.resize(outBytes);

	std::vector<float> columnSrc(static_cast<size_t>(sourceHeight) * static_cast<size_t>(channels));
	std::vector<uint8_t> columnDest(static_cast<size_t>(outHeight) * static_cast<size_t>(channels));

	for (int x = 0; x < outWidth; x++) {
		for (int y = 0; y < sourceHeight; y++) {
			const float *midPx =
				intermediate.data() + static_cast<size_t>(y) * midRowElements +
				static_cast<size_t>(x) * static_cast<size_t>(channels);
			float *colPx =
				columnSrc.data() + static_cast<size_t>(y) * static_cast<size_t>(channels);
			for (int c = 0; c < channels; c++) {
				colPx[c] = midPx[c];
			}
		}
		ReduceLineFToU8(columnSrc.data(), sourceHeight, columnDest.data(), outHeight, channels);
		for (int y = 0; y < outHeight; y++) {
			uint8_t *outPx =
				out.pixels.data() +
				(static_cast<size_t>(y) * static_cast<size_t>(outWidth) +
					static_cast<size_t>(x)) *
					static_cast<size_t>(channels);
			const uint8_t *colPx =
				columnDest.data() + static_cast<size_t>(y) * static_cast<size_t>(channels);
			for (int c = 0; c < channels; c++) {
				outPx[c] = colPx[c];
			}
		}
	}

	return true;
}

}
