// scalpel-editor shaped-run model: HarfBuzz shaping with per-input-byte positions.

#include <algorithm>
#include <cstdint>
#include <list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <hb.h>

#include "EmojiSequence.h"
#include "FontPlatform.h"
#include "Geometry.h"
#include "ShapedRun.h"
#include "UniConversion.h"

namespace Scintilla::Internal {

namespace {

// Escape code point for an invalid UTF-8 byte (matches Document::GetCharacterAndWidth).
constexpr char32_t InvalidByteCodePoint(unsigned char byte) noexcept {
	return static_cast<char32_t>(0xDC80u + byte);
}

constexpr XYPOSITION FromHarfBuzz(hb_position_t value) noexcept {
	return static_cast<XYPOSITION>(value) / 64.0;
}

struct InputCharacter {
	size_t byteOffset = 0;
	size_t byteLength = 0;
	char32_t codePoint = 0;
	bool valid = true;
};

// Split text into characters: valid UTF-8 sequences, or one byte when invalid.
std::vector<InputCharacter> SplitCharacters(std::string_view text) {
	std::vector<InputCharacter> characters;
	size_t i = 0;
	while (i < text.size()) {
		const int classified = UTF8Classify(text.data() + i, text.size() - i);
		InputCharacter ch;
		ch.byteOffset = i;
		if (classified & UTF8MaskInvalid) {
			ch.byteLength = 1;
			ch.codePoint = InvalidByteCodePoint(static_cast<unsigned char>(text[i]));
			ch.valid = false;
		} else {
			ch.byteLength = static_cast<size_t>(classified & UTF8MaskWidth);
			ch.codePoint = static_cast<char32_t>(UnicodeFromUTF8(
				std::string_view(text.data() + i, ch.byteLength)));
			ch.valid = true;
		}
		characters.push_back(ch);
		i += ch.byteLength;
	}
	return characters;
}

std::string FaceIdentity(const FontFace *face) {
	return std::to_string(reinterpret_cast<std::uintptr_t>(face));
}

std::string CacheKey(
	std::string_view text,
	const std::shared_ptr<FontFace> &primary,
	const FontFallback &fallback,
	TextDirection direction) {
	std::string key = FaceIdentity(primary.get());
	key.push_back('|');
	key.append(fallback.CacheIdentity());
	key.push_back('|');
	key.push_back(static_cast<char>('0' + static_cast<int>(direction)));
	key.push_back('|');
	key.append(text);
	return key;
}

// Discretionary ligatures off; required shaping features stay under HarfBuzz defaults.
const hb_feature_t *LigatureFeatures(unsigned int &count) noexcept {
	static const hb_feature_t features[] = {
		{HB_TAG('l', 'i', 'g', 'a'), 0, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END},
		{HB_TAG('d', 'l', 'i', 'g'), 0, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END},
	};
	count = 2;
	return features;
}

void ShapeSpan(
	const std::vector<InputCharacter> &spanCharacters,
	const std::shared_ptr<FontFace> &face,
	size_t unitByteBegin,
	bool collapseClustersToUnit,
	std::vector<ShapedGlyph> &outGlyphs) {
	if (!face || spanCharacters.empty()) {
		return;
	}
	hb_font_t *hbFont = static_cast<hb_font_t *>(face->HarfBuzzFont());
	if (!hbFont) {
		throw std::runtime_error("FontFace has no HarfBuzz font");
	}

	hb_buffer_t *buffer = hb_buffer_create();
	if (!buffer) {
		throw std::runtime_error("HarfBuzz buffer allocation failed");
	}
	// hb_buffer_add requires UNICODE content type; it does not set it itself.
	hb_buffer_set_content_type(buffer, HB_BUFFER_CONTENT_TYPE_UNICODE);
	hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
	hb_buffer_set_cluster_level(buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);

	for (const InputCharacter &ch : spanCharacters) {
		// cluster is the original byte offset so fallback splits keep one address space.
		hb_buffer_add(buffer, static_cast<hb_codepoint_t>(ch.codePoint),
			static_cast<unsigned int>(ch.byteOffset));
	}
	// Script from the buffer contents; direction stays LTR for editor lines.
	hb_buffer_guess_segment_properties(buffer);
	hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
	// English language remains the editor default when HarfBuzz leaves it unset.
	if (hb_buffer_get_language(buffer) == HB_LANGUAGE_INVALID) {
		hb_buffer_set_language(buffer, hb_language_from_string("en", -1));
	}

	unsigned int featureCount = 0;
	const hb_feature_t *features = LigatureFeatures(featureCount);
	hb_shape(hbFont, buffer, features, featureCount);

	const unsigned int glyphCount = hb_buffer_get_length(buffer);
	const hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, nullptr);
	const hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, nullptr);

