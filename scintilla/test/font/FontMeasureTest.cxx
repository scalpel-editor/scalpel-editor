#include "FontTest.h"

TEST_CASE("FillMeasureWidths matches the Surface MeasureWidths contract") {
	FontCache cache;
	const auto face = LoadPrimary(cache);
	const ShapedRun run = ShapeText("AB", face);
	std::vector<XYPOSITION> positions(2, -1.0);
	FillMeasureWidths(run, positions.data());

	CHECK(positions[0] == run.byteEndPositions[0]);
	CHECK(positions[1] == run.byteEndPositions[1]);
}

TEST_CASE("MeasureWidthsShaped and WidthTextShaped match ShapeText") {
	FontCache fonts;
	const auto face = LoadPrimary(fonts);
	const ShapedRun run = ShapeText("AV", face);
	std::vector<XYPOSITION> positions(2, 0.0);
	MeasureWidthsShaped("AV", face, {}, positions.data());

	CHECK(positions[0] == run.byteEndPositions[0]);
	CHECK(positions[1] == run.byteEndPositions[1]);
	CHECK(WidthTextShaped("AV", face) == run.Width());
	CHECK(WidthTextShaped("", face) == 0.0);
}

TEST_CASE("MeasureWidthsShaped keeps multi-byte cluster end positions") {
	FontCache fonts;
	const auto snowman = LoadSnowman(fonts);
	const std::string text = "\xE2\x98\x83";
	std::vector<XYPOSITION> positions(3, 0.0);
	MeasureWidthsShaped(text, snowman, {}, positions.data());

	CHECK(positions[0] == positions[1]);
	CHECK(positions[1] == positions[2]);
	CHECK(positions[0] > 0.0);
	CHECK(WidthTextShaped(text, snowman) == positions[2]);
}

TEST_CASE("MeasureWidthsShaped uses the shaped-run cache when given") {
	FontCache fonts;
	const auto face = LoadPrimary(fonts);
	ShapedRunCache cache(8);
	std::vector<XYPOSITION> first(2, 0.0);
	std::vector<XYPOSITION> second(2, 0.0);
	MeasureWidthsShaped("AB", face, {}, first.data(), &cache);
	CHECK(cache.Size() == 1);
	MeasureWidthsShaped("AB", face, {}, second.data(), &cache);
	CHECK(cache.Size() == 1);
	CHECK(first == second);
	CHECK(WidthTextShaped("AB", face, {}, &cache) == first[1]);
}

TEST_CASE("ShapeText long run keeps monotonic ends and matches WidthText") {
	FontCache fonts;
	const auto face = LoadPrimary(fonts);
	// Longer than BreakFinder lengthEachSubdivision (100) so layout subdivides;
	// ShapeText itself still shapes the full string in one call.
	const std::string text(320, 'A');
	const ShapedRun run = ShapeText(text, face);
	REQUIRE(run.byteEndPositions.size() == text.size());
	CHECK(MonotonicEnds(run.byteEndPositions));
	REQUIRE(run.caretStops.size() == text.size() + 1);
	CHECK(run.caretStops.front() == 0);
	CHECK(run.caretStops.back() == text.size());
	CHECK(WidthTextShaped(text, face) == run.Width());
	CHECK(run.Width() > 0.0);
	// Sum of glyph advances equals the reported width (no lost clusters).
	XYPOSITION glyphSum = 0.0;
	for (const ShapedGlyph &glyph : run.glyphs) {
		glyphSum += glyph.xAdvance;
	}
	CHECK(glyphSum == run.Width());
}

TEST_CASE("ShapedRunCache capacity bounds retained entries under churn") {
	FontCache fonts;
	const auto face = LoadPrimary(fonts);
	ShapedRunCache cache(4);
	for (int i = 0; i < 12; i++) {
		const std::string text(static_cast<size_t>(i + 1), 'B');
		const auto run = cache.Get(text, face);
		REQUIRE(run);
		CHECK(run->text == text);
		CHECK(cache.Size() <= cache.Capacity());
	}
	CHECK(cache.Size() == 4);
}
