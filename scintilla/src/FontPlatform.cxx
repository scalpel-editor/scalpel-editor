// scalpel-editor font lookup and FreeType face ownership.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
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

std::string FaceKey(const std::filesystem::path &path, int index, const FontParameters &parameters,
	const FontRasterPolicy &rasterPolicy) {
	return path.string() + ':' + std::to_string(index) + ':' +
		RequestedFamilyFromParameters(parameters) + ':' + std::to_string(parameters.size) + ':' +
		std::to_string(static_cast<int>(parameters.weight)) + ':' + (parameters.italic ? "1" : "0") + ':' +
		std::to_string(static_cast<int>(parameters.stretch)) + ':' +
		(rasterPolicy.antialias ? "1" : "0") + ':' +
		std::to_string(static_cast<int>(rasterPolicy.hintStyle));
}

[[nodiscard]] int32_t NormalizePhase26_6(int32_t value) noexcept {
	// Floor remainder into [0, 63]. C++11 % truncates toward zero for negatives.
	int32_t remainder = value % 64;
	if (remainder < 0) {
		remainder += 64;
	}
	return remainder;
}

[[nodiscard]] FT_F26Dot6 DeviceHeight26_6(double logicalPixels, RasterScale scale) noexcept {
	const double height =
		std::max(1.0, logicalPixels) * 64.0 *
		static_cast<double>(scale.Numerator()) /
		static_cast<double>(scale.Denominator());
	return static_cast<FT_F26Dot6>(std::lround(height));
}

/**
 * Apply a 26.6 translation delta for the duration of outline rasterization.
 * Restores the previous face transform so callers never leave mutable FreeType
 * state behind (independent of call order).
 */
class FaceTransformGuard {
public:
	FaceTransformGuard(FT_Face face_, FT_Pos phaseX, FT_Pos phaseY) noexcept :
		face(face_) {
		if (!face) {
			return;
		}
		FT_Get_Transform(face, &savedMatrix, &savedDelta);
		FT_Vector delta{};
		delta.x = phaseX;
		delta.y = phaseY;
		// Identity matrix + phase delta. FreeType applies this after hinting
		// (ftobjs.c FT_Load_Glyph) and only to scalable outline images.
		FT_Set_Transform(face, nullptr, &delta);
	}

	~FaceTransformGuard() noexcept {
		if (!face) {
			return;
		}
		FT_Set_Transform(face, &savedMatrix, &savedDelta);
	}

	FaceTransformGuard(const FaceTransformGuard &) = delete;
	FaceTransformGuard &operator=(const FaceTransformGuard &) = delete;

private:
	FT_Face face = nullptr;
	FT_Matrix savedMatrix{};
	FT_Vector savedDelta{};
};

int FreeTypeLoadFlagsForPolicy(const FontRasterPolicy &policy) noexcept {
	// FT_LOAD_COLOR keeps CBDT/CBLC colour bitmaps. Hint target is mutually
	// exclusive among None / Slight / Normal; TARGET_NORMAL is zero bits.
	int flags = static_cast<int>(FT_LOAD_COLOR);
	switch (policy.hintStyle) {
	case FontHintStyle::None:
		flags |= static_cast<int>(FT_LOAD_NO_HINTING);
		break;
	case FontHintStyle::Slight:
		flags |= static_cast<int>(FT_LOAD_TARGET_LIGHT);
		break;
	case FontHintStyle::Normal:
		flags |= static_cast<int>(FT_LOAD_TARGET_NORMAL);
		break;
	}
	return flags;
}

std::string FallbackDecisionKey(const FontParameters &parameters, char32_t character,
	EmojiPresentation presentation) {
	return RequestedFamilyFromParameters(parameters) + '|' +
		std::to_string(parameters.size) + '|' +
		std::to_string(static_cast<int>(parameters.weight)) + '|' +
		(parameters.italic ? '1' : '0') + '|' +
		std::to_string(static_cast<int>(parameters.stretch)) + '|' +
		std::to_string(static_cast<uint32_t>(character)) + "|p:" +
		std::to_string(static_cast<int>(presentation));
}

