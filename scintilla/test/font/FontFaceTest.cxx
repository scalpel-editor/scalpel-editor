#include "FontTest.h"

TEST_CASE("Explicit font paths select stable fixtures") {
	FontCache cache;
	const FontParameters parameters("ignored by explicit paths", 16.0);
	const auto faces = cache.LoadPaths({primaryPath, snowmanPath}, parameters);

	REQUIRE(faces.size() == 2);
	CHECK(faces[0]->Path() == primaryPath);
	CHECK(faces[0]->Family() == "Scalpel Fallback Primary");
	CHECK(faces[1]->Path() == snowmanPath);
	CHECK(faces[1]->Family() == "Scalpel Fallback Snowman");
	CHECK(faces[0]->HasGlyph(U'A'));
	CHECK_FALSE(faces[0]->HasGlyph(U'\u2603'));
	CHECK(faces[1]->HasGlyph(U'\u2603'));
}

TEST_CASE("Fixture metrics are available at the requested size") {
	FontCache cache;
	const auto face = cache.LoadPath(primaryPath, FontParameters("fixture", 16.0));
	const FontMetrics metrics = face->Metrics();

	CHECK(face->RequestedSize() == 16.0);
	CHECK(metrics.ascent == 18.0);
	CHECK(metrics.descent == 11.0);
	CHECK(metrics.height == 27.0);
	CHECK(metrics.internalLeading == 0.0);
}

TEST_CASE("Explicit faces retain requested style and weight") {
	FontCache cache;
	const FontParameters parameters("fixture", 13.0, FontWeight::Bold, true);
	const auto face = cache.LoadPath(primaryPath, parameters);

	CHECK(face->RequestedWeight() == FontWeight::Bold);
	CHECK(face->RequestedItalic());
	CHECK(face->RequestedSize() == 13.0);
}

TEST_CASE("Missing production family falls back through Fontconfig") {
	FontCache cache;
	const FontParameters parameters("Scalpel family that cannot exist 93E9B3", 14.0,
		FontWeight::SemiBold, true);
	const auto face = cache.Match(parameters);

	CHECK_FALSE(face->Path().empty());
	CHECK_FALSE(face->Family().empty());
	CHECK(face->Family() != parameters.faceName);
	CHECK(face->RequestedWeight() == FontWeight::SemiBold);
	CHECK(face->RequestedItalic());
}

TEST_CASE("Production font allocation uses the Fontconfig face") {
	const auto font = Font::Allocate(FontParameters("sans-serif", 14.0));
	const FontFace *face = FaceFromFont(font.get());

	REQUIRE(face);
	CHECK_FALSE(face->Path().empty());
	CHECK(face->Metrics().height > 0.0);
}

TEST_CASE("Production generic font families resolve usable faces") {
	// Canonical generic families requested by the Font menu. Assert only that
	// each literal family string resolves to a loadable face and path; never
	// assert a concrete host family such as Cantarell or DejaVu Sans.
	FontCache cache;
	const char *families[] = {
		"monospace",
		"serif",
		"sans-serif",
		"system-ui",
	};
	for (const char *family : families) {
		const auto face = cache.Match(FontParameters(family, 14.0));
		REQUIRE(face);
		CHECK_FALSE(face->Path().empty());
		CHECK(face->Metrics().height > 0.0);
		CHECK(face->RequestedSize() == 14.0);
	}
}

TEST_CASE("Production fallback covers the requested character") {
	FontCache cache;
	const auto fallback = cache.MatchFallback(FontParameters("sans-serif", 14.0), U'\u2603');

	CHECK(fallback->HasGlyph(U'\u2603'));
	CHECK_FALSE(fallback->Path().empty());
}

TEST_CASE("Face cache owns primary and fallback faces") {
	FontCache cache;
	const FontParameters parameters("fixture", 16.0);
	const auto first = cache.LoadPath(primaryPath, parameters);
	const auto again = cache.LoadPath(primaryPath, parameters);
	const auto fallback = cache.LoadPath(snowmanPath, parameters);

	CHECK(first == again);
	CHECK(first != fallback);
	CHECK(first->HasGlyph(U'A'));
	CHECK_FALSE(first->HasGlyph(U'\u2603'));
	CHECK(fallback->HasGlyph(U'\u2603'));
}

TEST_CASE("HarfBuzz font uses the rasterizer load flags") {
	FontCache cache;
	const auto face = LoadPrimary(cache);
	hb_font_t *hbFont = static_cast<hb_font_t *>(face->HarfBuzzFont());

	REQUIRE(hbFont != nullptr);
	CHECK(hb_ft_font_get_load_flags(hbFont) == FT_LOAD_DEFAULT);
	CHECK_FALSE(hb_ft_font_get_load_flags(hbFont) & FT_LOAD_NO_HINTING);
}

TEST_CASE("HarfBuzz narrow glyph uses a hinted advance at 11 pixels") {
	FontCache cache;
	const auto face = LoadPrimary(cache, 11.0);
	const ShapedRun run = ShapeText("i", face);

	REQUIRE(run.glyphs.size() == 1);
	CHECK(run.glyphs[0].xAdvance == 3.0);
	CHECK(run.Width() == run.glyphs[0].xAdvance);
}

TEST_CASE("ShapeText measures ASCII with per-byte end positions") {
	FontCache cache;
	const auto face = LoadPrimary(cache);
	const ShapedRun run = ShapeText("AB", face);

	REQUIRE(run.text == "AB");
	REQUIRE(run.byteEndPositions.size() == 2);
	CHECK(run.byteEndPositions[0] > 0.0);
	CHECK(run.byteEndPositions[1] > run.byteEndPositions[0]);
	CHECK(run.Width() == run.byteEndPositions.back());
	CHECK(MonotonicEnds(run.byteEndPositions));
	REQUIRE(run.caretStops == std::vector<size_t>{0, 1, 2});
	REQUIRE_FALSE(run.glyphs.empty());
	CHECK(run.glyphs[0].face == face);
	CHECK(run.glyphs[0].cluster == 0);
}

TEST_CASE("ShapeText applies kerning on AV with the primary fixture") {
	FontCache cache;
	const auto face = LoadPrimary(cache);
	const ShapedRun pair = ShapeText("AV", face);
	const ShapedRun a = ShapeText("A", face);
	const ShapedRun v = ShapeText("V", face);

	const XYPOSITION separate = a.Width() + v.Width();
	CHECK(pair.Width() + 1e-6 < separate);
	CHECK(pair.Width() > 0.0);
}
