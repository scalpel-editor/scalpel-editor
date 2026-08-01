// scalpel-editor font lookup and FreeType face ownership.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>

#include "EditorStyleTypes.h"
#include "EmojiSequence.h"
#include "FontPlatform.h"
#include "Platform.h"

namespace Scintilla::Internal {

namespace {

struct FreeTypeLibrary {
	FT_Library value = nullptr;

	FreeTypeLibrary() {
		if (FT_Init_FreeType(&value)) {
			throw std::runtime_error("FreeType initialization failed");
		}
	}

	~FreeTypeLibrary() noexcept {
		FT_Done_FreeType(value);
	}
};

using PatternPtr = std::unique_ptr<FcPattern, decltype(&FcPatternDestroy)>;

int FontconfigWidth(FontStretch stretch) noexcept {
	switch (stretch) {
	case FontStretch::UltraCondensed: return FC_WIDTH_ULTRACONDENSED;
	case FontStretch::ExtraCondensed: return FC_WIDTH_EXTRACONDENSED;
	case FontStretch::Condensed: return FC_WIDTH_CONDENSED;
	case FontStretch::SemiCondensed: return FC_WIDTH_SEMICONDENSED;
	case FontStretch::Normal: return FC_WIDTH_NORMAL;
	case FontStretch::SemiExpanded: return FC_WIDTH_SEMIEXPANDED;
	case FontStretch::Expanded: return FC_WIDTH_EXPANDED;
	case FontStretch::ExtraExpanded: return FC_WIDTH_EXTRAEXPANDED;
	case FontStretch::UltraExpanded: return FC_WIDTH_ULTRAEXPANDED;
	}
	return FC_WIDTH_NORMAL;
}

std::string RequestedFamilyFromParameters(const FontParameters &parameters) {
	// faceName is a literal family value for FC_FAMILY, not Fontconfig pattern
	// text. Do not run it through FcNameParse. A leading '!' is stripped for
	// compatibility with historical Scintilla face strings; the remaining bytes
	// (including hyphens in system-ui) are stored and passed to Fontconfig.
	if (!parameters.faceName || !parameters.faceName[0]) {
		return {};
	}
	const char *family = parameters.faceName[0] == '!' ? parameters.faceName + 1 : parameters.faceName;
	return family;
}

void AddRequest(FcPattern *pattern, const FontParameters &parameters) {
	const std::string family = RequestedFamilyFromParameters(parameters);
	if (!family.empty()) {
		FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8 *>(family.c_str()));
	}
	FcPatternAddInteger(pattern, FC_WEIGHT, FcWeightFromOpenType(static_cast<int>(parameters.weight)));
	FcPatternAddInteger(pattern, FC_SLANT, parameters.italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
	FcPatternAddInteger(pattern, FC_WIDTH, FontconfigWidth(parameters.stretch));
	FcPatternAddDouble(pattern, FC_PIXEL_SIZE, std::max(1.0, parameters.size));
}

std::string FaceKey(const std::filesystem::path &path, int index, const FontParameters &parameters) {
	return path.string() + ':' + std::to_string(index) + ':' + std::to_string(parameters.size) + ':' +
		std::to_string(static_cast<int>(parameters.weight)) + ':' + (parameters.italic ? "1" : "0") + ':' +
		std::to_string(static_cast<int>(parameters.stretch));
}

std::string FallbackDecisionKey(const FontParameters &parameters, char32_t character) {
	return RequestedFamilyFromParameters(parameters) + '|' +
		std::to_string(parameters.size) + '|' +
		std::to_string(static_cast<int>(parameters.weight)) + '|' +
		(parameters.italic ? '1' : '0') + '|' +
		std::to_string(static_cast<int>(parameters.stretch)) + '|' +
		std::to_string(static_cast<uint32_t>(character));
}

std::string FallbackDecisionKey(
	const FontParameters &parameters, const char32_t *codePoints, size_t count) {
	std::string key = RequestedFamilyFromParameters(parameters) + '|' +
		std::to_string(parameters.size) + '|' +
		std::to_string(static_cast<int>(parameters.weight)) + '|' +
		(parameters.italic ? '1' : '0') + '|' +
		std::to_string(static_cast<int>(parameters.stretch)) + "|seq:";
	for (size_t i = 0; i < count; i++) {
		if (i > 0) {
			key.push_back(',');
		}
		key.append(std::to_string(static_cast<uint32_t>(codePoints[i])));
	}
	return key;
}

bool SequenceHasRequiredCoverage(const FontFace &face, const char32_t *codePoints, size_t count) {
	if (!codePoints || count == 0) {
		return false;
	}
	for (size_t i = 0; i < count; i++) {
		if (IsEmojiCoverageIgnorable(codePoints[i])) {
			continue;
		}
		if (!face.HasGlyph(codePoints[i])) {
			return false;
		}
	}
	return true;
}

FontParameters ParametersFromFace(const FontFace &face) {
	return FontParameters(
		face.RequestedFamily().c_str(),
		face.RequestedSize(),
		face.RequestedWeight(),
		face.RequestedItalic(),
		FontQuality::QualityDefault,
		CharacterSet::Ansi,
		localeNameDefault,
		face.RequestedStretch());
}

class FontImpl final : public Font {
public:
	explicit FontImpl(std::shared_ptr<FontFace> face_) : face(std::move(face_)) {
	}

