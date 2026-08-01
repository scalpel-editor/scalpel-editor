#include "FontTest.h"

#include <fontconfig/fontconfig.h>

namespace {

FontRasterPolicy Policy(FontHintStyle style, bool antialias = true) {
	FontRasterPolicy policy;
	policy.hintStyle = style;
	policy.antialias = antialias;
	return policy;
}

std::shared_ptr<FontFace> LoadPrimaryWithPolicy(
	FontCache &cache, FontHintStyle style, double size = 16.0) {
	return cache.LoadPath(primaryPath, FontParameters("fixture", size), Policy(style));
}

long CoverageSum(const GlyphImage &image) {
	long sum = 0;
	for (const uint8_t c : image.gray) {
		sum += c;
	}
	return sum;
}

}

TEST_CASE("RasterizeGlyph returns coverage for a shaped ASCII glyph") {
	FontCache cache;
	const auto face = LoadPrimary(cache);
	const ShapedRun run = ShapeText("A", face);
	REQUIRE_FALSE(run.glyphs.empty());

	const GlyphImage image = face->RasterizeGlyph(run.glyphs[0].glyphId);
	REQUIRE(image.width > 0);
	REQUIRE(image.height > 0);
	CHECK(image.kind == GlyphImageKind::Gray);
	CHECK(image.scale == 1.0);
	REQUIRE(image.gray.size() == static_cast<size_t>(image.width) * static_cast<size_t>(image.height));
	const bool hasCoverage = std::any_of(image.gray.begin(), image.gray.end(),
		[](uint8_t c) { return c > 0; });
	CHECK(hasCoverage);
	// Capital letters typically sit above the baseline.
	CHECK(image.top > 0);
}

TEST_CASE("RasterizeGlyph is deterministic for the same glyph id") {
	FontCache cache;
	const auto face = LoadPrimary(cache);
	const ShapedRun run = ShapeText("B", face);
	REQUIRE_FALSE(run.glyphs.empty());
	const uint32_t glyphId = run.glyphs[0].glyphId;

	const GlyphImage first = face->RasterizeGlyph(glyphId);
	const GlyphImage second = face->RasterizeGlyph(glyphId);
	CHECK(first.width == second.width);
	CHECK(first.height == second.height);
	CHECK(first.left == second.left);
	CHECK(first.top == second.top);
	CHECK(first.gray == second.gray);
}

TEST_CASE("RasterizeGlyph returns empty image for unloadable glyph id") {
	FontCache cache;
	const auto face = LoadPrimary(cache);
	// FreeType glyph indices are face-specific; a huge id fails load cleanly.
	const GlyphImage image = face->RasterizeGlyph(0x7fffffffu);
	CHECK(image.width == 0);
	CHECK(image.height == 0);
	CHECK(image.gray.empty());
}

TEST_CASE("RasterizeGlyph covers the shaped snowman fallback glyph") {
	FontCache cache;
	const auto snowman = LoadSnowman(cache);
	const std::string text = "\xE2\x98\x83";
	const ShapedRun run = ShapeText(text, snowman);
	REQUIRE_FALSE(run.glyphs.empty());

	const GlyphImage image = snowman->RasterizeGlyph(run.glyphs[0].glyphId);
	REQUIRE(image.width > 0);
	REQUIRE(image.height > 0);
	const bool hasCoverage = std::any_of(image.gray.begin(), image.gray.end(),
		[](uint8_t c) { return c > 0; });
	CHECK(hasCoverage);
}

TEST_CASE("color emoji face selects the fixed strike and scales metrics") {
	FontCache cache;
	const auto emoji = cache.LoadPath(emojiPath, FontParameters("fixture-emoji", 16.0));
	REQUIRE(emoji->UsesBitmapStrike());
	CHECK(emoji->StrikePpem() > 0.0);
	CHECK(emoji->RequestedSize() == 16.0);
	const double expectedScale = 16.0 / emoji->StrikePpem();
	CHECK(std::abs(emoji->MetricsScale() - expectedScale) < 1e-9);
	const FontMetrics metrics = emoji->Metrics();
	CHECK(metrics.height > 0.0);
	// Logical height is far smaller than the raw 109ppem strike height.
	CHECK(metrics.height < emoji->StrikePpem());
}