	// Bitmap strikes shape at strike ppem; scale into the requested logical size.
	const XYPOSITION metricsScale = static_cast<XYPOSITION>(face->MetricsScale());
	outGlyphs.reserve(outGlyphs.size() + glyphCount);
	for (unsigned int g = 0; g < glyphCount; g++) {
		ShapedGlyph glyph;
		glyph.glyphId = infos[g].codepoint;
		glyph.xAdvance = FromHarfBuzz(positions[g].x_advance) * metricsScale;
		glyph.yAdvance = FromHarfBuzz(positions[g].y_advance) * metricsScale;
		glyph.xOffset = FromHarfBuzz(positions[g].x_offset) * metricsScale;
		glyph.yOffset = FromHarfBuzz(positions[g].y_offset) * metricsScale;
		// Supported multi-code-point emoji units expose one caret stop even when
		// the font emits several glyphs with distinct HarfBuzz clusters.
		glyph.cluster = collapseClustersToUnit ? unitByteBegin : infos[g].cluster;
		glyph.face = face;
		outGlyphs.push_back(glyph);
	}

	hb_buffer_destroy(buffer);
}

// Map glyph advances onto per-byte end positions and caret stops.
// units list character-index ranges that form caret-atomic segments.
void FinishRun(
	ShapedRun &run,
	const std::vector<InputCharacter> &characters,
	const std::vector<EmojiUnit> &units) {
	run.byteEndPositions.assign(run.text.size(), 0.0);
	run.caretStops = {0};

	if (characters.empty()) {
		return;
	}

	// Total advance per unit (sum of glyphs whose cluster falls in the unit).
	std::vector<XYPOSITION> advanceByUnit(units.size(), 0.0);
	for (const ShapedGlyph &glyph : run.glyphs) {
		for (size_t u = 0; u < units.size(); u++) {
			const InputCharacter &first = characters[units[u].charBegin];
			const InputCharacter &last = characters[units[u].charEnd - 1];
			const size_t unitBegin = first.byteOffset;
			const size_t unitEnd = last.byteOffset + last.byteLength;
			if (glyph.cluster >= unitBegin && glyph.cluster < unitEnd) {
				advanceByUnit[u] += glyph.xAdvance;
				break;
			}
		}
	}

	XYPOSITION cumulative = 0.0;
	for (size_t u = 0; u < units.size(); u++) {
		cumulative += advanceByUnit[u];
		const InputCharacter &first = characters[units[u].charBegin];
		const InputCharacter &last = characters[units[u].charEnd - 1];
		const size_t unitBegin = first.byteOffset;
		const size_t unitEnd = last.byteOffset + last.byteLength;
		for (size_t b = unitBegin; b < unitEnd; b++) {
			run.byteEndPositions[b] = cumulative;
		}
		if (unitBegin != 0) {
			run.caretStops.push_back(unitBegin);
		}
	}
	std::sort(run.caretStops.begin(), run.caretStops.end());
	run.caretStops.erase(std::unique(run.caretStops.begin(), run.caretStops.end()), run.caretStops.end());
	if (run.caretStops.back() != run.text.size()) {
		run.caretStops.push_back(run.text.size());
	}
}

}

ShapedRun ShapeText(
	std::string_view text,
	const std::shared_ptr<FontFace> &primary,
	const FontFallback &fallback) {
	ShapedRun run;
	run.text = std::string(text);
	run.direction = TextDirection::LeftToRight;

	if (text.empty()) {
		run.caretStops = {0};
		return run;
	}
	if (!primary) {
		throw std::invalid_argument("ShapeText requires a primary face");
	}

	const std::vector<InputCharacter> characters = SplitCharacters(text);
	std::vector<char32_t> codePoints;
	codePoints.reserve(characters.size());
	for (const InputCharacter &ch : characters) {
		codePoints.push_back(ch.codePoint);
	}
	const std::vector<EmojiUnit> units = SegmentEmojiUnits(codePoints.data(), codePoints.size());

	// Group consecutive units that share a face into shaping spans so ordinary
	// Latin runs still shape together, while emoji units stay unsplit.
	size_t unitIndex = 0;
	while (unitIndex < units.size()) {
		const EmojiUnit &unit = units[unitIndex];
		const size_t unitLen = unit.charEnd - unit.charBegin;
		std::vector<char32_t> unitPoints(
			codePoints.begin() + static_cast<std::ptrdiff_t>(unit.charBegin),
			codePoints.begin() + static_cast<std::ptrdiff_t>(unit.charEnd));
		const std::shared_ptr<FontFace> unitFace =
			fallback.Select(primary, unitPoints.data(), unitPoints.size());

		// Extend the span with following single-character units on the same face
		// so "AB" still shapes as one HarfBuzz run. Multi-code-point emoji units
		// always shape alone so their sequence is not merged with neighbours.
		size_t spanUnitEnd = unitIndex + 1;
		if (unitLen == 1) {
			while (spanUnitEnd < units.size()) {
				const EmojiUnit &next = units[spanUnitEnd];
				if (next.charEnd - next.charBegin != 1) {
					break;
				}
				const char32_t nextCp = codePoints[next.charBegin];
				if (fallback.Select(primary, &nextCp, 1) != unitFace) {
					break;
				}
				spanUnitEnd++;
			}
		}

		std::vector<InputCharacter> span(
			characters.begin() + static_cast<std::ptrdiff_t>(unit.charBegin),
			characters.begin() + static_cast<std::ptrdiff_t>(units[spanUnitEnd - 1].charEnd));
		const bool multiCodePointEmoji = unitLen > 1 && spanUnitEnd == unitIndex + 1;
		ShapeSpan(span, unitFace, characters[unit.charBegin].byteOffset,
			multiCodePointEmoji, run.glyphs);
		unitIndex = spanUnitEnd;
	}

	FinishRun(run, characters, units);
	return run;
}

