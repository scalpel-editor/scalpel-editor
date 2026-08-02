// scalpel-editor font lookup and FreeType face ownership.

#ifndef FONTPLATFORM_H
#define FONTPLATFORM_H

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "EmojiSequence.h"
#include "Platform.h"

namespace Scintilla::Internal {

struct FontMetrics {
	double ascent = 0.0;
	double descent = 0.0;
	double height = 0.0;
	double internalLeading = 0.0;
};

/** Pixel layout produced by FontFace::RasterizeGlyph. */
enum class GlyphImageKind {
	Empty = 0,
	/** 8-bit coverage mask; renderer tints with the text foreground. */
	Gray = 1,
	/** Premultiplied RGBA; renderer draws colour without RGB tint. */
	Colour = 2,
};

/**
 * FreeType-rasterized glyph image for the renderer glyph cache.
 *
 * width/height are the bitmap size in the raster coordinate system (device
 * pixels for phase-aware outline rasterization; strike pixels for fixed
 * bitmaps). left/top are integer bearings from the pen origin to the bitmap
 * left/top edges in that same coordinate system. scale maps bitmap size to
 * logical draw size for fixed-bitmap strikes (requested/strike); it is 1 for
 * outline glyphs already rasterized at the requested device size. gray is
 * row-major top-down coverage when kind is Gray. rgba is row-major top-down
 * premultiplied RGBA when kind is Colour.
 */
struct GlyphImage {
	GlyphImageKind kind = GlyphImageKind::Empty;
	int width = 0;
	int height = 0;
	int left = 0;
	int top = 0;
	double scale = 1.0;
	std::vector<uint8_t> gray;
	std::vector<uint8_t> rgba;
};

/**
 * Normalized FreeType 26.6 subpixel phase for outline rasterization.
 *
 * Each component lies in [0, 63]. Equivalent device positions that differ by a
 * whole pixel share the same phase after floor-based normalization.
 */
struct GlyphRasterPhase {
	int32_t x = 0;
	int32_t y = 0;

	/** Reduce 26.6 offsets into [0, 63] with floor remainder (handles negatives). */
	[[nodiscard]] static GlyphRasterPhase Normalize(int32_t x26_6, int32_t y26_6) noexcept;

	[[nodiscard]] friend constexpr bool operator==(const GlyphRasterPhase &left,
		const GlyphRasterPhase &right) noexcept {
		return left.x == right.x && left.y == right.y;
	}

	[[nodiscard]] friend constexpr bool operator!=(const GlyphRasterPhase &left,
		const GlyphRasterPhase &right) noexcept {
		return !(left == right);
	}
};

/**
 * Request to rasterize one FreeType glyph at a device scale and phase.
 *
 * scale is the nominal output RasterScale (not bufferWidth/logicalWidth).
 * phase is applied only to outline glyphs via FreeType's face transform delta
 * after hinting. Fixed bitmap and colour strikes ignore phase and keep their
 * strike-selection path.
 */
struct GlyphRasterRequest {
	uint32_t glyphId = 0;
	RasterScale scale{};
	GlyphRasterPhase phase{};

	/** Scale 1/1 and zero phase (legacy single-argument RasterizeGlyph). */
	[[nodiscard]] static GlyphRasterRequest Identity(uint32_t glyphId) noexcept {
		return GlyphRasterRequest{glyphId, RasterScale{}, GlyphRasterPhase{}};
	}

	[[nodiscard]] friend constexpr bool operator==(const GlyphRasterRequest &left,
		const GlyphRasterRequest &right) noexcept {
		return left.glyphId == right.glyphId && left.scale == right.scale &&
			left.phase == right.phase;
	}
};

/**
 * FreeType hint strength for antialiased grayscale outlines.
 *
 * Mapped from Fontconfig FC_HINTING / FC_HINT_STYLE. Stronger styles
 * (medium, full) collapse to Normal because the renderer only consumes
 * FT_PIXEL_MODE_GRAY.
 */
enum class FontHintStyle {
	None = 0,
	Slight = 1,
	Normal = 2,
};

/**
 * Grayscale raster choices retained with a FontFace.
 *
 * Production faces extract these from the prepared Fontconfig match.
 * Explicit-path loads use Normal unless a test supplies another policy.
 * antialias is recorded for identity and documentation; false still
 * rasterizes as 8-bit gray coverage because packed monochrome is not
 * supported by the renderer.
 */
struct FontRasterPolicy {
	bool antialias = true;
	FontHintStyle hintStyle = FontHintStyle::Normal;