TEST_CASE("color emoji RasterizeGlyph returns premultiplied RGBA") {
	FontCache cache;
	const auto emoji = cache.LoadPath(emojiPath, FontParameters("fixture-emoji", 16.0));
	const std::string grinning = "\xF0\x9F\x98\x80";
	const ShapedRun run = ShapeText(grinning, emoji);
	REQUIRE_FALSE(run.glyphs.empty());

	const GlyphImage image = emoji->RasterizeGlyph(run.glyphs[0].glyphId);
	REQUIRE(image.kind == GlyphImageKind::Colour);
	REQUIRE(image.width > 0);
	REQUIRE(image.height > 0);
	REQUIRE(image.rgba.size() ==
		static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4u);
	CHECK(image.gray.empty());
	CHECK(std::abs(image.scale - emoji->MetricsScale()) < 1e-9);
	// Bearings are logical (scaled); pixel buffer stays at strike resolution.
	CHECK(image.width > static_cast<int>(std::lround(16.0)));

	bool sawOpaqueColour = false;
	bool channelsPremultiplied = true;
	for (size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
		const uint8_t r = image.rgba[i];
		const uint8_t g = image.rgba[i + 1];
		const uint8_t b = image.rgba[i + 2];
		const uint8_t a = image.rgba[i + 3];
		if (a > 0 && (r > 0 || g > 0 || b > 0)) {
			sawOpaqueColour = true;
		}
		// Premultiplied: each colour channel must be <= alpha.
		if (r > a || g > a || b > a) {
			channelsPremultiplied = false;
			break;
		}
	}
	CHECK(sawOpaqueColour);
	CHECK(channelsPremultiplied);
}

TEST_CASE("color emoji RasterizeGlyph is stable for the same glyph id") {
	FontCache cache;
	const auto emoji = cache.LoadPath(emojiPath, FontParameters("fixture-emoji", 16.0));
	const ShapedRun run = ShapeText("\xF0\x9F\x98\x80", emoji);
	REQUIRE_FALSE(run.glyphs.empty());
	const uint32_t glyphId = run.glyphs[0].glyphId;
	const GlyphImage first = emoji->RasterizeGlyph(glyphId);
	const GlyphImage second = emoji->RasterizeGlyph(glyphId);
	CHECK(first.kind == second.kind);
	CHECK(first.width == second.width);
	CHECK(first.height == second.height);
	CHECK(first.left == second.left);
	CHECK(first.top == second.top);
	CHECK(first.scale == second.scale);
	CHECK(first.rgba == second.rgba);
}

TEST_CASE("color emoji shaped advance matches requested size scale") {
	FontCache cache;
	const auto emoji = cache.LoadPath(emojiPath, FontParameters("fixture-emoji", 16.0));
	const ShapedRun run = ShapeText("\xF0\x9F\x98\x80", emoji);
	REQUIRE_FALSE(run.glyphs.empty());
	// Strike is ~109ppem; logical advance at 16px is well below the raw bitmap width.
	CHECK(run.Width() > 0.0);
	CHECK(run.Width() < emoji->StrikePpem());
	CHECK(run.glyphs[0].xAdvance == run.Width());
}

TEST_CASE("ShapeText keeps multi-byte clusters and caret stops") {
	FontCache cache;
	const auto snowman = LoadSnowman(cache);
	// U+2603 SNOWMAN is three UTF-8 bytes: e2 98 83
	const std::string text = "\xE2\x98\x83";
	const ShapedRun run = ShapeText(text, snowman);

	REQUIRE(run.byteEndPositions.size() == 3);
	CHECK(run.byteEndPositions[0] == run.byteEndPositions[1]);
	CHECK(run.byteEndPositions[1] == run.byteEndPositions[2]);
	CHECK(run.byteEndPositions[0] > 0.0);
	// Caret only at start and end; not on trail bytes.
	REQUIRE(run.caretStops == std::vector<size_t>{0, 3});
	REQUIRE_FALSE(run.glyphs.empty());
	CHECK(run.glyphs[0].cluster == 0);
}

