#include "FontTest.h"

namespace {

// Minimal IScreenLine for LayoutScreenLine tests.
class FakeScreenLine final : public IScreenLine {
public:
	std::string text;
	std::shared_ptr<Font> font;
	std::vector<XYPOSITION> representationWidths;
	std::vector<XYPOSITION> positions;
	XYPOSITION tabWidth = 40.0;
	int tabMinPixels = 2;

	std::string_view Text() const override { return text; }
	size_t Length() const override { return text.size(); }
	size_t RepresentationCount() const override {
		return static_cast<size_t>(std::count_if(
			representationWidths.begin(), representationWidths.end(),
			[](XYPOSITION w) { return w > 0.0; }));
	}
	XYPOSITION Width() const override { return 1000.0; }
	XYPOSITION Height() const override { return 20.0; }
	XYPOSITION TabWidth() const override { return tabWidth; }
	XYPOSITION TabWidthMinimumPixels() const override {
		return static_cast<XYPOSITION>(tabMinPixels);
	}
	const Font *FontOfPosition(size_t) const override { return font.get(); }
	XYPOSITION RepresentationWidth(size_t position) const override {
		if (position >= representationWidths.size()) {
			return 0.0;
		}
		return representationWidths[position];
	}
	XYPOSITION TabPositionAfter(XYPOSITION xPosition) const override {
		return (std::floor((xPosition + TabWidthMinimumPixels()) / TabWidth()) + 1.0) * TabWidth();
	}
	XYPOSITION XFromPosition(size_t position) const override {
		if (positions.empty()) {
			return 0.0;
		}
		return position < positions.size() ? positions[position] : positions.back();
	}
};

void SetPositions(FakeScreenLine &line, const ShapedRun &run) {
	line.positions.assign(1, 0.0);
	line.positions.insert(line.positions.end(),
		run.byteEndPositions.begin(), run.byteEndPositions.end());
}

}

TEST_CASE("LayoutScreenLine places ASCII and agrees with shaped widths") {
	FontCache fonts;
	const auto face = LoadPrimary(fonts);
	FakeScreenLine line;
	line.font = FontFromFace(face);
	line.text = "AB";
	line.representationWidths.assign(line.text.size(), 0.0);

	const ShapedRun run = ShapeText("AB", face);
	SetPositions(line, run);
	const auto layout = LayoutScreenLine(&line);
	CHECK(layout->XFromPosition(0) == 0.0);
	CHECK(layout->XFromPosition(1) == run.byteEndPositions[0]);
	CHECK(layout->XFromPosition(2) == run.Width());
	CHECK(layout->PositionFromX(0.0, true) == 0);
	CHECK(layout->PositionFromX(run.Width() + 1.0, true) == 2);
}

TEST_CASE("LayoutScreenLine keeps multi-byte cluster ends and caret stops") {
	FontCache fonts;
	const auto snowman = LoadSnowman(fonts);
	FakeScreenLine line;
	line.font = FontFromFace(snowman);
	line.text = "\xE2\x98\x83";
	line.representationWidths.assign(line.text.size(), 0.0);

	const ShapedRun run = ShapeText(line.text, snowman);
	SetPositions(line, run);
	const auto layout = LayoutScreenLine(&line);
	REQUIRE(run.caretStops == std::vector<size_t>{0, 3});
	CHECK(layout->XFromPosition(0) == 0.0);
	CHECK(layout->XFromPosition(1) == run.Width());
	CHECK(layout->XFromPosition(2) == run.Width());
	CHECK(layout->XFromPosition(3) == run.Width());
	// Left half of the glyph (strictly before the mid-cell) maps to the start.
	const size_t hit = layout->PositionFromX(run.Width() / 2.0 - 0.01, false);
	CHECK(hit == 0);
	CHECK(layout->PositionFromX(run.Width() / 2.0 + 0.01, false) == 3);
	// Trail byte offsets are not caret stops; X equals the cluster end.
	for (size_t stop : run.caretStops) {
		if (stop == 0) {
			CHECK(layout->XFromPosition(stop) == 0.0);
		} else {
			CHECK(layout->XFromPosition(stop) == run.Width());
		}
	}
}

