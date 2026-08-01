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
	CHECK(face->RequestedFamily() == "fixture");
	CHECK(face->RequestedStretch() == FontStretch::Normal);
}

TEST_CASE("Faces retain requested family and stretch independently of concrete face") {
	FontCache cache;
	const FontParameters parameters("sans-serif", 14.0, FontWeight::SemiBold, true,
		FontQuality::QualityDefault, CharacterSet::Ansi, localeNameDefault,
		FontStretch::SemiCondensed);
	const auto face = cache.Match(parameters);

	// Requested family is the stable caller string, not the concrete file family.
	CHECK(face->RequestedFamily() == "sans-serif");
	CHECK(face->RequestedStretch() == FontStretch::SemiCondensed);
	CHECK(face->RequestedWeight() == FontWeight::SemiBold);
	CHECK(face->RequestedItalic());
	CHECK(face->RequestedSize() == 14.0);
	CHECK_FALSE(face->Family().empty());
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

TEST_CASE("ResolveFallback reuses the decision and face cache") {
	FontCache cache;
	const FontParameters parameters("sans-serif", 14.0);
	const auto first = cache.ResolveFallback(parameters, U'\u2603');
	const auto again = cache.ResolveFallback(parameters, U'\u2603');

	REQUIRE(first);
	CHECK(first == again);
	CHECK(first->HasGlyph(U'\u2603'));
	// Face cache returns the same instance for the same path/size/style.
	const auto reloaded = cache.LoadPath(first->Path(), parameters);
	CHECK(reloaded == first);
}

TEST_CASE("ResolveFallback returns empty when no candidate covers the character") {
	FontCache cache;
	// U+FFFF is a noncharacter; FreeType coverage is absent on typical hosts.
	const auto missing = cache.ResolveFallback(FontParameters("sans-serif", 14.0), U'\uFFFF');
	CHECK_FALSE(missing);
	// A second miss hits the negative decision cache without throwing.
	CHECK_FALSE(cache.ResolveFallback(FontParameters("sans-serif", 14.0), U'\uFFFF'));
}

TEST_CASE("ResolveFallback uses the primary face request characteristics") {
	FontCache cache;
	const auto primary = cache.LoadPath(primaryPath,
		FontParameters("fixture-primary", 18.0, FontWeight::Bold, true));
	REQUIRE_FALSE(primary->HasGlyph(U'\u2603'));

	const auto resolved = cache.ResolveFallback(*primary, U'\u2603');
	if (resolved) {
		CHECK(resolved->HasGlyph(U'\u2603'));
		CHECK(resolved->RequestedSize() == 18.0);
		CHECK(resolved->RequestedWeight() == FontWeight::Bold);
		CHECK(resolved->RequestedItalic());
		CHECK(resolved->RequestedFamily() == "fixture-primary");
	}
}

TEST_CASE("FontFallback Select keeps primary for missing coverage") {
	FontCache cache;
	const auto primary = LoadPrimary(cache);
	const FontFallback none;
	const auto selected = none.Select(primary, U'\u2603');
	CHECK(selected == primary);
	CHECK_FALSE(selected->HasGlyph(U'\u2603'));
}

TEST_CASE("FontFallback fixed faces cover missing primary glyphs") {
	FontCache cache;
	const auto primary = LoadPrimary(cache);
	const auto snowman = LoadSnowman(cache);
	const FontFallback fallback = FontFallback::Fixed({snowman});
	const auto selected = fallback.Select(primary, U'\u2603');
	CHECK(selected == snowman);
}

TEST_CASE("Production measure and shape paths agree on fallback selection") {
	FontCache cache;
	const auto primary = cache.Match(FontParameters("sans-serif", 14.0));
	const auto font = FontFromFace(primary);
	const std::string text = std::string("A") + "\xE2\x98\x83" + "B";
	const FontFallback production = FontFallback::Production(cache);

	// MeasureSurface and ShapeText share FontFallback::Select so widths match.
	auto measure = CreateMeasureSurface(production);
	std::vector<XYPOSITION> measureEnds(text.size());
	std::vector<XYPOSITION> shapedEnds(text.size());
	measure->MeasureWidths(font.get(), text, measureEnds.data());
	const ShapedRun run = ShapeText(text, primary, production);
	FillMeasureWidths(run, shapedEnds.data());

	REQUIRE(measureEnds.size() == shapedEnds.size());
	for (size_t i = 0; i < measureEnds.size(); i++) {
		CHECK(measureEnds[i] == shapedEnds[i]);
	}
	// A second measure surface with the same production resolver agrees too.
	auto measureAgain = CreateMeasureSurface(production);
	std::vector<XYPOSITION> againEnds(text.size());
	measureAgain->MeasureWidths(font.get(), text, againEnds.data());
	for (size_t i = 0; i < measureEnds.size(); i++) {
		CHECK(againEnds[i] == measureEnds[i]);
	}
	// When a covering face exists, the snowman span is not the primary face.
	for (const ShapedGlyph &glyph : run.glyphs) {
		if (glyph.cluster == 1 && glyph.face != primary) {
			CHECK(glyph.face->HasGlyph(U'\u2603'));
		}
	}
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
	CHECK(hb_ft_font_get_load_flags(hbFont) == (FT_LOAD_COLOR | FT_LOAD_DEFAULT));
	CHECK(hb_ft_font_get_load_flags(hbFont) & FT_LOAD_COLOR);
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