std::string FallbackDecisionKey(
	const FontParameters &parameters, const char32_t *codePoints, size_t count,
	EmojiPresentation presentation) {
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
	key.append("|p:");
	key.append(std::to_string(static_cast<int>(presentation)));
	return key;
}

/** True when face satisfies a presentation colour preference. */
bool FaceMatchesPresentation(const FontFace &face, EmojiPresentation presentation) noexcept {
	switch (presentation) {
	case EmojiPresentation::Emoji:
		return face.HasColor();
	case EmojiPresentation::Text:
		return !face.HasColor();
	case EmojiPresentation::Unspecified:
		return true;
	}
	return true;
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

int FontRasterPolicy::FreeTypeLoadFlags() const noexcept {
	return FreeTypeLoadFlagsForPolicy(*this);
}

FontRasterPolicy RasterPolicyFromFontconfigPattern(const void *pattern) noexcept {
	// Documented defaults when properties are missing or invalid.
	FontRasterPolicy policy;
	if (!pattern) {
		return policy;
	}
	const FcPattern *fc = static_cast<const FcPattern *>(pattern);

	FcBool antialias = FcTrue;
	if (FcPatternGetBool(fc, FC_ANTIALIAS, 0, &antialias) == FcResultMatch) {
		policy.antialias = antialias != FcFalse;
	}

	FcBool hinting = FcTrue;
	const bool haveHinting = FcPatternGetBool(fc, FC_HINTING, 0, &hinting) == FcResultMatch;
	if (haveHinting && hinting == FcFalse) {
		policy.hintStyle = FontHintStyle::None;
		return policy;
	}

	int hintStyle = FC_HINT_FULL;
	if (FcPatternGetInteger(fc, FC_HINT_STYLE, 0, &hintStyle) != FcResultMatch) {
		// Hinting on (or unspecified) without a style → FreeType normal target.
		policy.hintStyle = FontHintStyle::Normal;
		return policy;
	}
	switch (hintStyle) {
	case FC_HINT_NONE:
		policy.hintStyle = FontHintStyle::None;
		break;
	case FC_HINT_SLIGHT:
		policy.hintStyle = FontHintStyle::Slight;
		break;
	case FC_HINT_MEDIUM:
	case FC_HINT_FULL:
		policy.hintStyle = FontHintStyle::Normal;
		break;
	default:
		policy.hintStyle = FontHintStyle::Normal;
		break;
	}
	return policy;
}

class FontFace::Impl {
public:
	Impl(std::shared_ptr<void> libraryOwner_, FcPattern *pattern_, FT_Face face_, std::filesystem::path path_,
		int faceIndex_, std::string requestedFamily_, double size_, FontWeight weight_, bool italic_,
		FontStretch stretch_, FontRasterPolicy rasterPolicy_, double metricsScale_, double strikePpem_,
		bool usesBitmapStrike_) :
		libraryOwner(std::move(libraryOwner_)), pattern(pattern_, FcPatternDestroy), face(face_),
		path(std::move(path_)), faceIndex(faceIndex_), requestedFamily(std::move(requestedFamily_)),
		size(size_), weight(weight_), italic(italic_), stretch(stretch_), rasterPolicy(rasterPolicy_),
		loadFlags(static_cast<FT_Int32>(FreeTypeLoadFlagsForPolicy(rasterPolicy_))),
		metricsScale(metricsScale_), strikePpem(strikePpem_),
		usesBitmapStrike(usesBitmapStrike_) {
		// Size must be set on the FT_Face before hb_ft_font_create_referenced.
		hbFont = hb_ft_font_create_referenced(face);
		// Match RasterizeGlyph so advances and colour bitmaps share load flags.
		hb_ft_font_set_load_flags(hbFont, loadFlags);
	}

	~Impl() noexcept {
		// HarfBuzz font first: it may hold a reference on the FT_Face.
		if (hbFont) {
			hb_font_destroy(hbFont);
			hbFont = nullptr;
		}
		for (auto &entry : rasterFaces) {
			if (entry.second) {
				FT_Done_Face(entry.second);
				entry.second = nullptr;
			}
		}
		rasterFaces.clear();
		if (face) {
			FT_Done_Face(face);
			face = nullptr;
		}
	}

	/**
	 * Raster-only FT_Face at the given device height (26.6). Never touches the
	 * HarfBuzz face. Reuses faces for the same height so repeated phase variants
	 * at one scale share one FreeType size object.
	 */
	FT_Face RasterFaceAt(FT_F26Dot6 deviceHeight26_6) {
		if (deviceHeight26_6 <= 0) {
			deviceHeight26_6 = 64;
		}
		if (const auto found = rasterFaces.find(deviceHeight26_6); found != rasterFaces.end()) {
			return found->second;
		}
		auto *library = static_cast<FreeTypeLibrary *>(libraryOwner.get());
		FT_Face raster = nullptr;
		if (FT_New_Face(library->value, path.c_str(), faceIndex, &raster) != 0) {
			return nullptr;
		}
		if (FT_Set_Char_Size(raster, 0, deviceHeight26_6, 72, 72) != 0) {
			// Bitmap-only members of a collection: pick the closest strike.
			if (raster->num_fixed_sizes <= 0) {
				FT_Done_Face(raster);
				return nullptr;
			}
			const double wanted = deviceHeight26_6 / 64.0;
			int bestStrike = 0;
			double bestDiff = 1e300;
			for (int strike = 0; strike < raster->num_fixed_sizes; strike++) {
				const double ppem = raster->available_sizes[strike].y_ppem / 64.0;
				const double diff = std::abs(ppem - wanted);
				if (diff < bestDiff) {
					bestDiff = diff;
					bestStrike = strike;
				}
			}
			if (FT_Select_Size(raster, bestStrike) != 0) {
				FT_Done_Face(raster);
				return nullptr;
			}
		}
		// Keep identity transform until a phase guard sets a delta.
		FT_Set_Transform(raster, nullptr, nullptr);
		rasterFaces.emplace(deviceHeight26_6, raster);
		return raster;
	}

	// Declaration order: hb_font and FT_Face die before the FreeType library owner.
	std::shared_ptr<void> libraryOwner;
	PatternPtr pattern;
	hb_font_t *hbFont = nullptr;
	FT_Face face = nullptr;
	std::filesystem::path path;
	int faceIndex = 0;
	std::string requestedFamily;
	double size;
	FontWeight weight;
	bool italic;
	FontStretch stretch;
	FontRasterPolicy rasterPolicy;
	FT_Int32 loadFlags = FT_LOAD_COLOR | FT_LOAD_TARGET_NORMAL;
	double metricsScale = 1.0;
	double strikePpem = 0.0;
	bool usesBitmapStrike = false;
	/** device height 26.6 → independent raster-only face (not shared with HarfBuzz). */
	std::unordered_map<FT_F26Dot6, FT_Face> rasterFaces;
};

FontFace::FontFace(std::shared_ptr<void> libraryOwner, void *pattern, void *face,
	std::filesystem::path path, int faceIndex, std::string requestedFamily, double size,
	FontWeight weight, bool italic, FontStretch stretch, FontRasterPolicy rasterPolicy,
	double metricsScale, double strikePpem, bool usesBitmapStrike) :
	impl(std::make_unique<Impl>(std::move(libraryOwner), static_cast<FcPattern *>(pattern),
		static_cast<FT_Face>(face), std::move(path), faceIndex, std::move(requestedFamily), size,
		weight, italic, stretch, rasterPolicy, metricsScale, strikePpem, usesBitmapStrike)) {
}

FontFace::~FontFace() noexcept = default;

const std::filesystem::path &FontFace::Path() const noexcept {
	return impl->path;
}

int FontFace::FaceIndex() const noexcept {
	return impl->faceIndex;
}

std::string FontFace::Family() const {
	return impl->face->family_name ? impl->face->family_name : "";
}

GlyphRasterPhase GlyphRasterPhase::Normalize(int32_t x26_6, int32_t y26_6) noexcept {
	return GlyphRasterPhase{NormalizePhase26_6(x26_6), NormalizePhase26_6(y26_6)};
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

const FontRasterPolicy &FontFace::RasterPolicy() const noexcept {
	return impl->rasterPolicy;
}

int FontFace::FreeTypeLoadFlags() const noexcept {
	return static_cast<int>(impl->loadFlags);
}

FontMetrics FontFace::Metrics() const noexcept {
	const FT_Size_Metrics &metrics = impl->face->size->metrics;
	const double scale = impl->metricsScale;
	const double ascent = (metrics.ascender / 64.0) * scale;
	const double descent = (-metrics.descender / 64.0) * scale;
	const double height = (metrics.height / 64.0) * scale;
	return {ascent, descent, height, std::max(0.0, height - ascent - descent)};
}

double FontFace::MetricsScale() const noexcept {
	return impl->metricsScale;
}

bool FontFace::UsesBitmapStrike() const noexcept {
	return impl->usesBitmapStrike;
}

double FontFace::StrikePpem() const noexcept {
	return impl->strikePpem;
}

bool FontFace::HasColor() const noexcept {
	// FT_HAS_COLOR is true when face_flags includes FT_FACE_FLAG_COLOR, set for
	// faces with colour glyph tables (for example CBDT/CBLC). FreeType documents
	// this since 2.5.1; it does not require loading a glyph first.
	return FT_HAS_COLOR(impl->face) != 0;
}

bool FontFace::HasGlyph(char32_t character) const noexcept {
	return FT_Get_Char_Index(impl->face, static_cast<FT_ULong>(character)) != 0;
}

bool FontFace::ShapesSequence(const char32_t *codePoints, size_t count) const {
	if (!codePoints || count == 0 || !impl->hbFont) {
		return false;
	}
	// Must match ShapeSpan in ShapedRun.cxx: discretionary liga/dlig off so
	// fallback acceptance uses the same features as measurement and drawing.
	static const hb_feature_t kEditorShapeFeatures[] = {
		{HB_TAG('l', 'i', 'g', 'a'), 0, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END},
		{HB_TAG('d', 'l', 'i', 'g'), 0, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END},
	};
	struct HbBufferDestroy {
		void operator()(hb_buffer_t *buffer) const noexcept {
			if (buffer) {
				hb_buffer_destroy(buffer);
			}
		}
	};
	std::unique_ptr<hb_buffer_t, HbBufferDestroy> buffer(hb_buffer_create());
	if (!buffer) {
		return false;
	}
	hb_buffer_set_content_type(buffer.get(), HB_BUFFER_CONTENT_TYPE_UNICODE);
	hb_buffer_set_direction(buffer.get(), HB_DIRECTION_LTR);
	hb_buffer_set_cluster_level(buffer.get(), HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
	for (size_t i = 0; i < count; i++) {
		hb_buffer_add(buffer.get(), static_cast<hb_codepoint_t>(codePoints[i]),
			static_cast<unsigned int>(i));
	}
	hb_buffer_guess_segment_properties(buffer.get());
	// Keep editor line ordering left-to-right even when script detection differs.
	hb_buffer_set_direction(buffer.get(), HB_DIRECTION_LTR);
	hb_shape(impl->hbFont, buffer.get(), kEditorShapeFeatures, 2);

	const unsigned int glyphCount = hb_buffer_get_length(buffer.get());
	const hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer.get(), nullptr);
	const hb_glyph_position_t *positions = hb_buffer_get_glyph_positions(buffer.get(), nullptr);
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
	return clean && sawInk;
}

namespace {

GlyphImage CopyGlyphSlotImage(FT_GlyphSlot slot, double metricsScale) {
	GlyphImage image;
	if (!slot) {
		return image;
	}
	const FT_Bitmap &bitmap = slot->bitmap;
	image.width = static_cast<int>(bitmap.width);
	image.height = static_cast<int>(bitmap.rows);
	image.scale = metricsScale;
	// Bearings stay in the face's pixel units. metricsScale converts fixed
	// bitmap-strike pixels into logical units for the historical draw path.
	image.left = static_cast<int>(std::lround(slot->bitmap_left * metricsScale));
	image.top = static_cast<int>(std::lround(slot->bitmap_top * metricsScale));
	if (image.width <= 0 || image.height <= 0 || !bitmap.buffer) {
		image.width = 0;
		image.height = 0;
		return image;
	}

	const int pitch = bitmap.pitch;
	const int absPitch = pitch >= 0 ? pitch : -pitch;
	const auto rowSource = [&](int y) -> const uint8_t * {
		if (pitch >= 0) {
			return bitmap.buffer + y * pitch;
		}
		return bitmap.buffer + (image.height - 1 - y) * absPitch;
	};

	if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
		image.kind = GlyphImageKind::Gray;
		image.gray.resize(static_cast<size_t>(image.width) * static_cast<size_t>(image.height));
		for (int y = 0; y < image.height; y++) {
			std::memcpy(image.gray.data() + static_cast<size_t>(y) * static_cast<size_t>(image.width),
				rowSource(y), static_cast<size_t>(image.width));
		}
		return image;
	}
	if (bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
		// FreeType BGRA is premultiplied; convert to premultiplied RGBA for GL.
		image.kind = GlyphImageKind::Colour;
		image.rgba.resize(static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4u);
		for (int y = 0; y < image.height; y++) {
			const uint8_t *src = rowSource(y);
			uint8_t *dst = image.rgba.data() +
				static_cast<size_t>(y) * static_cast<size_t>(image.width) * 4u;
			for (int x = 0; x < image.width; x++) {
				const uint8_t b = src[x * 4 + 0];
				const uint8_t g = src[x * 4 + 1];
				const uint8_t r = src[x * 4 + 2];
				const uint8_t a = src[x * 4 + 3];
				dst[x * 4 + 0] = r;
				dst[x * 4 + 1] = g;
				dst[x * 4 + 2] = b;
				dst[x * 4 + 3] = a;
			}
		}
		return image;
	}
	// Unsupported pixel mode: leave empty rather than mis-read the buffer.
	image.width = 0;
	image.height = 0;
	image.left = 0;
	image.top = 0;
	return image;
}

GlyphImage RasterizeOnFace(FT_Face face, uint32_t glyphId, FT_Int32 loadFlags,
	double metricsScale, FT_Pos phaseX, FT_Pos phaseY, bool applyPhase) {
	GlyphImage image;
	if (!face) {
		return image;
	}
	// Always arm the guard when applyPhase is true so a previous non-zero phase
	// cannot leak into a zero-phase request (guard restores on scope exit).
	std::optional<FaceTransformGuard> phaseGuard;
	if (applyPhase) {
		phaseGuard.emplace(face, phaseX, phaseY);
	}
	if (FT_Load_Glyph(face, static_cast<FT_UInt>(glyphId), loadFlags) != 0) {
		return image;
	}
	// CBDT loads may already be bitmaps; outlines still need FT_Render_Glyph.
	// Always NORMAL for 8-bit gray: light hinting is the load target, not the
	// render mode (FT_RENDER_MODE_LIGHT produces the same coverage format).
	if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP) {
		if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
			return image;
		}
	}
	return CopyGlyphSlotImage(face->glyph, metricsScale);
}

} // namespace

