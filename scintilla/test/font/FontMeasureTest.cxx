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