void FillMeasureWidths(const ShapedRun &run, XYPOSITION *positions) noexcept {
	if (!positions || run.byteEndPositions.empty()) {
		return;
	}
	std::copy(run.byteEndPositions.begin(), run.byteEndPositions.end(), positions);
}

namespace {

std::shared_ptr<const ShapedRun> ShapeOrCache(
	std::string_view text,
	const std::shared_ptr<FontFace> &primary,
	const FontFallback &fallback,
	ShapedRunCache *cache) {
	if (cache) {
		return cache->Get(text, primary, fallback);
	}
	return std::make_shared<const ShapedRun>(ShapeText(text, primary, fallback));
}

}

void MeasureWidthsShaped(
	std::string_view text,
	const std::shared_ptr<FontFace> &primary,
	const FontFallback &fallback,
	XYPOSITION *positions,
	ShapedRunCache *cache) {
	if (!positions || text.empty()) {
		return;
	}
	const auto run = ShapeOrCache(text, primary, fallback, cache);
	FillMeasureWidths(*run, positions);
}

XYPOSITION WidthTextShaped(
	std::string_view text,
	const std::shared_ptr<FontFace> &primary,
	const FontFallback &fallback,
	ShapedRunCache *cache) {
	if (text.empty()) {
		return 0.0;
	}
	return ShapeOrCache(text, primary, fallback, cache)->Width();
}

class ShapedRunCache::Impl {
public:
	explicit Impl(size_t capacity_) : capacity(std::max<size_t>(1, capacity_)) {
	}

	std::shared_ptr<const ShapedRun> Get(
		std::string_view text,
		const std::shared_ptr<FontFace> &primary,
		const FontFallback &fallback) {
		const std::string key = CacheKey(text, primary, fallback, TextDirection::LeftToRight);
		if (const auto found = map.find(key); found != map.end()) {
			// Move hit to the front of the LRU list.
			order.splice(order.begin(), order, found->second);
			return found->second->run;
		}

		auto shaped = std::make_shared<ShapedRun>(ShapeText(text, primary, fallback));
		order.push_front(Entry{key, std::move(shaped), primary, fallback});
		map[key] = order.begin();

		while (order.size() > capacity) {
			map.erase(order.back().key);
			order.pop_back();
		}
		return order.front().run;
	}

	void Clear() noexcept {
		map.clear();
		order.clear();
	}

	size_t Size() const noexcept {
		return order.size();
	}

	size_t Capacity() const noexcept {
		return capacity;
	}

private:
	struct Entry {
		std::string key;
		std::shared_ptr<const ShapedRun> run;
		std::shared_ptr<FontFace> primary;
		FontFallback fallback;
	};

	size_t capacity;
	std::list<Entry> order;
	std::unordered_map<std::string, std::list<Entry>::iterator> map;
};

ShapedRunCache::ShapedRunCache(size_t capacity) : impl(std::make_unique<Impl>(capacity)) {
}

ShapedRunCache::~ShapedRunCache() = default;

std::shared_ptr<const ShapedRun> ShapedRunCache::Get(
	std::string_view text,
	const std::shared_ptr<FontFace> &primary,
	const FontFallback &fallback) {
	return impl->Get(text, primary, fallback);
}

void ShapedRunCache::Clear() noexcept {
	impl->Clear();
}

size_t ShapedRunCache::Size() const noexcept {
	return impl->Size();
}

size_t ShapedRunCache::Capacity() const noexcept {
	return impl->Capacity();
}

}