TEST_CASE("ShapeText treats each invalid UTF-8 byte as one character") {
	FontCache cache;
	const auto face = LoadPrimary(cache);
	// 0xFF is never a valid UTF-8 lead or trail alone in a useful sequence here.
	const std::string text = "A\xFF" "B";
	const ShapedRun run = ShapeText(text, face);

	REQUIRE(run.byteEndPositions.size() == 3);
	CHECK(MonotonicEnds(run.byteEndPositions));
	REQUIRE(run.caretStops == std::vector<size_t>{0, 1, 2, 3});
	// Three clusters: A, invalid byte, B.
	std::vector<size_t> clusters;
	for (const ShapedGlyph &glyph : run.glyphs) {
		if (clusters.empty() || clusters.back() != glyph.cluster) {
			clusters.push_back(glyph.cluster);
		}
	}
	REQUIRE(clusters.size() == 3);
	CHECK(clusters[0] == 0);
	CHECK(clusters[1] == 1);
	CHECK(clusters[2] == 2);
}

TEST_CASE("ShapeText splits fallback spans without losing byte offsets") {
	FontCache cache;
	const auto primary = LoadPrimary(cache);
	const auto snowman = LoadSnowman(cache);
	// A (primary), snowman (fallback), B (primary)
	const std::string text = std::string("A") + "\xE2\x98\x83" + "B";
	const ShapedRun run = ShapeText(text, primary, {snowman});

	REQUIRE(run.byteEndPositions.size() == 5);
	CHECK(MonotonicEnds(run.byteEndPositions));
	// Caret stops: before A, before snowman (byte 1), before B (byte 4), end (5).
	REQUIRE(run.caretStops == std::vector<size_t>{0, 1, 4, 5});

	const ShapedGlyph *snowmanGlyph = nullptr;
	for (const ShapedGlyph &glyph : run.glyphs) {
		if (glyph.cluster == 1) {
			snowmanGlyph = &glyph;
			break;
		}
	}
	REQUIRE(snowmanGlyph != nullptr);
	CHECK(snowmanGlyph->face == snowman);

	bool sawPrimaryA = false;
	bool sawPrimaryB = false;
	for (const ShapedGlyph &glyph : run.glyphs) {
		if (glyph.cluster == 0) {
			CHECK(glyph.face == primary);
			sawPrimaryA = true;
		}
		if (glyph.cluster == 4) {
			CHECK(glyph.face == primary);
			sawPrimaryB = true;
		}
	}
	CHECK(sawPrimaryA);
	CHECK(sawPrimaryB);
}

TEST_CASE("hint policy extraction uses Fontconfig defaults for a null pattern") {
	const FontRasterPolicy policy = RasterPolicyFromFontconfigPattern(nullptr);
	CHECK(policy.antialias);
	CHECK(policy.hintStyle == FontHintStyle::Normal);
	CHECK(policy.FreeTypeLoadFlags() == (FT_LOAD_COLOR | FT_LOAD_TARGET_NORMAL));
}

