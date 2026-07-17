// scalpel-editor font selection, ownership, and shaped-run tests.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "EditorStyleTypes.h"
#include "FontPlatform.h"
#include "Geometry.h"
#include "Platform.h"
#include "ShapedLayout.h"
#include "ShapedRun.h"

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

using namespace Scintilla;
using namespace Scintilla::Internal;

namespace {

const std::filesystem::path fontDirectory = SCALPEL_TEST_FONT_DIR;
const std::filesystem::path primaryPath = fontDirectory / "FallbackPrimary.ttf";
const std::filesystem::path snowmanPath = fontDirectory / "FallbackSnowman.ttf";

std::shared_ptr<FontFace> LoadPrimary(FontCache &cache, double size = 16.0) {
	return cache.LoadPath(primaryPath, FontParameters("fixture", size));
}

std::shared_ptr<FontFace> LoadSnowman(FontCache &cache, double size = 16.0) {
	return cache.LoadPath(snowmanPath, FontParameters("fixture", size));
}

bool MonotonicEnds(const std::vector<XYPOSITION> &ends) {
	for (size_t i = 1; i < ends.size(); i++) {
		if (ends[i] + 1e-9 < ends[i - 1]) {
			return false;
		}
	}
	return true;
}

}

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

TEST_CASE("Fixture faces own a HarfBuzz font") {
	FontCache cache;
	const auto face = LoadPrimary(cache);
	REQUIRE(face->HarfBuzzFont() != nullptr);
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

namespace {

// Minimal IScreenLine for LayoutScreenLine tests.
class FakeScreenLine final : public IScreenLine {
public:
	std::string text;
	std::shared_ptr<Font> font;
	std::vector<XYPOSITION> representationWidths;
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
};

}

TEST_CASE("LayoutScreenLine places ASCII and agrees with shaped widths") {
	FontCache fonts;
	const auto face = LoadPrimary(fonts);
	FakeScreenLine line;
	line.font = FontFromFace(face);
	line.text = "AB";
	line.representationWidths.assign(line.text.size(), 0.0);

	const auto layout = LayoutScreenLine(&line);
	const ShapedRun run = ShapeText("AB", face);
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

	const auto layout = LayoutScreenLine(&line);
	const ShapedRun run = ShapeText(line.text, snowman);
	REQUIRE(run.caretStops == std::vector<size_t>{0, 3});
	CHECK(layout->XFromPosition(0) == 0.0);
	CHECK(layout->XFromPosition(1) == run.Width());
	CHECK(layout->XFromPosition(2) == run.Width());
	CHECK(layout->XFromPosition(3) == run.Width());
	// Left half of the glyph (strictly before the mid-cell) maps to the start.
	const size_t hit = layout->PositionFromX(run.Width() / 2.0 - 0.01, false);
	CHECK(hit == 0);
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

	const auto layout = LayoutScreenLine(&line);
	const ShapedRun a = ShapeText("A", face);
	const ShapedRun b = ShapeText("B", face);
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
