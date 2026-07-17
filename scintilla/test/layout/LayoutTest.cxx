// scalpel-editor layout tests: measure surface and shaped-run positions.

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "EditorStyleTypes.h"
#include "FontPlatform.h"
#include "Geometry.h"
#include "MeasureSurface.h"
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

class FakeScreenLine final : public IScreenLine {
public:
	std::string text;
	std::shared_ptr<Font> font;
	std::vector<XYPOSITION> representationWidths;
	XYPOSITION tabWidth = 40.0;
	int tabMinPixels = 2;

	std::string_view Text() const override { return text; }
	size_t Length() const override { return text.size(); }
	size_t RepresentationCount() const override { return 0; }
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

TEST_CASE("MeasureSurface widths match ShapeText") {
	FontCache fonts;
	const auto face = LoadPrimary(fonts);
	const auto font = FontFromFace(face);
	auto surface = CreateMeasureSurface();

	const ShapedRun run = ShapeText("AV", face);
	std::vector<XYPOSITION> positions(2, 0.0);
	surface->MeasureWidths(font.get(), "AV", positions.data());

	CHECK(positions[0] == run.byteEndPositions[0]);
	CHECK(positions[1] == run.byteEndPositions[1]);
	CHECK(surface->WidthText(font.get(), "AV") == run.Width());
	CHECK(surface->WidthText(font.get(), "") == 0.0);
}

TEST_CASE("MeasureSurface caches shaped runs across measure calls") {
	FontCache fonts;
	const auto face = LoadPrimary(fonts);
	const auto font = FontFromFace(face);
	auto surface = CreateMeasureSurface();
	auto *measure = dynamic_cast<MeasureSurface *>(surface.get());
	REQUIRE(measure != nullptr);

	std::vector<XYPOSITION> first(2, 0.0);
	std::vector<XYPOSITION> second(2, 0.0);
	surface->MeasureWidths(font.get(), "AB", first.data());
	CHECK(measure->RunCache().Size() == 1);
	surface->MeasureWidths(font.get(), "AB", second.data());
	CHECK(measure->RunCache().Size() == 1);
	CHECK(first == second);
	CHECK(surface->WidthText(font.get(), "AB") == first[1]);
}

TEST_CASE("MeasureSurface font metrics come from the fixture face") {
	FontCache fonts;
	const auto face = LoadPrimary(fonts);
	const auto font = FontFromFace(face);
	auto surface = CreateMeasureSurface();
	const FontMetrics metrics = face->Metrics();

	CHECK(surface->Ascent(font.get()) == metrics.ascent);
	CHECK(surface->Descent(font.get()) == metrics.descent);
	CHECK(surface->Height(font.get()) == metrics.height);
	CHECK(surface->InternalLeading(font.get()) == metrics.internalLeading);
	CHECK(surface->AverageCharWidth(font.get()) == surface->WidthText(font.get(), "x"));
}

TEST_CASE("MeasureSurface Layout matches LayoutScreenLine") {
	FontCache fonts;
	const auto face = LoadPrimary(fonts);
	FakeScreenLine line;
	line.font = FontFromFace(face);
	line.text = "AB";
	line.representationWidths.assign(2, 0.0);

	auto surface = CreateMeasureSurface();
	const auto viaSurface = surface->Layout(&line);
	const auto viaHelper = LayoutScreenLine(&line);
	CHECK(viaSurface->XFromPosition(0) == viaHelper->XFromPosition(0));
	CHECK(viaSurface->XFromPosition(1) == viaHelper->XFromPosition(1));
	CHECK(viaSurface->XFromPosition(2) == viaHelper->XFromPosition(2));
}

TEST_CASE("MeasureSurface Layout uses fallback faces for missing glyphs") {
	FontCache fonts;
	const auto primary = LoadPrimary(fonts);
	const auto snowman = LoadSnowman(fonts);
	const std::string text = std::string("A") + "\xE2\x98\x83" + "B";

	FakeScreenLine line;
	line.font = FontFromFace(primary);
	line.text = text;
	line.representationWidths.assign(text.size(), 0.0);

	auto surface = CreateMeasureSurface({snowman});
	const auto layout = surface->Layout(&line);
	const ShapedRun run = ShapeText(text, primary, {snowman});

	CHECK(layout->XFromPosition(0) == 0.0);
	CHECK(layout->XFromPosition(1) == run.byteEndPositions[0]);
	CHECK(layout->XFromPosition(5) == run.Width());
	REQUIRE(run.caretStops == std::vector<size_t>{0, 1, 4, 5});
	for (size_t stop : run.caretStops) {
		CHECK(layout->XFromPosition(stop) ==
			(stop == 0 ? 0.0 : run.byteEndPositions[stop - 1]));
	}
}