TEST_CASE("hint policy extraction maps Fontconfig antialias and hint styles") {
	FcPattern *pattern = FcPatternCreate();
	REQUIRE(pattern != nullptr);

	// Absent properties → documented defaults.
	{
		const FontRasterPolicy policy = RasterPolicyFromFontconfigPattern(pattern);
		CHECK(policy.antialias);
		CHECK(policy.hintStyle == FontHintStyle::Normal);
	}

	FcPatternAddBool(pattern, FC_ANTIALIAS, FcFalse);
	FcPatternAddBool(pattern, FC_HINTING, FcFalse);
	{
		const FontRasterPolicy policy = RasterPolicyFromFontconfigPattern(pattern);
		CHECK_FALSE(policy.antialias);
		CHECK(policy.hintStyle == FontHintStyle::None);
		CHECK(policy.FreeTypeLoadFlags() == (FT_LOAD_COLOR | FT_LOAD_NO_HINTING));
	}

	// Clear and rebuild for slight hinting with antialias on.
	FcPatternDestroy(pattern);
	pattern = FcPatternCreate();
	REQUIRE(pattern != nullptr);
	FcPatternAddBool(pattern, FC_ANTIALIAS, FcTrue);
	FcPatternAddBool(pattern, FC_HINTING, FcTrue);
	FcPatternAddInteger(pattern, FC_HINT_STYLE, FC_HINT_SLIGHT);
	{
		const FontRasterPolicy policy = RasterPolicyFromFontconfigPattern(pattern);
		CHECK(policy.antialias);
		CHECK(policy.hintStyle == FontHintStyle::Slight);
		CHECK(policy.FreeTypeLoadFlags() == (FT_LOAD_COLOR | FT_LOAD_TARGET_LIGHT));
	}

	FcPatternDel(pattern, FC_HINT_STYLE);
	FcPatternAddInteger(pattern, FC_HINT_STYLE, FC_HINT_NONE);
	{
		const FontRasterPolicy policy = RasterPolicyFromFontconfigPattern(pattern);
		CHECK(policy.hintStyle == FontHintStyle::None);
	}

	FcPatternDel(pattern, FC_HINT_STYLE);
	FcPatternAddInteger(pattern, FC_HINT_STYLE, FC_HINT_MEDIUM);
	{
		const FontRasterPolicy policy = RasterPolicyFromFontconfigPattern(pattern);
		CHECK(policy.hintStyle == FontHintStyle::Normal);
	}

	FcPatternDel(pattern, FC_HINT_STYLE);
	FcPatternAddInteger(pattern, FC_HINT_STYLE, FC_HINT_FULL);
	{
		const FontRasterPolicy policy = RasterPolicyFromFontconfigPattern(pattern);
		CHECK(policy.hintStyle == FontHintStyle::Normal);
	}

	// Invalid style collapses to Normal.
	FcPatternDel(pattern, FC_HINT_STYLE);
	FcPatternAddInteger(pattern, FC_HINT_STYLE, 99);
	{
		const FontRasterPolicy policy = RasterPolicyFromFontconfigPattern(pattern);
		CHECK(policy.hintStyle == FontHintStyle::Normal);
	}

	FcPatternDestroy(pattern);
}

TEST_CASE("hint policy FreeType load flags select none slight and normal targets") {
	CHECK(Policy(FontHintStyle::None).FreeTypeLoadFlags() ==
		(FT_LOAD_COLOR | FT_LOAD_NO_HINTING));
	CHECK(Policy(FontHintStyle::Slight).FreeTypeLoadFlags() ==
		(FT_LOAD_COLOR | FT_LOAD_TARGET_LIGHT));
	CHECK(Policy(FontHintStyle::Normal).FreeTypeLoadFlags() ==
		(FT_LOAD_COLOR | FT_LOAD_TARGET_NORMAL));
	// antialias false does not switch to monochrome load flags.
	CHECK(Policy(FontHintStyle::Slight, false).FreeTypeLoadFlags() ==
		(FT_LOAD_COLOR | FT_LOAD_TARGET_LIGHT));
}

