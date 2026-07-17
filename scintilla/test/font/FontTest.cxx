// scalpel-editor font selection and ownership tests.

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "EditorStyleTypes.h"
#include "FontPlatform.h"
#include "Platform.h"

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

using namespace Scintilla;
using namespace Scintilla::Internal;

namespace {

const std::filesystem::path fontDirectory = SCALPEL_TEST_FONT_DIR;
const std::filesystem::path primaryPath = fontDirectory / "FallbackPrimary.ttf";
const std::filesystem::path snowmanPath = fontDirectory / "FallbackSnowman.ttf";

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