	/**
	 * FreeType load flags for HarfBuzz and RasterizeGlyph. Always includes
	 * FT_LOAD_COLOR. Hint none uses FT_LOAD_NO_HINTING; slight uses
	 * FT_LOAD_TARGET_LIGHT; normal uses FT_LOAD_TARGET_NORMAL.
	 */
	[[nodiscard]] int FreeTypeLoadFlags() const noexcept;
};

[[nodiscard]] inline bool operator==(const FontRasterPolicy &a, const FontRasterPolicy &b) noexcept {
	return a.antialias == b.antialias && a.hintStyle == b.hintStyle;
}

[[nodiscard]] inline bool operator!=(const FontRasterPolicy &a, const FontRasterPolicy &b) noexcept {
	return !(a == b);
}

/**
 * Read grayscale raster policy from a prepared Fontconfig match (FcPattern *).
 * Typed as void * so this header does not include Fontconfig.
 *
 * Defaults when a property is absent or invalid: antialias true, hinting on,
 * hint style Normal. FC_HINTING false or FC_HINT_NONE yields None;
 * FC_HINT_SLIGHT yields Slight; medium, full, and unknown styles yield Normal.
 * FC_ANTIALIAS false is recorded but does not select monochrome load flags.
 */
[[nodiscard]] FontRasterPolicy RasterPolicyFromFontconfigPattern(const void *pattern) noexcept;

class FontFace {
	class Impl;
	std::unique_ptr<Impl> impl;

public:
	FontFace(std::shared_ptr<void> libraryOwner, void *pattern, void *face,
		std::filesystem::path path, int faceIndex, std::string requestedFamily, double size,
		FontWeight weight, bool italic, FontStretch stretch,
		FontRasterPolicy rasterPolicy = {},
		double metricsScale = 1.0, double strikePpem = 0.0, bool usesBitmapStrike = false);
	~FontFace() noexcept;

	FontFace(const FontFace &) = delete;
	FontFace(FontFace &&) = delete;
	FontFace &operator=(const FontFace &) = delete;
	FontFace &operator=(FontFace &&) = delete;

	const std::filesystem::path &Path() const noexcept;
	/** Fontconfig / FreeType face index within a TTC or OTC collection. */
	[[nodiscard]] int FaceIndex() const noexcept;
	std::string Family() const;
	/** Requested FC_FAMILY string (stable copy; not the concrete face family). */
	const std::string &RequestedFamily() const noexcept;
	double RequestedSize() const noexcept;
	FontWeight RequestedWeight() const noexcept;
	bool RequestedItalic() const noexcept;
	FontStretch RequestedStretch() const noexcept;
	/** Grayscale raster policy used for HarfBuzz and FreeType load flags. */
	const FontRasterPolicy &RasterPolicy() const noexcept;
	/**
	 * FreeType load flags shared by HarfBuzz and RasterizeGlyph
	 * (FT_LOAD_COLOR plus the policy hint target).
	 */
	[[nodiscard]] int FreeTypeLoadFlags() const noexcept;
	/**
	 * Logical metrics at the requested size. For fixed bitmap strikes this is
	 * the strike metrics multiplied by MetricsScale().
	 */
	FontMetrics Metrics() const noexcept;
	/**
	 * requestedSize / strike ppem for bitmap-only faces; 1 for scalable faces.
	 * Shaping advances and glyph bearings are expressed in logical units.
	 */
	double MetricsScale() const noexcept;
	/** True when FreeType selected a fixed bitmap strike instead of scaling. */
	bool UsesBitmapStrike() const noexcept;
	/** Selected strike y_ppem, or 0 when the face is scalable. */
	double StrikePpem() const noexcept;
	/**
	 * True when FreeType reports colour glyph tables on this face
	 * (FT_HAS_COLOR / FT_FACE_FLAG_COLOR). Used to prefer colour or
	 * monochrome candidates for emoji presentation fallback.
	 */
	bool HasColor() const noexcept;
	bool HasGlyph(char32_t character) const noexcept;

	/**
	 * True when shaping the complete sequence yields at least one non-.notdef
	 * glyph and no positive-advance .notdef. Default-ignorable joiners and
	 * selectors may map to zero-advance placeholders. Empty sequences are false.
	 */
	bool ShapesSequence(const char32_t *codePoints, size_t count) const;

	/**
	 * Rasterize one glyph by FreeType glyph index (not a character code).
	 *
	 * Equivalent to RasterizeGlyph(GlyphRasterRequest::Identity(glyphId)):
	 * scale 1/1 and zero phase. Prefer the request overload for device-scale
	 * and phase-aware outline rasterization.
	 */
	GlyphImage RasterizeGlyph(uint32_t glyphId) const;

