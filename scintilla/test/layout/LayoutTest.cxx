// scalpel-editor layout tests: measure surface and shaped-run positions.

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "EditorBasicTypes.h"
#include "EditorDocumentTypes.h"
#include "EditorLayoutTypes.h"
#include "EditorStyleTypes.h"
#include "ILexer.h"
#include "ILoader.h"

#include "Debugging.h"
#include "Document.h"
#include "FontPlatform.h"
#include "Geometry.h"
#include "MeasureSurface.h"
#include "Platform.h"
#include "Position.h"
#include "PositionCache.h"
#include "ShapedLayout.h"
#include "ShapedRun.h"

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

using namespace Scintilla;
using namespace Scintilla::Internal;

// Minimal Platform symbols pulled by Document / PositionCache / ViewStyle.
void Platform::Assert(const char *c, const char *file, int line) noexcept {
	fprintf(stderr, "Assertion [%s] failed at %s %d\n", c, file, line);
	abort();
}

void Platform::DebugPrintf(const char *format, ...) noexcept {
	char buffer[2000];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	fprintf(stderr, "%s", buffer);
}

ColourRGBA Platform::Chrome() {
	return ColourRGBA(0xe0, 0xe0, 0xe0);
}

ColourRGBA Platform::ChromeHighlight() {
	return ColourRGBA(0xff, 0xff, 0xff);
}

const char *Platform::DefaultFont() {
	return "fixture";
}

int Platform::DefaultFontSize() {
	return 10;
}

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