TEST_CASE("hinted fixture faces share load flags between HarfBuzz and RasterizeGlyph") {
	FontCache cache;
	const auto normal = LoadPrimaryWithPolicy(cache, FontHintStyle::Normal);
	const auto light = LoadPrimaryWithPolicy(cache, FontHintStyle::Slight);
	const auto unhinted = LoadPrimaryWithPolicy(cache, FontHintStyle::None);

	CHECK(normal->RasterPolicy().hintStyle == FontHintStyle::Normal);
	CHECK(light->RasterPolicy().hintStyle == FontHintStyle::Slight);
	CHECK(unhinted->RasterPolicy().hintStyle == FontHintStyle::None);

	// Distinct face identities so cache keys include the raster policy.
	CHECK(normal.get() != light.get());
	CHECK(normal.get() != unhinted.get());
	CHECK(light.get() != unhinted.get());

	for (const auto &face : {normal, light, unhinted}) {
		hb_font_t *hbFont = static_cast<hb_font_t *>(face->HarfBuzzFont());
		REQUIRE(hbFont != nullptr);
		CHECK(hb_ft_font_get_load_flags(hbFont) == face->FreeTypeLoadFlags());
		CHECK(hb_ft_font_get_load_flags(hbFont) == face->RasterPolicy().FreeTypeLoadFlags());
		CHECK(hb_ft_font_get_load_flags(hbFont) & FT_LOAD_COLOR);
	}
	CHECK(hb_ft_font_get_load_flags(static_cast<hb_font_t *>(light->HarfBuzzFont())) &
		FT_LOAD_TARGET_LIGHT);
	CHECK(hb_ft_font_get_load_flags(static_cast<hb_font_t *>(unhinted->HarfBuzzFont())) &
		FT_LOAD_NO_HINTING);
}

TEST_CASE("hinted RasterizeGlyph coverage differs for normal light and unhinted policies") {
	// Editor body size is 16pt at 96 DPI → ~21.33 logical pixels.
	const double editorPixels = 16.0 * 96.0 / 72.0;
	FontCache cache;
	const auto normal = LoadPrimaryWithPolicy(cache, FontHintStyle::Normal, editorPixels);
	const auto light = LoadPrimaryWithPolicy(cache, FontHintStyle::Slight, editorPixels);
	const auto unhinted = LoadPrimaryWithPolicy(cache, FontHintStyle::None, editorPixels);

	const ShapedRun shaped = ShapeText("H", normal);
	REQUIRE_FALSE(shaped.glyphs.empty());
	const uint32_t glyphId = shaped.glyphs[0].glyphId;

	const GlyphImage normalImage = normal->RasterizeGlyph(glyphId);
	const GlyphImage lightImage = light->RasterizeGlyph(glyphId);
	const GlyphImage unhintedImage = unhinted->RasterizeGlyph(glyphId);

	REQUIRE(normalImage.kind == GlyphImageKind::Gray);
	REQUIRE(lightImage.kind == GlyphImageKind::Gray);
	REQUIRE(unhintedImage.kind == GlyphImageKind::Gray);
	REQUIRE(normalImage.width > 0);
	REQUIRE(lightImage.width > 0);
	REQUIRE(unhintedImage.width > 0);

	// Light and normal targets must not produce identical coverage on this fixture.
	CHECK(normalImage.gray != lightImage.gray);
	CHECK(normalImage.gray != unhintedImage.gray);
	CHECK(lightImage.gray != unhintedImage.gray);
	CHECK(CoverageSum(normalImage) != CoverageSum(lightImage));
}

TEST_CASE("light-hinted RasterizeGlyph is stable for the same glyph id") {
	const double editorPixels = 16.0 * 96.0 / 72.0;
	FontCache cache;
	const auto face = LoadPrimaryWithPolicy(cache, FontHintStyle::Slight, editorPixels);
	const ShapedRun run = ShapeText("standards", face);
	REQUIRE_FALSE(run.glyphs.empty());
	const uint32_t glyphId = run.glyphs[0].glyphId;

	const GlyphImage first = face->RasterizeGlyph(glyphId);
	const GlyphImage second = face->RasterizeGlyph(glyphId);
	REQUIRE(first.kind == GlyphImageKind::Gray);
	CHECK(first.width == second.width);
	CHECK(first.height == second.height);
	CHECK(first.left == second.left);
	CHECK(first.top == second.top);
	CHECK(first.gray == second.gray);
	CHECK(CoverageSum(first) > 0);
}