	/**
	 * Rasterize one glyph at the request's device scale and 26.6 phase.
	 *
	 * Outline glyphs use a separate FreeType face sized to the device ppem so
	 * the HarfBuzz logical face is never resized or transformed. Phase is
	 * applied with FT_Set_Transform (identity matrix + 26.6 delta) after
	 * hinting, then restored. Fixed bitmap strikes and colour bitmaps skip
	 * outline phase transforms and keep strike selection / metricsScale.
	 * Load flags always match FreeTypeLoadFlags() (FT_LOAD_COLOR plus the
	 * face hint policy). Outlines always use FT_RENDER_MODE_NORMAL. Missing
	 * or unloadable glyphs return an empty image.
	 */
	GlyphImage RasterizeGlyph(const GlyphRasterRequest &request) const;

	/**
	 * HarfBuzz font for this face. Owned by FontFace and destroyed before the
	 * FreeType face. Typed as void* so this header does not include HarfBuzz.
	 * Callers cast to hb_font_t *. Load flags match RasterizeGlyph
	 * (FreeTypeLoadFlags()).
	 */
	void *HarfBuzzFont() const noexcept;
};

/**
 * Owns Fontconfig, FreeType, and all faces selected through it.
 *
 * Production requests use Fontconfig. FontParameters::faceName is a literal
 * Fontconfig family string (for example monospace, serif, sans-serif, or
 * system-ui). It is added with FcPatternAddString as FC_FAMILY; it is not
 * parsed with FcNameParse. That matters for system-ui: the hyphen is part of
 * the family name. The shell command fc-match treats an unescaped hyphen as a
 * family/size separator, so fc-match system-ui and fc-match 'system\-ui' are
 * different diagnostics; the application always passes the C string
 * "system-ui" and never a backslash-escaped form.
 *
 * Deterministic layout and raster tests load explicit paths so installed fonts
 * and the user's Fontconfig rules cannot change their results. Focused
 * production-lookup tests exercise the host configuration without naming its
 * chosen concrete families. Cached faces stay alive until this cache is
 * destroyed; each face also retains the shared FreeType owner needed by its
 * FT_Face. HarfBuzz fonts belong to FontFace and are destroyed before the
 * FT_Face.
 */
class FontCache {
	class Impl;
	std::unique_ptr<Impl> impl;

public:
	FontCache();
	~FontCache() noexcept;

	FontCache(const FontCache &) = delete;
	FontCache(FontCache &&) = delete;
	FontCache &operator=(const FontCache &) = delete;
	FontCache &operator=(FontCache &&) = delete;

	/** Resolve parameters.faceName as a literal FC_FAMILY through Fontconfig. */
	std::shared_ptr<FontFace> Match(const FontParameters &parameters);
	/**
	 * Resolve a face covering character for the request. Throws when no
	 * Fontconfig candidate has FreeType coverage (for callers that require a
	 * hit). Prefer ResolveFallback when absence must not throw.
	 */
	std::shared_ptr<FontFace> MatchFallback(const FontParameters &parameters, char32_t character);
	/**
	 * Ordered Fontconfig candidates for the primary request plus character,
	 * validated with FreeType coverage and the face cache. Empty when nothing
	 * covers the character. Decisions are cached per request characteristics,
	 * code point, and presentation preference. Does not throw for a missing
	 * covering face. When presentation is Emoji or Text, only colour-capable
	 * or non-colour faces are accepted; callers that need unrestricted
	 * resolution pass Unspecified.
	 */
	std::shared_ptr<FontFace> ResolveFallback(const FontFace &primary, char32_t character,
		EmojiPresentation presentation = EmojiPresentation::Unspecified);
	std::shared_ptr<FontFace> ResolveFallback(const FontParameters &parameters, char32_t character,
		EmojiPresentation presentation = EmojiPresentation::Unspecified);
	/**
	 * Resolve a face that shapes the complete sequence (ordered Fontconfig
	 * candidates, FreeType coverage of non-ignorable code points, then
	 * ShapesSequence). Empty when nothing covers the request. Presentation
	 * filters candidates the same way as the single-character overload.
	 */
	std::shared_ptr<FontFace> ResolveFallback(
		const FontFace &primary, const char32_t *codePoints, size_t count,
		EmojiPresentation presentation = EmojiPresentation::Unspecified);
	std::shared_ptr<FontFace> ResolveFallback(
		const FontParameters &parameters, const char32_t *codePoints, size_t count,
		EmojiPresentation presentation = EmojiPresentation::Unspecified);
	/**
	 * Load an explicit font file without Fontconfig matching. Uses the given
	 * raster policy (Normal by default) so deterministic tests can inject
	 * light or unhinted grayscale rasterization without host config.
	 */
	std::shared_ptr<FontFace> LoadPath(const std::filesystem::path &path,
		const FontParameters &parameters, FontRasterPolicy rasterPolicy = {});
	std::vector<std::shared_ptr<FontFace>> LoadPaths(
		const std::vector<std::filesystem::path> &paths, const FontParameters &parameters,
		FontRasterPolicy rasterPolicy = {});
};

/**
 * Selects a fallback face for a missing glyph relative to a primary face.
 *
 * Fixed faces are the deterministic test override. Production resolves through
 * a FontCache using each primary face's retained request characteristics. When
 * nothing covers the character, Select returns the primary so HarfBuzz can emit
 * .notdef without crashing painting.
 */
class FontFallback {
public:
	/** No fallback faces and no production resolution. */
	FontFallback() noexcept = default;