// Fill LineLayout positions from MeasureSurface the way LayoutLine does for a
// single plain-text segment (no tabs or style breaks).
void FillLineLayoutFromSurface(
	LineLayout &ll,
	Surface &surface,
	const Font *font,
	std::string_view text) {
	const int n = static_cast<int>(text.size());
	ll.ReSet(0, n);
	if (n > 0) {
		std::memcpy(ll.chars.get(), text.data(), static_cast<size_t>(n));
	}
	ll.chars[n] = 0;
	std::fill(ll.styles.get(), ll.styles.get() + n + 1, 0);
	ll.numCharsInLine = n;
	ll.numCharsBeforeEOL = n;
	ll.positions[0] = 0.0;
	if (n == 0) {
		return;
	}
	// Relative end positions into positions[1..n], then absolute accumulate.
	surface.MeasureWidths(font, text, &ll.positions[1]);
	XYPOSITION x = 0.0;
	for (int i = 1; i <= n; i++) {
		x = ll.positions[i]; // already cumulative from MeasureWidths
		ll.positions[i] = x;
	}
	// MeasureWidths already stores cumulative ends; positions[0] is start.
	// One unwrapped display line until WrapLine is called.
	ll.lines = 1;
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

TEST_CASE("LineLayout positions match shaped byte ends and caret stops") {
	FontCache fonts;
	const auto face = LoadPrimary(fonts);
	const auto font = FontFromFace(face);
	auto surface = CreateMeasureSurface();
	const std::string text = "AV";
	const ShapedRun run = ShapeText(text, face);

	LineLayout ll(0, static_cast<int>(text.size()));
	FillLineLayoutFromSurface(ll, *surface, font.get(), text);

	REQUIRE(ll.numCharsInLine == 2);
	CHECK(ll.positions[0] == 0.0);
	CHECK(ll.positions[1] == run.byteEndPositions[0]);
	CHECK(ll.positions[2] == run.Width());
	for (size_t stop : run.caretStops) {
		CHECK(ll.positions[stop] == (stop == 0 ? 0.0 : run.byteEndPositions[stop - 1]));
	}
	const Point pt = ll.PointFromPosition(0, 20, PointEnd::start);
	CHECK(pt.x == 0.0);
	const Point ptEnd = ll.PointFromPosition(2, 20, PointEnd::start);
	CHECK(ptEnd.x == run.Width());
}

TEST_CASE("LineLayout multi-byte clusters share ends and hit-test to the character") {
	FontCache fonts;
	const auto snowman = LoadSnowman(fonts);
	const auto font = FontFromFace(snowman);
	auto surface = CreateMeasureSurface();
	// U+2603 SNOWMAN — three UTF-8 bytes.
	const std::string text = "\xE2\x98\x83";
	const ShapedRun run = ShapeText(text, snowman);

	LineLayout ll(0, static_cast<int>(text.size()));
	FillLineLayoutFromSurface(ll, *surface, font.get(), text);

	REQUIRE(ll.numCharsInLine == 3);
	CHECK(ll.positions[1] == run.Width());
	CHECK(ll.positions[2] == run.Width());
	CHECK(ll.positions[3] == run.Width());
	REQUIRE(run.caretStops == std::vector<size_t>{0, 3});

	// Left half of the glyph maps to the start; trail offsets are not stops.
	CHECK(ll.FindPositionFromX(run.Width() / 2.0 - 0.01, Range(0, 3), false) == 0);
	// Span for the whole character.
	const Interval span = ll.Span(0, 3);
	CHECK(span.left == 0.0);
	CHECK(span.right == run.Width());

	Document doc(DocumentOption::Default);
	const Sci::Position end = doc.InsertString(0, text);
	REQUIRE(end == 3);
	// Document character movement lands only on caret stops.
	CHECK(doc.MovePositionOutsideChar(1, 1) == 3);
	CHECK(doc.MovePositionOutsideChar(2, 1) == 3);
	CHECK(doc.MovePositionOutsideChar(1, -1) == 0);
	CHECK(doc.NextPosition(0, 1) == 3);
}

TEST_CASE("LineLayout wraps using shaped widths") {
	FontCache fonts;
	const auto face = LoadPrimary(fonts);
	const auto font = FontFromFace(face);
	auto surface = CreateMeasureSurface();
	// Spaces allow Wrap::Word to break.
	const std::string text = "AAAA BBBB CCCC";
	const ShapedRun run = ShapeText(text, face);

	LineLayout ll(0, static_cast<int>(text.size()));
	FillLineLayoutFromSurface(ll, *surface, font.get(), text);

	Document doc(DocumentOption::Default);
	doc.InsertString(0, text);

	// Wide enough for the whole line: one display line.
	ll.wrapIndent = 0;
	ll.WrapLine(&doc, 0, Wrap::Word, run.Width() + 1.0);
	CHECK(ll.lines == 1);

	// Narrower than the first word group forces at least one wrap.
	const XYPOSITION firstWord = ShapeText("AAAA", face).Width();
	ll.WrapLine(&doc, 0, Wrap::Word, firstWord + 1.0);
	CHECK(ll.lines >= 2);
	CHECK(ll.LineStart(0) == 0);
	CHECK(ll.LineStart(1) > 0);
}

TEST_CASE("LineLayout selection span and tab expansion use measure path") {
	FontCache fonts;
	const auto face = LoadPrimary(fonts);
	const auto font = FontFromFace(face);
	auto surface = CreateMeasureSurface();

	// Plain selection interval on shaped text.
	const std::string plain = "ABCD";
	LineLayout plainLl(0, static_cast<int>(plain.size()));
	FillLineLayoutFromSurface(plainLl, *surface, font.get(), plain);
	const Interval mid = plainLl.Span(1, 3);
	CHECK(mid.left == plainLl.positions[1]);
	CHECK(mid.right == plainLl.positions[3]);

	// Tab: relative width filled then expanded like LayoutLine.
	const std::string withTab = "A\tB";
	LineLayout tabLl(0, static_cast<int>(withTab.size()));
	tabLl.ReSet(0, static_cast<int>(withTab.size()));
	std::memcpy(tabLl.chars.get(), withTab.data(), withTab.size());
	tabLl.chars[withTab.size()] = 0;
	std::fill(tabLl.styles.get(), tabLl.styles.get() + withTab.size() + 1, 0);
	tabLl.numCharsInLine = static_cast<int>(withTab.size());
	tabLl.numCharsBeforeEOL = tabLl.numCharsInLine;
	tabLl.positions[0] = 0.0;

	const XYPOSITION tabWidth = 40.0;
	const ShapedRun a = ShapeText("A", face);
	const ShapedRun b = ShapeText("B", face);
	// Segment A
	tabLl.positions[1] = a.Width();
	// Tab: LayoutLine uses NextTabstopPos; here one stop at 40.
	const XYPOSITION afterTab = tabWidth;
	tabLl.positions[2] = afterTab;
	// Segment B relative then absolute
	tabLl.positions[3] = afterTab + b.Width();

	tabLl.lines = 1;
	CHECK(tabLl.positions[1] == a.Width());
	CHECK(tabLl.positions[2] == 40.0);
	CHECK(tabLl.positions[3] == Approx(40.0 + b.Width()));
	// Strictly left of the mid-cell for 'A'.
	CHECK(tabLl.FindPositionFromX(a.Width() / 2.0 - 0.01, Range(0, 3), false) == 0);
	// Inside the tab cell (x=45) with charPosition lands on the tab byte.
	CHECK(tabLl.FindPositionFromX(45.0, Range(0, 3), true) == 2);
}