TEST_CASE("LayoutScreenLine expands tabs and fixed representations") {
	FontCache fonts;
	const auto face = LoadPrimary(fonts);
	FakeScreenLine line;
	line.font = FontFromFace(face);
	line.text = "A\tB";
	line.representationWidths = {0.0, 0.0, 0.0};
	line.tabWidth = 40.0;

	const ShapedRun a = ShapeText("A", face);
	const ShapedRun b = ShapeText("B", face);
	line.positions = {0.0, a.Width(), 40.0, 40.0 + b.Width()};
	const auto layout = LayoutScreenLine(&line);
	CHECK(layout->XFromPosition(0) == 0.0);
	CHECK(layout->XFromPosition(1) == a.Width());
	// Tab from after A goes to the next multiple of 40.
	const XYPOSITION afterTab = layout->XFromPosition(2);
	CHECK(afterTab == 40.0);
	CHECK(layout->XFromPosition(3) == Approx(40.0 + b.Width()));

	// Representation replaces one character with a fixed width.
	FakeScreenLine withRepr;
	withRepr.font = line.font;
	withRepr.text = std::string("A") + '\x7f' + 'B';
	REQUIRE(withRepr.text.size() == 3);
	withRepr.representationWidths = {0.0, 12.0, 0.0};
	withRepr.positions = {0.0, a.Width(), a.Width() + 12.0, a.Width() + 12.0 + b.Width()};
	const auto reprLayout = LayoutScreenLine(&withRepr);
	CHECK(reprLayout->XFromPosition(1) == a.Width());
	CHECK(reprLayout->XFromPosition(2) == Approx(a.Width() + 12.0));
	CHECK(reprLayout->XFromPosition(3) == Approx(a.Width() + 12.0 + b.Width()));
}

TEST_CASE("LayoutScreenLine selection interval is one LTR range") {
	FontCache fonts;
	const auto face = LoadPrimary(fonts);
	FakeScreenLine line;
	line.font = FontFromFace(face);
	line.text = "ABCD";
	line.representationWidths.assign(line.text.size(), 0.0);
	SetPositions(line, ShapeText(line.text, face));

	const auto layout = LayoutScreenLine(&line);
	const auto intervals = layout->FindRangeIntervals(1, 3);
	REQUIRE(intervals.size() == 1);
	CHECK(intervals[0].left == layout->XFromPosition(1));
	CHECK(intervals[0].right == layout->XFromPosition(3));
}

TEST_CASE("ShapedRunCache returns the same run for the same key") {
	FontCache fonts;
	const auto face = LoadPrimary(fonts);
	ShapedRunCache cache(8);

	const auto first = cache.Get("AV", face);
	const auto again = cache.Get("AV", face);
	CHECK(first == again);
	CHECK(cache.Size() == 1);

	const auto other = cache.Get("AB", face);
	CHECK(other != first);
	CHECK(cache.Size() == 2);
	CHECK(other->text != first->text);
}

TEST_CASE("ShapedRunCache keeps returned runs alive after eviction") {
	FontCache fonts;
	const auto face = LoadPrimary(fonts);
	ShapedRunCache cache(2);

	const auto a = cache.Get("A", face);
	const auto b = cache.Get("B", face);
	CHECK(cache.Size() == 2);
	// Touch A so B is the older entry when C arrives.
	const XYPOSITION aWidth = cache.Get("A", face)->Width();
	cache.Get("C", face);
	CHECK(cache.Size() == 2);

	// Recreating B evicts A from the cache, but the returned run remains valid.
	const auto bAgain = cache.Get("B", face);
	CHECK(bAgain != b);
	CHECK(a->text == "A");
	CHECK(a->Width() == aWidth);
}

TEST_CASE("ShapedRunCache distinguishes face objects") {
	FontCache firstFonts;
	FontCache secondFonts;
	const auto firstFace = LoadPrimary(firstFonts);
	const auto secondFace = LoadPrimary(secondFonts);
	REQUIRE(firstFace != secondFace);
	ShapedRunCache cache;

	const auto first = cache.Get("A", firstFace);
	const auto second = cache.Get("A", secondFace);

	CHECK(first != second);
	CHECK(cache.Size() == 2);
	REQUIRE_FALSE(second->glyphs.empty());
	CHECK(second->glyphs.front().face == secondFace);
}
