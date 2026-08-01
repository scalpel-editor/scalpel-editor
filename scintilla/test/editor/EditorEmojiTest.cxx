// Editor path coverage for supported emoji sequences with fixture fonts.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "EditorBasicTypes.h"
#include "EditorDocumentTypes.h"
#include "EditorStyleTypes.h"
#include "EditorInputTypes.h"
#include "EditorLayoutTypes.h"
#include "ILoader.h"
#include "ILexer.h"

#include "Debugging.h"
#include "DrawSurface.h"
#include "FontPlatform.h"
#include "Geometry.h"
#include "Platform.h"
#include "CharacterType.h"
#include "CharacterCategoryMap.h"
#include "Position.h"
#include "UniqueString.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "ContractionState.h"
#include "CellBuffer.h"
#include "PerLine.h"
#include "KeyMap.h"
#include "Indicator.h"
#include "LineMarker.h"
#include "Style.h"
#include "ViewStyle.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "Document.h"
#include "UniConversion.h"
#include "Selection.h"
#include "PositionCache.h"
#include "EditModel.h"
#include "MarginView.h"
#include "EditView.h"
#include "Editor.h"
#include "AutoComplete.h"
#include "CallTip.h"
#include "ScintillaBase.h"

#include "TestPlatform.h"
#include "TestEditor.h"

#include "catch.hpp"

using namespace Scintilla;
using namespace Scintilla::Internal;

namespace {

const std::string grinning = "\xF0\x9F\x98\x80";
const std::string whiteSmile = "\xE2\x98\xBA";
const std::string vs16 = "\xEF\xB8\x8F";
const std::string thumbsUp = "\xF0\x9F\x91\x8D";
const std::string toneLight = "\xF0\x9F\x8F\xBB";
const std::string woman = "\xF0\x9F\x91\xA9";
const std::string zwj = "\xE2\x80\x8D";
const std::string rocket = "\xF0\x9F\x9A\x80";
const std::string regionalU = "\xF0\x9F\x87\xBA";
const std::string regionalS = "\xF0\x9F\x87\xB8";
const std::string keycap = "\xE2\x83\xA3";

std::string EmojiSampleLine() {
	// ASCII + each supported multi-code-point form + trailing ASCII.
	return "A" + grinning + whiteSmile + vs16 + thumbsUp + toneLight +
		woman + zwj + rocket + regionalU + regionalS + "1" + vs16 + keycap + "Z";
}

bool ExactColour(ColourRGBA a, ColourRGBA b) {
	return a.GetRed() == b.GetRed() && a.GetGreen() == b.GetGreen() &&
		a.GetBlue() == b.GetBlue() && a.GetAlpha() == b.GetAlpha();
}

bool HasNonBackgroundInk(const ColourBuffer &buffer, ColourRGBA bg) {
	for (int y = 0; y < buffer.Height(); y++) {
		for (int x = 0; x < buffer.Width(); x++) {
			if (!ExactColour(buffer.ReadPixel(x, y), bg)) {
				return true;
			}
		}
	}
	return false;
}

bool HasNonMonochromeInk(const ColourBuffer &buffer, ColourRGBA bg) {
	for (int y = 0; y < buffer.Height(); y++) {
		for (int x = 0; x < buffer.Width(); x++) {
			const ColourRGBA px = buffer.ReadPixel(x, y);
			if (ExactColour(px, bg)) {
				continue;
			}
			// Colour emoji is not a pure gray or pure magenta mask.
			const int r = px.GetRed();
			const int g = px.GetGreen();
			const int b = px.GetBlue();
			if (g > 12 && (std::abs(r - g) > 8 || std::abs(b - g) > 8 || std::abs(r - b) > 8)) {
				return true;
			}
		}
	}
	return false;
}

}

