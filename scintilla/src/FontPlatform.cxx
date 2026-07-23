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

void AddRequest(FcPattern *pattern, const FontParameters &parameters) {
	if (parameters.faceName && parameters.faceName[0]) {
		const char *family = parameters.faceName[0] == '!' ? parameters.faceName + 1 : parameters.faceName;
		FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8 *>(family));
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
		double size_, FontWeight weight_, bool italic_) :
		libraryOwner(std::move(libraryOwner_)), pattern(pattern_, FcPatternDestroy), face(face_),
		path(std::move(path_)), size(size_), weight(weight_), italic(italic_) {
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
	double size;
	FontWeight weight;
	bool italic;
};

FontFace::FontFace(std::shared_ptr<void> libraryOwner, void *pattern, void *face,
	std::filesystem::path path, double size, FontWeight weight, bool italic) :
	impl(std::make_unique<Impl>(std::move(libraryOwner), static_cast<FcPattern *>(pattern),
		static_cast<FT_Face>(face), std::move(path), size, weight, italic)) {
}

FontFace::~FontFace() noexcept = default;

const std::filesystem::path &FontFace::Path() const noexcept {
	return impl->path;
}

std::string FontFace::Family() const {
	return impl->face->family_name ? impl->face->family_name : "";
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
		const FT_F26Dot6 height = static_cast<FT_F26Dot6>(std::lround(std::max(1.0, parameters.size) * 64.0));
		if (FT_Set_Char_Size(face, 0, height, 72, 72)) {
			FT_Done_Face(face);
			if (pattern) {
				FcPatternDestroy(pattern);
			}
			throw std::runtime_error("FreeType could not size font: " + path.string());
		}

		auto selected = std::make_shared<FontFace>(library, pattern, face, path, parameters.size,
			parameters.weight, parameters.italic);
		faces.emplace(key, selected);
		return selected;
	}

	std::shared_ptr<FreeTypeLibrary> library;
	std::unique_ptr<FcConfig, decltype(&FcConfigDestroy)> config;
	std::unordered_map<std::string, std::shared_ptr<FontFace>> faces;
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

std::shared_ptr<FontFace> FontCache::MatchFallback(const FontParameters &parameters, char32_t character) {
	PatternPtr request(FcPatternCreate(), FcPatternDestroy);
	std::unique_ptr<FcCharSet, decltype(&FcCharSetDestroy)> characters(FcCharSetCreate(), FcCharSetDestroy);
	if (!request || !characters || !FcCharSetAddChar(characters.get(), static_cast<FcChar32>(character))) {
		throw std::runtime_error("Fontconfig fallback pattern allocation failed");
	}
	AddRequest(request.get(), parameters);
	FcPatternAddCharSet(request.get(), FC_CHARSET, characters.get());
	FcConfigSubstitute(impl->config.get(), request.get(), FcMatchPattern);
	FcDefaultSubstitute(request.get());
	FcResult result = FcResultNoMatch;
	FcPattern *match = FcFontMatch(impl->config.get(), request.get(), &result);
	if (!match || result == FcResultNoMatch) {
		if (match) {
			FcPatternDestroy(match);
		}
		throw std::runtime_error("Fontconfig found no fallback font");
	}
	FcChar8 *file = nullptr;
	int index = 0;
	if (FcPatternGetString(match, FC_FILE, 0, &file) != FcResultMatch ||
		FcPatternGetInteger(match, FC_INDEX, 0, &index) != FcResultMatch) {
		FcPatternDestroy(match);
		throw std::runtime_error("Fontconfig fallback has no font file");
	}
	auto fallback = impl->Load(reinterpret_cast<const char *>(file), index, match, parameters);
	if (!fallback->HasGlyph(character)) {
		throw std::runtime_error("Fontconfig fallback lacks the requested glyph");
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