	std::shared_ptr<FontFace> face;
};

}

class FontFace::Impl {
public:
	Impl(std::shared_ptr<void> libraryOwner_, FcPattern *pattern_, FT_Face face_, std::filesystem::path path_,
		std::string requestedFamily_, double size_, FontWeight weight_, bool italic_, FontStretch stretch_) :
		libraryOwner(std::move(libraryOwner_)), pattern(pattern_, FcPatternDestroy), face(face_),
		path(std::move(path_)), requestedFamily(std::move(requestedFamily_)), size(size_), weight(weight_),
		italic(italic_), stretch(stretch_) {
		// Size must be set on the FT_Face before hb_ft_font_create_referenced.
		hbFont = hb_ft_font_create_referenced(face);
		hb_ft_font_set_load_flags(hbFont, FT_LOAD_DEFAULT);
	}

	~Impl() noexcept {
		// HarfBuzz font first: it may hold a reference on the FT_Face.
		if (hbFont) {
			hb_font_destroy(hbFont);
			hbFont = nullptr;
		}
		if (face) {
			FT_Done_Face(face);
			face = nullptr;
		}
	}

	// Declaration order: hb_font and FT_Face die before the FreeType library owner.
	std::shared_ptr<void> libraryOwner;
	PatternPtr pattern;
	hb_font_t *hbFont = nullptr;
	FT_Face face = nullptr;
	std::filesystem::path path;
	std::string requestedFamily;
	double size;
	FontWeight weight;
	bool italic;
	FontStretch stretch;
};

FontFace::FontFace(std::shared_ptr<void> libraryOwner, void *pattern, void *face,
	std::filesystem::path path, std::string requestedFamily, double size, FontWeight weight,
	bool italic, FontStretch stretch) :
	impl(std::make_unique<Impl>(std::move(libraryOwner), static_cast<FcPattern *>(pattern),
		static_cast<FT_Face>(face), std::move(path), std::move(requestedFamily), size, weight, italic,
		stretch)) {
}

FontFace::~FontFace() noexcept = default;

const std::filesystem::path &FontFace::Path() const noexcept {
	return impl->path;
}

std::string FontFace::Family() const {
	return impl->face->family_name ? impl->face->family_name : "";
}

const std::string &FontFace::RequestedFamily() const noexcept {
	return impl->requestedFamily;
}

double FontFace::RequestedSize() const noexcept {
	return impl->size;
}

FontWeight FontFace::RequestedWeight() const noexcept {
	return impl->weight;
}

bool FontFace::RequestedItalic() const noexcept {
	return impl->italic;
}

FontStretch FontFace::RequestedStretch() const noexcept {
	return impl->stretch;
}

FontMetrics FontFace::Metrics() const noexcept {
	const FT_Size_Metrics &metrics = impl->face->size->metrics;
	const double ascent = metrics.ascender / 64.0;
	const double descent = -metrics.descender / 64.0;
	const double height = metrics.height / 64.0;
	return {ascent, descent, height, std::max(0.0, height - ascent - descent)};
}

bool FontFace::HasGlyph(char32_t character) const noexcept {
	return FT_Get_Char_Index(impl->face, static_cast<FT_ULong>(character)) != 0;
}

bool FontFace::ShapesSequence(const char32_t *codePoints, size_t count) const {
	if (!codePoints || count == 0 || !impl->hbFont) {
		return false;
	}
	hb_buffer_t *buffer = hb_buffer_create();
	if (!buffer) {
		return false;
	}
	hb_buffer_set_content_type(buffer, HB_BUFFER_CONTENT_TYPE_UNICODE);
	hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
	hb_buffer_set_cluster_level(buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
	for (size_t i = 0; i < count; i++) {
		hb_buffer_add(buffer, static_cast<hb_codepoint_t>(codePoints[i]),
			static_cast<unsigned int>(i));
	}
	hb_buffer_guess_segment_properties(buffer);
	// Keep editor line ordering left-to-right even when script detection differs.
	hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
	hb_shape(impl->hbFont, buffer, nullptr, 0);

	const unsigned int glyphCount = hb_buffer_get_length(buffer);
	const hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, nullptr);
	const hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer, nullptr);
	bool sawInk = false;
	bool clean = true;
	for (unsigned int g = 0; g < glyphCount; g++) {
		const bool missing = infos[g].codepoint == 0;
		const bool hasAdvance = positions[g].x_advance != 0 || positions[g].y_advance != 0;
		if (missing && hasAdvance) {
			clean = false;
			break;
		}
		if (!missing) {
			sawInk = true;
		}
	}
	hb_buffer_destroy(buffer);
	return clean && sawInk;
}