GlyphImage FontFace::RasterizeGlyph(uint32_t glyphId) const {
	return RasterizeGlyph(GlyphRasterRequest::Identity(glyphId));
}

GlyphImage FontFace::RasterizeGlyph(const GlyphRasterRequest &request) const {
	const GlyphRasterPhase phase = GlyphRasterPhase::Normalize(request.phase.x, request.phase.y);
	// Fixed bitmap strikes: keep the logical face and strike metricsScale.
	// FreeType does not apply FT_Set_Transform to non-scalable bitmap formats.
	if (impl->usesBitmapStrike) {
		return RasterizeOnFace(impl->face, request.glyphId, impl->loadFlags,
			impl->metricsScale, 0, 0, false);
	}

	const FT_F26Dot6 deviceHeight = DeviceHeight26_6(impl->size, request.scale);
	FT_Face rasterFace = impl->RasterFaceAt(deviceHeight);
	if (!rasterFace) {
		return {};
	}
	// Device-sized outline masks are already in device pixels; scale stays 1.
	// Colour bitmaps loaded from a device-sized face also report scale 1 here;
	// strike scaling for fixed-only faces is handled above.
	//
	// phase stores y-down device fractions in [0, 63]. FreeType outline space
	// is y-up, so the vertical transform delta is negated (ftobjs.c applies
	// FT_Set_Transform after hinting via FT_Outline_Translate).
	return RasterizeOnFace(rasterFace, request.glyphId, impl->loadFlags, 1.0,
		phase.x, -phase.y, true);
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
		FcPattern *pattern, const FontParameters &parameters, FontRasterPolicy rasterPolicy) {
		// Prepared Fontconfig matches supply the raster policy; explicit paths
		// keep the caller-provided policy (Normal by default).
		if (pattern) {
			rasterPolicy = RasterPolicyFromFontconfigPattern(pattern);
		}
		const std::string key = FaceKey(path, index, parameters, rasterPolicy);
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
		double metricsScale = 1.0;
		double strikePpem = 0.0;
		bool usesBitmapStrike = false;
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
			strikePpem = face->available_sizes[bestStrike].y_ppem / 64.0;
			if (strikePpem <= 0.0) {
				strikePpem = static_cast<double>(face->available_sizes[bestStrike].height);
			}
			if (strikePpem <= 0.0) {
				FT_Done_Face(face);
				if (pattern) {
					FcPatternDestroy(pattern);
				}
				throw std::runtime_error("FreeType bitmap strike has no usable ppem: " + path.string());
			}
			metricsScale = requestedPixels / strikePpem;
			usesBitmapStrike = true;
		}

		auto selected = std::make_shared<FontFace>(library, pattern, face, path, index,
			RequestedFamilyFromParameters(parameters), parameters.size, parameters.weight,
			parameters.italic, parameters.stretch, rasterPolicy, metricsScale, strikePpem,
			usesBitmapStrike);
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
				// Raster policy comes from the prepared match inside Load.
				auto candidate = Load(reinterpret_cast<const char *>(file), index, prepared, parameters,
					FontRasterPolicy{});
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

	std::shared_ptr<FontFace> ResolveForRequest(const FontParameters &parameters, char32_t character,
		EmojiPresentation presentation) {
		const char32_t codePoints[1] = {character};
		return ResolveForRequest(
			parameters,
			FallbackDecisionKey(parameters, character, presentation),
			codePoints,
			1,
			[character, presentation](const FontFace &face) {
				return FaceMatchesPresentation(face, presentation) && face.HasGlyph(character);
			});
	}

	std::shared_ptr<FontFace> ResolveForRequest(
		const FontParameters &parameters, const char32_t *codePoints, size_t count,
		EmojiPresentation presentation) {
		return ResolveForRequest(
			parameters,
			FallbackDecisionKey(parameters, codePoints, count, presentation),
			codePoints,
			count,
			[codePoints, count, presentation](const FontFace &face) {
				return FaceMatchesPresentation(face, presentation) &&
					SequenceHasRequiredCoverage(face, codePoints, count) &&
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
	// Raster policy comes from the Fontconfig match inside Load.
	return impl->Load(reinterpret_cast<const char *>(file), index, match, parameters,
		FontRasterPolicy{});
}

std::shared_ptr<FontFace> FontCache::ResolveFallback(const FontParameters &parameters, char32_t character,
	EmojiPresentation presentation) {
	return impl->ResolveForRequest(parameters, character, presentation);
}

std::shared_ptr<FontFace> FontCache::ResolveFallback(const FontFace &primary, char32_t character,
	EmojiPresentation presentation) {
	// ParametersFromFace borrows RequestedFamily().c_str(); the string is owned
	// by the face and stays alive for this call and for AddRequest's copy.
	return ResolveFallback(ParametersFromFace(primary), character, presentation);
}

std::shared_ptr<FontFace> FontCache::ResolveFallback(
	const FontParameters &parameters, const char32_t *codePoints, size_t count,
	EmojiPresentation presentation) {
	return impl->ResolveForRequest(parameters, codePoints, count, presentation);
}

std::shared_ptr<FontFace> FontCache::ResolveFallback(
	const FontFace &primary, const char32_t *codePoints, size_t count,
	EmojiPresentation presentation) {
	return ResolveFallback(ParametersFromFace(primary), codePoints, count, presentation);
}

std::shared_ptr<FontFace> FontCache::MatchFallback(const FontParameters &parameters, char32_t character) {
	auto fallback = ResolveFallback(parameters, character);
	if (!fallback) {
		throw std::runtime_error("Fontconfig found no fallback font covering the character");
	}
	return fallback;
}

std::shared_ptr<FontFace> FontCache::LoadPath(
	const std::filesystem::path &path, const FontParameters &parameters,
	FontRasterPolicy rasterPolicy) {
	if (!path.is_absolute()) {
		throw std::invalid_argument("test font path must be absolute");
	}
	return impl->Load(path, 0, nullptr, parameters, rasterPolicy);
}

std::vector<std::shared_ptr<FontFace>> FontCache::LoadPaths(
	const std::vector<std::filesystem::path> &paths, const FontParameters &parameters,
	FontRasterPolicy rasterPolicy) {
	std::vector<std::shared_ptr<FontFace>> loaded;
	loaded.reserve(paths.size());
	for (const std::filesystem::path &path : paths) {
		loaded.push_back(LoadPath(path, parameters, rasterPolicy));
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
	char32_t character,
	EmojiPresentation presentation) const {
	const auto covers = [character](const std::shared_ptr<FontFace> &face) {
		return face && face->HasGlyph(character);
	};
	// Preferred colour class first for emoji/text presentation.
	if (presentation != EmojiPresentation::Unspecified) {
		if (covers(primary) && FaceMatchesPresentation(*primary, presentation)) {
			return primary;
		}
		for (const std::shared_ptr<FontFace> &face : fixedFaces) {
			if (covers(face) && FaceMatchesPresentation(*face, presentation)) {
				return face;
			}
		}
		if (cache && primary) {
			if (std::shared_ptr<FontFace> resolved =
				cache->ResolveFallback(*primary, character, presentation)) {
				return resolved;
			}
		}
	}
	// Unrestricted order: preserve usable monochrome/colour coverage.
	if (covers(primary)) {
		return primary;
	}
	for (const std::shared_ptr<FontFace> &face : fixedFaces) {
		if (covers(face)) {
			return face;
		}
	}
	if (cache && primary) {
		if (std::shared_ptr<FontFace> resolved =
			cache->ResolveFallback(*primary, character, EmojiPresentation::Unspecified)) {
			return resolved;
		}
	}
	return primary;
}

std::shared_ptr<FontFace> FontFallback::Select(
	const std::shared_ptr<FontFace> &primary,
	const char32_t *codePoints, size_t count,
	EmojiPresentation presentation) const {
	if (!codePoints || count == 0) {
		return primary;
	}
	if (count == 1) {
		return Select(primary, codePoints[0], presentation);
	}
	const auto shapes = [codePoints, count](const std::shared_ptr<FontFace> &face) {
		return face && face->ShapesSequence(codePoints, count);
	};
	if (presentation != EmojiPresentation::Unspecified) {
		if (shapes(primary) && FaceMatchesPresentation(*primary, presentation)) {
			return primary;
		}
		for (const std::shared_ptr<FontFace> &face : fixedFaces) {
			if (shapes(face) && FaceMatchesPresentation(*face, presentation)) {
				return face;
			}
		}
		if (cache && primary) {
			if (std::shared_ptr<FontFace> resolved =
				cache->ResolveFallback(*primary, codePoints, count, presentation)) {
				return resolved;
			}
		}
	}
	if (shapes(primary)) {
		return primary;
	}
	for (const std::shared_ptr<FontFace> &face : fixedFaces) {
		if (shapes(face)) {
			return face;
		}
	}
	if (cache && primary) {
		if (std::shared_ptr<FontFace> resolved =
			cache->ResolveFallback(*primary, codePoints, count, EmojiPresentation::Unspecified)) {
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
