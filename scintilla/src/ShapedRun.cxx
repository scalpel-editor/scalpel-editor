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

std::shared_ptr<FontFace> FaceForCharacter(
	char32_t codePoint,
	const std::shared_ptr<FontFace> &primary,
	const std::vector<std::shared_ptr<FontFace>> &fallbacks) {
	if (primary && primary->HasGlyph(codePoint)) {
		return primary;
	}
	for (const std::shared_ptr<FontFace> &fallback : fallbacks) {
		if (fallback && fallback->HasGlyph(codePoint)) {
			return fallback;
		}
	}
	return primary;
}

std::string FaceIdentity(const FontFace *face) {
	return std::to_string(reinterpret_cast<std::uintptr_t>(face));
}

std::string CacheKey(
	std::string_view text,
	const std::shared_ptr<FontFace> &primary,
	const std::vector<std::shared_ptr<FontFace>> &fallbacks,
	TextDirection direction) {
	std::string key = FaceIdentity(primary.get());
	key.push_back('|');
	for (const std::shared_ptr<FontFace> &fallback : fallbacks) {
		key.append(FaceIdentity(fallback.get()));
		key.push_back(';');
	}
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
	// The editor currently supports English shaping only.
	hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
	hb_buffer_set_script(buffer, HB_SCRIPT_LATIN);
	hb_buffer_set_language(buffer, hb_language_from_string("en", -1));
	hb_buffer_set_cluster_level(buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);

	for (const InputCharacter &ch : spanCharacters) {
		// cluster is the original byte offset so fallback splits keep one address space.
		hb_buffer_add(buffer, static_cast<hb_codepoint_t>(ch.codePoint),
			static_cast<unsigned int>(ch.byteOffset));
	}

	unsigned int featureCount = 0;
	const hb_feature_t *features = LigatureFeatures(featureCount);
	hb_shape(hbFont, buffer, features, featureCount);

	const unsigned int glyphCount = hb_buffer_get_length(buffer);
	const hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, nullptr);
	const hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, nullptr);

	outGlyphs.reserve(outGlyphs.size() + glyphCount);
	for (unsigned int g = 0; g < glyphCount; g++) {
		ShapedGlyph glyph;
		glyph.glyphId = infos[g].codepoint;
		glyph.xAdvance = FromHarfBuzz(positions[g].x_advance);
		glyph.yAdvance = FromHarfBuzz(positions[g].y_advance);
		glyph.xOffset = FromHarfBuzz(positions[g].x_offset);
		glyph.yOffset = FromHarfBuzz(positions[g].y_offset);
		glyph.cluster = infos[g].cluster;
		glyph.face = face;
		outGlyphs.push_back(glyph);
	}

	hb_buffer_destroy(buffer);
}

// Map glyph advances onto per-byte end positions and caret stops.
void FinishRun(ShapedRun &run, const std::vector<InputCharacter> &characters) {
	run.byteEndPositions.assign(run.text.size(), 0.0);
	run.caretStops = {0};

	if (characters.empty()) {
		return;
	}

	// Total advance per input cluster (byte offset of character start).
	std::unordered_map<size_t, XYPOSITION> advanceByCluster;
	for (const ShapedGlyph &glyph : run.glyphs) {
		advanceByCluster[glyph.cluster] += glyph.xAdvance;
		if (glyph.cluster != 0 && glyph.cluster < run.text.size()) {
			run.caretStops.push_back(glyph.cluster);
		}
	}
	std::sort(run.caretStops.begin(), run.caretStops.end());
	run.caretStops.erase(std::unique(run.caretStops.begin(), run.caretStops.end()), run.caretStops.end());

	XYPOSITION cumulative = 0.0;
	for (const InputCharacter &ch : characters) {
		cumulative += advanceByCluster[ch.byteOffset];
		for (size_t b = 0; b < ch.byteLength; b++) {
			run.byteEndPositions[ch.byteOffset + b] = cumulative;
		}
	}
	if (run.caretStops.back() != run.text.size()) {
		run.caretStops.push_back(run.text.size());
	}
}

}

ShapedRun ShapeText(
	std::string_view text,
	const std::shared_ptr<FontFace> &primary,
	const std::vector<std::shared_ptr<FontFace>> &fallbacks) {
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

	// Group consecutive characters that share a face into spans for shaping.
	size_t spanStart = 0;
	while (spanStart < characters.size()) {
		const std::shared_ptr<FontFace> spanFace =
			FaceForCharacter(characters[spanStart].codePoint, primary, fallbacks);
		size_t spanEnd = spanStart + 1;
		while (spanEnd < characters.size()) {
			const std::shared_ptr<FontFace> nextFace =
				FaceForCharacter(characters[spanEnd].codePoint, primary, fallbacks);
			if (nextFace != spanFace) {
				break;
			}
			spanEnd++;
		}
		std::vector<InputCharacter> span(
			characters.begin() + static_cast<std::ptrdiff_t>(spanStart),
			characters.begin() + static_cast<std::ptrdiff_t>(spanEnd));
		ShapeSpan(span, spanFace, run.glyphs);
		spanStart = spanEnd;
	}

	FinishRun(run, characters);
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
	const std::vector<std::shared_ptr<FontFace>> &fallbacks,
	ShapedRunCache *cache) {
	if (cache) {
		return cache->Get(text, primary, fallbacks);
	}
	return std::make_shared<const ShapedRun>(ShapeText(text, primary, fallbacks));
}

}

void MeasureWidthsShaped(
	std::string_view text,
	const std::shared_ptr<FontFace> &primary,
	const std::vector<std::shared_ptr<FontFace>> &fallbacks,
	XYPOSITION *positions,
	ShapedRunCache *cache) {
	if (!positions || text.empty()) {
		return;
	}
	const auto run = ShapeOrCache(text, primary, fallbacks, cache);
	FillMeasureWidths(*run, positions);
}

XYPOSITION WidthTextShaped(
	std::string_view text,
	const std::shared_ptr<FontFace> &primary,
	const std::vector<std::shared_ptr<FontFace>> &fallbacks,
	ShapedRunCache *cache) {
	if (text.empty()) {
		return 0.0;
	}
	return ShapeOrCache(text, primary, fallbacks, cache)->Width();
}

class ShapedRunCache::Impl {
public:
	explicit Impl(size_t capacity_) : capacity(std::max<size_t>(1, capacity_)) {
	}

	std::shared_ptr<const ShapedRun> Get(
		std::string_view text,
		const std::shared_ptr<FontFace> &primary,
		const std::vector<std::shared_ptr<FontFace>> &fallbacks) {
		const std::string key = CacheKey(text, primary, fallbacks, TextDirection::LeftToRight);
		if (const auto found = map.find(key); found != map.end()) {
			// Move hit to the front of the LRU list.
			order.splice(order.begin(), order, found->second);
			return found->second->run;
		}

		auto shaped = std::make_shared<ShapedRun>(ShapeText(text, primary, fallbacks));
		order.push_front(Entry{key, std::move(shaped), primary, fallbacks});
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
		std::vector<std::shared_ptr<FontFace>> fallbacks;
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
	const std::vector<std::shared_ptr<FontFace>> &fallbacks) {
	return impl->Get(text, primary, fallbacks);
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