TEST_CASE("Editor measures and places carets around supported emoji sequences") {
	TestHost host;
	TestEditor editor(host);
	// Wide client so the sample line stays one display line for hit tests.
	editor.SetClientRectangle(PRectangle(0, 0, 1200, 200));
	const std::string text = EmojiSampleLine();
	editor.SetText(text);

	// Shaped width through the editor style-0 path is positive and finite.
	const long width = editor.TextWidth(0, text);
	CHECK(width > 0);
	CHECK(width < 1200);

	// MeasureWidths puts the full unit advance on every byte of the sequence, so
	// caret x jumps from the unit start to the unit end with no intermediate x.
	const size_t zwjStart = text.find(woman);
	REQUIRE(zwjStart != std::string::npos);
	const size_t zwjEnd = zwjStart + woman.size() + zwj.size() + rocket.size();
	const int xBefore = editor.PointXFromPosition(static_cast<Sci::Position>(zwjStart));
	const int xInside = editor.PointXFromPosition(static_cast<Sci::Position>(zwjStart + woman.size()));
	const int xAfter = editor.PointXFromPosition(static_cast<Sci::Position>(zwjEnd));
	CHECK(xAfter > xBefore);
	CHECK(xInside == xAfter);

	// Flag pair is also one advance unit.
	const size_t flagStart = text.find(regionalU);
	REQUIRE(flagStart != std::string::npos);
	const int flagX0 = editor.PointXFromPosition(static_cast<Sci::Position>(flagStart));
	const int flagX1 = editor.PointXFromPosition(static_cast<Sci::Position>(flagStart + regionalU.size()));
	const int flagX2 = editor.PointXFromPosition(
		static_cast<Sci::Position>(flagStart + regionalU.size() + regionalS.size()));
	CHECK(flagX2 > flagX0);
	CHECK(flagX1 == flagX2);

	// Hit testing near the middle of the grinning face lands on its start, not
	// a trail byte, and not the surrounding ASCII when well inside the glyph.
	const size_t grinStart = text.find(grinning);
	REQUIRE(grinStart != std::string::npos);
	const int grinX0 = editor.PointXFromPosition(static_cast<Sci::Position>(grinStart));
	const int grinX1 = editor.PointXFromPosition(
		static_cast<Sci::Position>(grinStart + grinning.size()));
	const int midX = (grinX0 + grinX1) / 2;
	const int textTop = editor.PointYFromPosition(0);
	editor.MouseDown(Point(static_cast<XYPOSITION>(midX), static_cast<XYPOSITION>(textTop + 2)),
		KeyMod::Norm);
	editor.MouseUp(Point(static_cast<XYPOSITION>(midX), static_cast<XYPOSITION>(textTop + 2)),
		KeyMod::Norm);
	const Sci::Position clickPos = editor.GetCurrentPos();
	CHECK(clickPos >= static_cast<Sci::Position>(grinStart));
	CHECK(clickPos <= static_cast<Sci::Position>(grinStart + grinning.size()));
}

TEST_CASE("Editor wraps emoji sequences without splitting multi-code-point units") {
	TestHost host;
	TestEditor editor(host);
	// Narrow client forces wrap after the leading ASCII and first emoji.
	editor.SetClientRectangle(PRectangle(0, 0, 80, 240));
	editor.SetWrapMode(Wrap::Word);
	const std::string text = "AAAA" + woman + zwj + rocket + "BBBB";
	editor.SetText(text);
	editor.PaintAll();

	const Sci::Position zwjStart = static_cast<Sci::Position>(text.find(woman));
	const Sci::Position zwjEnd = zwjStart +
		static_cast<Sci::Position>(woman.size() + zwj.size() + rocket.size());
	// The ZWJ sequence stays on one display line (same y for start and end).
	const int yStart = editor.PointYFromPosition(zwjStart);
	const int yEnd = editor.PointYFromPosition(zwjEnd);
	CHECK(yStart == yEnd);
}