	/**
	 * Deterministic test faces only (no Fontconfig lookup).
	 * Allows CreateDrawSurface(..., {face}) and ShapeText(..., {face}).
	 */
	FontFallback(std::initializer_list<std::shared_ptr<FontFace>> faces);
	FontFallback(std::vector<std::shared_ptr<FontFace>> faces);

	/** Deterministic test faces only (no Fontconfig lookup). */
	static FontFallback Fixed(std::vector<std::shared_ptr<FontFace>> faces);

	/** Resolve missing glyphs through the process-wide FontCache. */
	static FontFallback Production();

	/** Resolve missing glyphs through the given cache. */
	static FontFallback Production(FontCache &cache);

	/**
	 * Primary when it covers the character; otherwise the first fixed face that
	 * covers it; otherwise a production ResolveFallback hit; otherwise primary.
	 * When presentation is Emoji, colour-capable faces are tried first; when
	 * Text, non-colour faces are tried first. If the preferred class has no
	 * usable face, the unrestricted order runs so monochrome coverage is not
	 * abandoned for .notdef.
	 */
	std::shared_ptr<FontFace> Select(
		const std::shared_ptr<FontFace> &primary,
		char32_t character,
		EmojiPresentation presentation = EmojiPresentation::Unspecified) const;

	/**
	 * Select one face for a whole supported sequence (or a single code point).
	 * Acceptance uses ShapesSequence so ligating emoji forms stay unsplit.
	 * Presentation preference matches the single-character overload.
	 */
	std::shared_ptr<FontFace> Select(
		const std::shared_ptr<FontFace> &primary,
		const char32_t *codePoints, size_t count,
		EmojiPresentation presentation = EmojiPresentation::Unspecified) const;

	[[nodiscard]] bool Empty() const noexcept;
	[[nodiscard]] bool UsesProductionResolver() const noexcept;
	[[nodiscard]] const std::vector<std::shared_ptr<FontFace>> &FixedFaces() const noexcept;
	[[nodiscard]] FontCache *ResolverCache() const noexcept;
	/** Stable identity for shaped-run cache keys. */
	[[nodiscard]] std::string CacheIdentity() const;

private:
	std::vector<std::shared_ptr<FontFace>> fixedFaces;
	FontCache *cache = nullptr;
};

const FontFace *FaceFromFont(const Font *font) noexcept;

/**
 * Shared face behind a Font produced by Font::Allocate or FontFromFace.
 * Empty when the Font is not a platform face (for example a test stub).
 */
std::shared_ptr<FontFace> SharedFaceFromFont(const Font *font) noexcept;

/**
 * Wrap an existing face as a Font for Surface and IScreenLine callers.
 * Face must be non-null.
 */
std::shared_ptr<Font> FontFromFace(std::shared_ptr<FontFace> face);

/**
 * Force Font::Allocate and Surface measure fallbacks to load checked-in fixture
 * paths instead of Fontconfig. editorTest calls this so system fonts cannot
 * move metrics. Empty primary clears the override (production default).
 */
void UseTestFontPaths(const std::filesystem::path &primary,
	const std::vector<std::filesystem::path> &fallbacks = {});
void ClearTestFontPaths() noexcept;

/** True while UseTestFontPaths has a non-empty primary. */
bool TestFontPathsActive() noexcept;

/**
 * Load fallback fixture faces at the given point size from the paths set by
 * UseTestFontPaths. Empty when no test paths are active.
 */
std::vector<std::shared_ptr<FontFace>> TestFontFallbackFaces(double size);

/** Production FontFallback when test paths are inactive; else fixed fixtures. */
FontFallback DefaultSurfaceFallback(double size = 10.0);

}

#endif
