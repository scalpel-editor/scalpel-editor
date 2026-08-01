// scalpel-editor shaped-run model: HarfBuzz shaping with per-input-byte positions.

#ifndef SHAPEDRUN_H
#define SHAPEDRUN_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "FontPlatform.h"
#include "Geometry.h"

namespace Scintilla::Internal {

class ShapedRunCache;

/**
 * Writing direction stored on a shaped run.
 *
 * The current shaper supports English left-to-right only. Other scripts and
 * mixed-direction line ordering are out of scope; the field stays so later
 * work can use the same run model without collapsing clusters or direction
 * into per-character data.
 */
enum class TextDirection {
	LeftToRight = 0,
};

/**
 * One output glyph from HarfBuzz for a shaped span.
 *
 * cluster is a byte offset into the original UTF-8 input (not a code-point
 * index). Several glyphs may share a cluster when one character maps to many
 * glyphs; ligatures that merge characters use the first character's cluster.
 */
struct ShapedGlyph {
	uint32_t glyphId = 0;
	XYPOSITION xAdvance = 0.0;
	XYPOSITION yAdvance = 0.0;
	XYPOSITION xOffset = 0.0;
	XYPOSITION yOffset = 0.0;
	size_t cluster = 0;
	std::shared_ptr<FontFace> face;
};

/**
 * Cached result of shaping a UTF-8 string: glyphs, per-byte end positions, and
 * valid caret stops.
 *
 * Measurement, wrapping, hit testing, selection, and drawing must all consume
 * this result rather than measuring again. byteEndPositions matches the
 * Surface::MeasureWidths contract: positions[i] is the cumulative advance
 * through input byte i (inclusive). Bytes that belong to the same character
 * share that character's end position.
 *
 * caretStops lists byte offsets where a caret may sit: 0, each shaped-cluster
 * start, and text.size(). Trail bytes and positions inside a merged cluster are
 * not stops.
 */
class ShapedRun {
public:
	std::string text;
	TextDirection direction = TextDirection::LeftToRight;
	std::vector<ShapedGlyph> glyphs;
	std::vector<XYPOSITION> byteEndPositions;
	std::vector<size_t> caretStops;

	[[nodiscard]] bool Empty() const noexcept {
		return text.empty();
	}

	[[nodiscard]] XYPOSITION Width() const noexcept {
		if (byteEndPositions.empty()) {
			return 0.0;
		}
		return byteEndPositions.back();
	}
};

/**
 * Copy a shaped run into the Surface::MeasureWidths contract.
 *
 * positions must hold at least run.text.size() entries. Each positions[i] is
 * the cumulative advance through input byte i (inclusive). Empty runs write
 * nothing.
 */
void FillMeasureWidths(const ShapedRun &run, XYPOSITION *positions) noexcept;

/**
 * Shape text and fill MeasureWidths positions in one step.
 *
 * Requires a non-null primary face when text is non-empty (same as ShapeText).
 * Empty text is a no-op. When cache is non-null, the shaped run is taken from
 * (or stored in) that cache so later layout and drawing can share it.
 */
void MeasureWidthsShaped(
	std::string_view text,
	const std::shared_ptr<FontFace> &primary,
	const FontFallback &fallback,
	XYPOSITION *positions,
	ShapedRunCache *cache = nullptr);

/**
 * Total advance of shaped text (empty text is 0).
 *
 * Same face and cache rules as MeasureWidthsShaped.
 */
XYPOSITION WidthTextShaped(
	std::string_view text,
	const std::shared_ptr<FontFace> &primary,
	const FontFallback &fallback = {},
	ShapedRunCache *cache = nullptr);

/**
 * Shape UTF-8 text as left-to-right English.
 *
 * Walks the input by UTF-8 character (each invalid byte is one character, same
 * policy as the document). Splits into maximal same-face spans using primary
 * when it has the glyph, else FontFallback::Select, else primary (HarfBuzz
 * emits .notdef). Each span uses fixed Latin script and English language
 * properties with discretionary ligatures (liga, dlig) disabled. Cluster
 * values stay as original byte offsets across fallback splits. Correct shaping
 * for other scripts is outside the current editor scope.
 */
ShapedRun ShapeText(
	std::string_view text,
	const std::shared_ptr<FontFace> &primary,
	const FontFallback &fallback = {});

/**
 * Bounded cache of shaped runs keyed by faces, text bytes, and direction.
 *
 * Returned shared pointers keep runs alive across later lookups and eviction.
 * Cache entries retain every face in their key, while runs retain the faces
 * used by their glyphs. Clear or destroy the cache when discarding a FontCache
 * if you need unused entries and faces released promptly.
 */
class ShapedRunCache {
public:
	explicit ShapedRunCache(size_t capacity = 256);
	~ShapedRunCache();

	ShapedRunCache(const ShapedRunCache &) = delete;
	ShapedRunCache(ShapedRunCache &&) = delete;
	ShapedRunCache &operator=(const ShapedRunCache &) = delete;
	ShapedRunCache &operator=(ShapedRunCache &&) = delete;

	std::shared_ptr<const ShapedRun> Get(
		std::string_view text,
		const std::shared_ptr<FontFace> &primary,
		const FontFallback &fallback = {});

	void Clear() noexcept;
	[[nodiscard]] size_t Size() const noexcept;
	[[nodiscard]] size_t Capacity() const noexcept;

private:
	class Impl;
	std::unique_ptr<Impl> impl;
};

}

#endif