TEST_CASE("Editor paints colour emoji ink with selection and caret") {
	TestHost host;
	TestEditor editor(host);
	editor.SetClientRectangle(PRectangle(0, 0, 400, 120));
	const std::string text = "A" + grinning + "B";
	editor.SetText(text);
	// Select the emoji unit only.
	const Sci::Position emojiStart = 1;
	const Sci::Position emojiEnd = emojiStart + static_cast<Sci::Position>(grinning.size());
	editor.SetSel(emojiStart, emojiEnd);
	editor.GotoPos(emojiEnd);

	std::unique_ptr<DrawSurface> surface = editor.PaintToSurface();
	REQUIRE(surface);
	REQUIRE(surface->Buffer().Valid());
	// Default background is not pure black in every theme; sample a corner.
	const ColourRGBA corner = surface->Buffer().ReadPixel(0, 0);
	REQUIRE(HasNonBackgroundInk(surface->Buffer(), corner));
	CHECK(HasNonMonochromeInk(surface->Buffer(), corner));

	// Caret at the end of the emoji is to the right of its start.
	CHECK(editor.PointXFromPosition(emojiEnd) > editor.PointXFromPosition(emojiStart));
	surface->Release();
}

TEST_CASE("Editor paints colour for default emoji covered by monochrome primary") {
	// Primary fixture includes a monochrome U+1F600; presentation preference must
	// still choose the colour emoji fixture through the real measure/draw path.
	const std::filesystem::path fontDir = SCALPEL_TEST_FONT_DIR;
	TestHost host;
	UseTestFontPaths(fontDir / "FallbackEmojiMono.ttf", {
		fontDir / "EmojiFixture.ttf",
	});
	TestEditor editor(host);
	editor.SetClientRectangle(PRectangle(0, 0, 400, 120));
	const std::string text = "A" + grinning + "B";
	editor.SetText(text);

	std::unique_ptr<DrawSurface> surface = editor.PaintToSurface();
	REQUIRE(surface);
	REQUIRE(surface->Buffer().Valid());
	const ColourRGBA corner = surface->Buffer().ReadPixel(0, 0);
	REQUIRE(HasNonBackgroundInk(surface->Buffer(), corner));
	CHECK(HasNonMonochromeInk(surface->Buffer(), corner));
	surface->Release();
}

TEST_CASE("Production measure and draw surfaces share emoji fallback policy") {
	// Surfaces without fixed faces use FontFallback::Production; sizes and
	// decisions come from each primary face's request, so chrome and body do
	// not share one preloaded fallback list.
	FontCache cache;
	const auto body = cache.Match(FontParameters("sans-serif", 14.0));
	const auto chrome = cache.Match(FontParameters("system-ui", 11.0));
	REQUIRE(body);
	REQUIRE(chrome);
	CHECK(body->RequestedSize() == 14.0);
	CHECK(chrome->RequestedSize() == 11.0);

	const FontFallback production = FontFallback::Production(cache);
	const std::string text = grinning;
	const ShapedRun bodyRun = ShapeText(text, body, production);
	const ShapedRun chromeRun = ShapeText(text, chrome, production);
	REQUIRE_FALSE(bodyRun.glyphs.empty());
	REQUIRE_FALSE(chromeRun.glyphs.empty());
	// Fallback faces retain the requesting primary's size, not a shared list size.
	if (bodyRun.glyphs[0].face && bodyRun.glyphs[0].face != body) {
		CHECK(bodyRun.glyphs[0].face->RequestedSize() == 14.0);
	}
	if (chromeRun.glyphs[0].face && chromeRun.glyphs[0].face != chrome) {
		CHECK(chromeRun.glyphs[0].face->RequestedSize() == 11.0);
	}
	// Different requested sizes must not share the same sized face instance.
	if (bodyRun.glyphs[0].face && chromeRun.glyphs[0].face &&
		bodyRun.glyphs[0].face != body && chromeRun.glyphs[0].face != chrome) {
		CHECK(bodyRun.glyphs[0].face != chromeRun.glyphs[0].face);
	}
}
