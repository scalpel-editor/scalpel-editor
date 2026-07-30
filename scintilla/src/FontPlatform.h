// scalpel-editor font lookup and FreeType face ownership.

#ifndef FONTPLATFORM_H
#define FONTPLATFORM_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Platform.h"

namespace Scintilla::Internal {

struct FontMetrics {
	double ascent = 0.0;
	double descent = 0.0;
	double height = 0.0;
	double internalLeading = 0.0;
};

/**
 * FreeType-rasterized glyph coverage (FT_RENDER_MODE_NORMAL).
 *
 * gray is 8-bit coverage, row-major, top-down. left/top are FreeType bearings:
 * left is the horizontal distance from the pen origin to the left of the
 * bitmap; top is the vertical distance from the baseline up to the top of the
 * bitmap (positive above the baseline). Empty width/height means no ink.
 */
struct GlyphImage {
	int width = 0;
	int height = 0;
	int left = 0;
	int top = 0;
	std::vector<uint8_t> gray;
};

class FontFace {
	class Impl;
	std::unique_ptr<Impl> impl;

public:
	FontFace(std::shared_ptr<void> libraryOwner, void *pattern, void *face,
		std::filesystem::path path, double size, FontWeight weight, bool italic);
	~FontFace() noexcept;

	FontFace(const FontFace &) = delete;
	FontFace(FontFace &&) = delete;
	FontFace &operator=(const FontFace &) = delete;
	FontFace &operator=(FontFace &&) = delete;

	const std::filesystem::path &Path() const noexcept;
	std::string Family() const;
	double RequestedSize() const noexcept;
	FontWeight RequestedWeight() const noexcept;
	bool RequestedItalic() const noexcept;
	FontMetrics Metrics() const noexcept;
	bool HasGlyph(char32_t character) const noexcept;

	/**
	 * Rasterize one glyph by FreeType glyph index (not a character code).
	 *
	 * Uses FT_LOAD_DEFAULT and FT_RENDER_MODE_NORMAL (no LCD subpixel). Missing
	 * or unloadable glyphs return an empty image. Callers that share this face
	 * with HarfBuzz must not change FT_Face size after construction; load and
	 * render only replace the slot.
	 */
	GlyphImage RasterizeGlyph(uint32_t glyphId) const;

	/**
	 * HarfBuzz font for this face. Owned by FontFace and destroyed before the
	 * FreeType face. Typed as void* so this header does not include HarfBuzz.
	 * Callers cast to hb_font_t *. Shaping and RasterizeGlyph both use
	 * FT_LOAD_DEFAULT so advances and bitmap shapes share hinted metrics.
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
 * Tests load explicit paths so installed fonts and the user's Fontconfig rules
 * cannot change their results. Cached faces stay alive until this cache is
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
	std::shared_ptr<FontFace> MatchFallback(const FontParameters &parameters, char32_t character);
	std::shared_ptr<FontFace> LoadPath(const std::filesystem::path &path, const FontParameters &parameters);
	std::vector<std::shared_ptr<FontFace>> LoadPaths(
		const std::vector<std::filesystem::path> &paths, const FontParameters &parameters);
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

}

#endif
