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
	 * HarfBuzz font for this face. Owned by FontFace and destroyed before the
	 * FreeType face. Typed as void* so this header does not include HarfBuzz.
	 * Callers cast to hb_font_t *.
	 */
	void *HarfBuzzFont() const noexcept;
};

/**
 * Owns Fontconfig, FreeType, and all faces selected through it.
 *
 * Production requests use Fontconfig. Tests load explicit paths so installed
 * fonts and the user's Fontconfig rules cannot change their results. Cached
 * faces stay alive until this cache is destroyed; each face also retains the
 * shared FreeType owner needed by its FT_Face. HarfBuzz fonts belong to
 * FontFace and are destroyed before the FT_Face.
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

}

#endif