GlyphImage FontFace::RasterizeGlyph(uint32_t glyphId) const {
	GlyphImage image;
	if (FT_Load_Glyph(impl->face, static_cast<FT_UInt>(glyphId), FT_LOAD_DEFAULT) != 0) {
		return image;
	}
	if (FT_Render_Glyph(impl->face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
		return image;
	}
	const FT_Bitmap &bitmap = impl->face->glyph->bitmap;
	image.left = impl->face->glyph->bitmap_left;
	image.top = impl->face->glyph->bitmap_top;
	image.width = static_cast<int>(bitmap.width);
	image.height = static_cast<int>(bitmap.rows);
	if (image.width <= 0 || image.height <= 0 || !bitmap.buffer) {
		image.width = 0;
		image.height = 0;
		return image;
	}
	image.gray.resize(static_cast<size_t>(image.width) * static_cast<size_t>(image.height));
	const int pitch = bitmap.pitch;
	const uint8_t *src = bitmap.buffer;
	// FreeType pitch may be negative (bottom-up). Copy into top-down rows.
	if (pitch >= 0) {
		for (int y = 0; y < image.height; y++) {
			std::memcpy(image.gray.data() + static_cast<size_t>(y) * static_cast<size_t>(image.width),
				src + y * pitch, static_cast<size_t>(image.width));
		}
	} else {
		const int absPitch = -pitch;
		for (int y = 0; y < image.height; y++) {
			std::memcpy(image.gray.data() + static_cast<size_t>(y) * static_cast<size_t>(image.width),
				src + (image.height - 1 - y) * absPitch, static_cast<size_t>(image.width));
		}
	}
	return image;
}

void *FontFace::HarfBuzzFont() const noexcept {
	return impl->hbFont;
}

class FontCache::Impl {
public:
	Impl() : library(std::make_shared<FreeTypeLibrary>()), config(FcInitLoadConfigAndFonts(), FcConfigDestroy) {
		if (!config) {
			throw std::runtime_error("Fontconfig initialization failed");
		}
	}

	std::shared_ptr<FontFace> Load(const std::filesystem::path &path, int index,
		FcPattern *pattern, const FontParameters &parameters) {
		const std::string key = FaceKey(path, index, parameters);
		if (const auto found = faces.find(key); found != faces.end()) {
			if (pattern) {
				FcPatternDestroy(pattern);
			}
			return found->second;
		}

		FT_Face face = nullptr;
		if (FT_New_Face(library->value, path.c_str(), index, &face)) {
			if (pattern) {
				FcPatternDestroy(pattern);
			}
			throw std::runtime_error("FreeType could not open font: " + path.string());
		}
		const double requestedPixels = std::max(1.0, parameters.size);
		const FT_F26Dot6 height = static_cast<FT_F26Dot6>(std::lround(requestedPixels * 64.0));
		// Outline faces use the requested pixel size. CBDT/CBLC and other
		// bitmap-only faces expose fixed strikes; pick the closest y_ppem and
		// keep parameters.size as the logical request for drawing scale.
		if (FT_Set_Char_Size(face, 0, height, 72, 72) != 0) {
			if (face->num_fixed_sizes <= 0) {
				FT_Done_Face(face);
				if (pattern) {
					FcPatternDestroy(pattern);
				}
				throw std::runtime_error("FreeType could not size font: " + path.string());
			}
			int bestStrike = 0;
			double bestDiff = 1e300;
			for (int strike = 0; strike < face->num_fixed_sizes; strike++) {
				const double ppem = face->available_sizes[strike].y_ppem / 64.0;
				const double diff = std::abs(ppem - requestedPixels);
				if (diff < bestDiff) {
					bestDiff = diff;
					bestStrike = strike;
				}
			}
			if (FT_Select_Size(face, bestStrike) != 0) {
				FT_Done_Face(face);
				if (pattern) {
					FcPatternDestroy(pattern);
				}
				throw std::runtime_error("FreeType could not select bitmap strike: " + path.string());
			}
		}

		auto selected = std::make_shared<FontFace>(library, pattern, face, path,
			RequestedFamilyFromParameters(parameters), parameters.size, parameters.weight,
			parameters.italic, parameters.stretch);
		faces.emplace(key, selected);
		return selected;
	}

	/**
	 * Walk Fontconfig's ordered candidates for the request plus charset.
	 * accept() chooses the first usable loaded face. Never throws for a miss.
	 */
	template <typename Accept>
	std::shared_ptr<FontFace> ResolveForRequest(
		const FontParameters &parameters,
		const std::string &decisionKey,
		const char32_t *codePoints,
		size_t count,
		Accept accept) {
		if (const auto found = fallbackDecisions.find(decisionKey); found != fallbackDecisions.end()) {
			return found->second;
		}

		std::shared_ptr<FontFace> resolved;
		PatternPtr request(FcPatternCreate(), FcPatternDestroy);
		std::unique_ptr<FcCharSet, decltype(&FcCharSetDestroy)> characters(FcCharSetCreate(), FcCharSetDestroy);
		if (!request || !characters || !codePoints || count == 0) {
			fallbackDecisions.emplace(decisionKey, resolved);
			return resolved;
		}
		for (size_t i = 0; i < count; i++) {
			if (IsEmojiCoverageIgnorable(codePoints[i])) {
				continue;
			}
			if (!FcCharSetAddChar(characters.get(), static_cast<FcChar32>(codePoints[i]))) {
				fallbackDecisions.emplace(decisionKey, resolved);
				return resolved;
			}
		}
		AddRequest(request.get(), parameters);
		FcPatternAddCharSet(request.get(), FC_CHARSET, characters.get());
		FcConfigSubstitute(config.get(), request.get(), FcMatchPattern);
		FcDefaultSubstitute(request.get());

		FcResult result = FcResultNoMatch;
		// trim=true drops later fonts whose coverage is a subset of earlier ones.
		FcFontSet *sorted = FcFontSort(config.get(), request.get(), FcTrue, nullptr, &result);
		if (!sorted) {
			fallbackDecisions.emplace(decisionKey, resolved);
			return resolved;
		}

		for (int i = 0; i < sorted->nfont; i++) {
			FcPattern *prepared = FcFontRenderPrepare(config.get(), request.get(), sorted->fonts[i]);
			if (!prepared) {
				continue;
			}
			FcChar8 *file = nullptr;
			int index = 0;
			if (FcPatternGetString(prepared, FC_FILE, 0, &file) != FcResultMatch ||
				FcPatternGetInteger(prepared, FC_INDEX, 0, &index) != FcResultMatch ||
				!file) {
				FcPatternDestroy(prepared);
				continue;
			}
			try {
				// Load always takes ownership of prepared (cache hit, face, or throw).
				auto candidate = Load(reinterpret_cast<const char *>(file), index, prepared, parameters);
				if (candidate && accept(*candidate)) {
					resolved = std::move(candidate);
					break;
				}
			} catch (const std::exception &) {
				// Skip unloadable candidates; try the next Fontconfig result.
			}
		}
		FcFontSetDestroy(sorted);
		fallbackDecisions.emplace(decisionKey, resolved);
		return resolved;
	}

	std::shared_ptr<FontFace> ResolveForRequest(const FontParameters &parameters, char32_t character) {
		const char32_t codePoints[1] = {character};
		return ResolveForRequest(
			parameters,
			FallbackDecisionKey(parameters, character),
			codePoints,
			1,
			[character](const FontFace &face) { return face.HasGlyph(character); });
	}

	std::shared_ptr<FontFace> ResolveForRequest(
		const FontParameters &parameters, const char32_t *codePoints, size_t count) {
		return ResolveForRequest(
			parameters,
			FallbackDecisionKey(parameters, codePoints, count),
			codePoints,
			count,
			[codePoints, count](const FontFace &face) {
				return SequenceHasRequiredCoverage(face, codePoints, count) &&
					face.ShapesSequence(codePoints, count);
			});
	}

	std::shared_ptr<FreeTypeLibrary> library;
	std::unique_ptr<FcConfig, decltype(&FcConfigDestroy)> config;
	std::unordered_map<std::string, std::shared_ptr<FontFace>> faces;
	// Empty shared_ptr values mean "no covering face" for this request.
	std::unordered_map<std::string, std::shared_ptr<FontFace>> fallbackDecisions;
};

FontCache::FontCache() : impl(std::make_unique<Impl>()) {
}

FontCache::~FontCache() noexcept = default;

std::shared_ptr<FontFace> FontCache::Match(const FontParameters &parameters) {
	PatternPtr request(FcPatternCreate(), FcPatternDestroy);
	if (!request) {
		throw std::runtime_error("Fontconfig pattern allocation failed");
	}
	AddRequest(request.get(), parameters);
	FcConfigSubstitute(impl->config.get(), request.get(), FcMatchPattern);
	FcDefaultSubstitute(request.get());
	FcResult result = FcResultNoMatch;
	FcPattern *match = FcFontMatch(impl->config.get(), request.get(), &result);
	if (!match || result == FcResultNoMatch) {
		if (match) {
			FcPatternDestroy(match);
		}
		throw std::runtime_error("Fontconfig found no usable font");
	}
	FcChar8 *file = nullptr;
	int index = 0;
	if (FcPatternGetString(match, FC_FILE, 0, &file) != FcResultMatch ||
		FcPatternGetInteger(match, FC_INDEX, 0, &index) != FcResultMatch) {
		FcPatternDestroy(match);
		throw std::runtime_error("Fontconfig match has no font file");
	}
	return impl->Load(reinterpret_cast<const char *>(file), index, match, parameters);
}

std::shared_ptr<FontFace> FontCache::ResolveFallback(const FontParameters &parameters, char32_t character) {
	return impl->ResolveForRequest(parameters, character);
}

std::shared_ptr<FontFace> FontCache::ResolveFallback(const FontFace &primary, char32_t character) {
	// ParametersFromFace borrows RequestedFamily().c_str(); the string is owned
	// by the face and stays alive for this call and for AddRequest's copy.
	return ResolveFallback(ParametersFromFace(primary), character);
}

std::shared_ptr<FontFace> FontCache::ResolveFallback(
	const FontParameters &parameters, const char32_t *codePoints, size_t count) {
	return impl->ResolveForRequest(parameters, codePoints, count);
}

std::shared_ptr<FontFace> FontCache::ResolveFallback(
	const FontFace &primary, const char32_t *codePoints, size_t count) {
	return ResolveFallback(ParametersFromFace(primary), codePoints, count);
}

std::shared_ptr<FontFace> FontCache::MatchFallback(const FontParameters &parameters, char32_t character) {
	auto fallback = ResolveFallback(parameters, character);
	if (!fallback) {
		throw std::runtime_error("Fontconfig found no fallback font covering the character");
	}
	return fallback;
}

std::shared_ptr<FontFace> FontCache::LoadPath(
	const std::filesystem::path &path, const FontParameters &parameters) {
	if (!path.is_absolute()) {
		throw std::invalid_argument("test font path must be absolute");
	}
	return impl->Load(path, 0, nullptr, parameters);
}

std::vector<std::shared_ptr<FontFace>> FontCache::LoadPaths(
	const std::vector<std::filesystem::path> &paths, const FontParameters &parameters) {
	std::vector<std::shared_ptr<FontFace>> loaded;
	loaded.reserve(paths.size());
	for (const std::filesystem::path &path : paths) {
		loaded.push_back(LoadPath(path, parameters));
	}
	return loaded;
}

namespace {

FontCache &SharedFontCache() {
	static FontCache cache;
	return cache;
}

std::filesystem::path testPrimaryPath;
std::vector<std::filesystem::path> testFallbackPaths;

}

FontFallback::FontFallback(std::initializer_list<std::shared_ptr<FontFace>> faces) :
	fixedFaces(faces) {
}

FontFallback::FontFallback(std::vector<std::shared_ptr<FontFace>> faces) :
	fixedFaces(std::move(faces)) {
}

FontFallback FontFallback::Fixed(std::vector<std::shared_ptr<FontFace>> faces) {
	return FontFallback(std::move(faces));
}

FontFallback FontFallback::Production() {
	return Production(SharedFontCache());
}

FontFallback FontFallback::Production(FontCache &cache) {
	FontFallback fallback;
	fallback.cache = &cache;
	return fallback;
}

std::shared_ptr<FontFace> FontFallback::Select(
	const std::shared_ptr<FontFace> &primary,
	char32_t character) const {
	if (primary && primary->HasGlyph(character)) {
		return primary;
	}
	for (const std::shared_ptr<FontFace> &face : fixedFaces) {
		if (face && face->HasGlyph(character)) {
			return face;
		}
	}
	if (cache && primary) {
		if (std::shared_ptr<FontFace> resolved = cache->ResolveFallback(*primary, character)) {
			return resolved;
		}
	}
	return primary;
}

std::shared_ptr<FontFace> FontFallback::Select(
	const std::shared_ptr<FontFace> &primary,
	const char32_t *codePoints, size_t count) const {
	if (!codePoints || count == 0) {
		return primary;
	}
	if (count == 1) {
		return Select(primary, codePoints[0]);
	}
	if (primary && primary->ShapesSequence(codePoints, count)) {
		return primary;
	}
	for (const std::shared_ptr<FontFace> &face : fixedFaces) {
		if (face && face->ShapesSequence(codePoints, count)) {
			return face;
		}
	}
	if (cache && primary) {
		if (std::shared_ptr<FontFace> resolved = cache->ResolveFallback(*primary, codePoints, count)) {
			return resolved;
		}
	}
	return primary;
}

bool FontFallback::Empty() const noexcept {
	return fixedFaces.empty() && cache == nullptr;
}

bool FontFallback::UsesProductionResolver() const noexcept {
	return cache != nullptr;
}

const std::vector<std::shared_ptr<FontFace>> &FontFallback::FixedFaces() const noexcept {
	return fixedFaces;
}

FontCache *FontFallback::ResolverCache() const noexcept {
	return cache;
}

std::string FontFallback::CacheIdentity() const {
	std::string identity;
	if (cache) {
		identity.append("P:");
		identity.append(std::to_string(reinterpret_cast<std::uintptr_t>(cache)));
	} else {
		identity.push_back('F');
	}
	identity.push_back('|');
	for (const std::shared_ptr<FontFace> &face : fixedFaces) {
		identity.append(std::to_string(reinterpret_cast<std::uintptr_t>(face.get())));
		identity.push_back(';');
	}
	return identity;
}

void UseTestFontPaths(const std::filesystem::path &primary,
	const std::vector<std::filesystem::path> &fallbacks) {
	testPrimaryPath = primary;
	testFallbackPaths = fallbacks;
}

void ClearTestFontPaths() noexcept {
	testPrimaryPath.clear();
	testFallbackPaths.clear();
}

bool TestFontPathsActive() noexcept {
	return !testPrimaryPath.empty();
}

std::vector<std::shared_ptr<FontFace>> TestFontFallbackFaces(double size) {
	std::vector<std::shared_ptr<FontFace>> faces;
	if (testFallbackPaths.empty()) {
		return faces;
	}
	const FontParameters parameters("fixture", size);
	faces.reserve(testFallbackPaths.size());
	for (const std::filesystem::path &path : testFallbackPaths) {
		faces.push_back(SharedFontCache().LoadPath(path, parameters));
	}
	return faces;
}

FontFallback DefaultSurfaceFallback(double size) {
	if (TestFontPathsActive()) {
		return FontFallback::Fixed(TestFontFallbackFaces(size));
	}
	return FontFallback::Production();
}

std::shared_ptr<Font> Font::Allocate(const FontParameters &parameters) {
	if (!testPrimaryPath.empty()) {
		return std::make_shared<FontImpl>(SharedFontCache().LoadPath(testPrimaryPath, parameters));
	}
	return std::make_shared<FontImpl>(SharedFontCache().Match(parameters));
}

const FontFace *FaceFromFont(const Font *font) noexcept {
	const auto *concrete = dynamic_cast<const FontImpl *>(font);
	return concrete ? concrete->face.get() : nullptr;
}

std::shared_ptr<FontFace> SharedFaceFromFont(const Font *font) noexcept {
	const auto *concrete = dynamic_cast<const FontImpl *>(font);
	return concrete ? concrete->face : std::shared_ptr<FontFace>{};
}

std::shared_ptr<Font> FontFromFace(std::shared_ptr<FontFace> face) {
	if (!face) {
		throw std::invalid_argument("FontFromFace requires a face");
	}
	return std::make_shared<FontImpl>(std::move(face));
}

}
