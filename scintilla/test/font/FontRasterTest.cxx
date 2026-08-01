#include "FontTest.h"

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
