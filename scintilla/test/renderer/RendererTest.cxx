// scalpel-editor offscreen renderer tests (headless EGL, no compositor).
//
// Phase 6 step 5 pixel matrix (no Wayland):
// - Primitive bounds: FillRectangle half-open, final-pixel LineDraw, concave
//   Polygon, Ellipse, Stadium, AlphaRectangle / RoundedRectangle
// - Nested clips: SetClip stack limits fills and survives pixmap target switches
// - Alpha blending: translucent fill over opaque and transparent backgrounds
// - Image placement: DrawRGBAImage top-left, neighbours, and straight-alpha input
// - Gradients, pixmap Copy, pattern fill, headless context, clear/readback
//
// Phase 6 step 6 text and composition matrix:
// - English DrawText*, empty string, NoClip back fill, Transparent, Clipped
// - Glyph cache + DrawGlyph placement with FreeType bearings
// - Shaped width (kerning AV), fallback snowman multi-face ink bands
// - Numeric ink bounds vs layout width and caret stops
// - Selection under text, caret line, indicator underline, margin + line number
// - Scrolled (negative origin) text clipped to a viewport
// - Multi-glyph xOffset/yOffset placement (synthetic pen offsets)
// Fixture fonts are ASCII + U+2603 only; Latin combining marks are out of scope.

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "DrawSurface.h"
#include "FontPlatform.h"
#include "Geometry.h"
#include "GlContext.h"
#include "Platform.h"
#include "Renderer.h"
#include "ShapedRun.h"

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

using namespace Scintilla::Internal;

namespace {

bool ExactColour(ColourRGBA a, ColourRGBA b) {
	return a.GetRed() == b.GetRed() && a.GetGreen() == b.GetGreen() &&
		a.GetBlue() == b.GetBlue() && a.GetAlpha() == b.GetAlpha();
}

ColourRGBA PixelAt(const std::vector<uint8_t> &topDown, int width, int x, int y) {
	const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
	return ColourRGBA(topDown[i], topDown[i + 1], topDown[i + 2], topDown[i + 3]);
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

/** Half-open ink bounds of non-bg pixels; empty (right<=left) if no ink. */
struct InkBounds {
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;

	bool Empty() const noexcept { return right <= left || bottom <= top; }
};

InkBounds FindInkBounds(const ColourBuffer &buffer, ColourRGBA bg) {
	InkBounds b{buffer.Width(), buffer.Height(), 0, 0};
	bool any = false;
	for (int y = 0; y < buffer.Height(); y++) {
		for (int x = 0; x < buffer.Width(); x++) {
			if (!ExactColour(buffer.ReadPixel(x, y), bg)) {
				any = true;
				b.left = std::min(b.left, x);
				b.top = std::min(b.top, y);
				b.right = std::max(b.right, x + 1);
				b.bottom = std::max(b.bottom, y + 1);
			}
		}
	}
	if (!any) {
		return {};
	}
	return b;
}

}

TEST_CASE("headless GlContext creates OpenGL 3.3 without a window system display") {
	GlContext context;
	REQUIRE(context.IsCurrent());
	REQUIRE(context.MajorVersion() == 3);
	REQUIRE(context.MinorVersion() == 3);

	const std::string version = context.VersionString();
	REQUIRE_FALSE(version.empty());
	// Accept "3.3.0 ..." and higher majors.
	int major = 0;
	int minor = 0;
	REQUIRE(std::sscanf(version.c_str(), "%d.%d", &major, &minor) >= 2);
	REQUIRE(major >= 3);
	if (major == 3) {
		REQUIRE(minor >= 3);
	}
	REQUIRE_FALSE(context.RendererString().empty());
}

TEST_CASE("GlContext MakeCurrent restores after ReleaseCurrent") {
	GlContext context;
	REQUIRE(context.IsCurrent());
	context.ReleaseCurrent();
	REQUIRE_FALSE(context.IsCurrent());
	context.MakeCurrent();
	REQUIRE(context.IsCurrent());
	REQUIRE_FALSE(context.VersionString().empty());
}

TEST_CASE("clear fills ColourBuffer; readback is top-to-bottom RGBA") {
	GlContext context;
	Renderer renderer(context);
	ColourBuffer buffer;
	buffer.Resize(4, 3);
	renderer.SetDrawTarget(buffer.FramebufferName(), buffer.Width(), buffer.Height());

	const ColourRGBA red(255, 0, 0, 255);
	renderer.Clear(red);

	// Single-pixel sample uses top-down coordinates: (0,0) is top-left.
	REQUIRE(ExactColour(buffer.ReadPixel(0, 0), red));
	REQUIRE(ExactColour(buffer.ReadPixel(3, 2), red));
	REQUIRE(ExactColour(buffer.ReadPixel(1, 1), red));

	const std::vector<uint8_t> pixels = buffer.ReadPixelsTopDown();
	REQUIRE(pixels.size() == 4u * 3u * 4u);
	for (int y = 0; y < 3; y++) {
		for (int x = 0; x < 4; x++) {
			REQUIRE(ExactColour(PixelAt(pixels, 4, x, y), red));
		}
	}

	const ColourRGBA blue(0, 0, 255, 255);
	renderer.Clear(blue);
	REQUIRE(ExactColour(buffer.ReadPixel(2, 1), blue));
}

TEST_CASE("DrawSurface measures through shaped runs; measure-only text is a no-op") {
	FontCache fonts;
	const std::filesystem::path primary = std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	std::shared_ptr<FontFace> face = fonts.LoadPath(primary, FontParameters("fixture", 16.0));
	std::shared_ptr<Font> font = FontFromFace(face);

	std::unique_ptr<DrawSurface> surface = CreateMeasureOnlySurface();
	REQUIRE(surface->Initialised());
	REQUIRE(surface->WidthText(font.get(), "Hi") > 0.0f);

	XYPOSITION positions[2] = {};
	surface->MeasureWidths(font.get(), "Hi", positions);
	REQUIRE(positions[0] > 0.0f);
	REQUIRE(positions[1] >= positions[0]);
	REQUIRE(surface->Ascent(font.get()) > 0.0f);
	REQUIRE(surface->Height(font.get()) > 0.0f);

	// Measure-only surfaces never paint and must not require a Renderer.
	surface->DrawTextTransparent(PRectangle::FromInts(0, 0, 10, 10), font.get(), 8.0f, "Hi",
		ColourRGBA(0, 0, 0));
}

TEST_CASE("DrawTextTransparent paints English text; empty string leaves buffer") {
	FontCache fonts;
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	std::shared_ptr<FontFace> face = fonts.LoadPath(primary, FontParameters("fixture", 16.0));
	std::shared_ptr<Font> font = FontFromFace(face);

	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 64, 40);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fg(255, 255, 255, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);

	const XYPOSITION ybase = surface->Ascent(font.get());
	surface->DrawTextTransparent(PRectangle::FromInts(2, 0, 64, 40), font.get(), ybase, "",
		fg);
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(10, 10), bg));

	surface->DrawTextTransparent(PRectangle::FromInts(2, 0, 64, 40), font.get(), ybase, "Hi",
		fg);
	REQUIRE(HasNonBackgroundInk(surface->Buffer(), bg));
}

TEST_CASE("DrawTextNoClip fills background; Transparent does not") {
	FontCache fonts;
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	std::shared_ptr<FontFace> face = fonts.LoadPath(primary, FontParameters("fixture", 16.0));
	std::shared_ptr<Font> font = FontFromFace(face);

	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 40, 30);
	const ColourRGBA green(0, 255, 0, 255);
	const ColourRGBA back(0, 0, 128, 255);
	const ColourRGBA fg(255, 255, 255, 255);
	surface->BindDrawTarget();
	renderer.Clear(green);

	const PRectangle rc = PRectangle::FromInts(4, 4, 36, 26);
	const XYPOSITION ybase = 4.0 + surface->Ascent(font.get());
	surface->DrawTextNoClip(rc, font.get(), ybase, "X", fg, back);
	// Background fill covers the rect; a pixel on the edge interior should be back or ink.
	const ColourRGBA corner = surface->Buffer().ReadPixel(4, 4);
	REQUIRE((ExactColour(corner, back) || !ExactColour(corner, green)));

	renderer.Clear(green);
	surface->DrawTextTransparent(rc, font.get(), ybase, "X", fg);
	// Transparent leaves corners of the rect as the previous clear when ink does not reach them.
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), green));
}

TEST_CASE("DrawTextClipped keeps ink inside the clip rectangle") {
	FontCache fonts;
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	std::shared_ptr<FontFace> face = fonts.LoadPath(primary, FontParameters("fixture", 16.0));
	std::shared_ptr<Font> font = FontFromFace(face);

	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 48, 32);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA back(32, 32, 32, 255);
	const ColourRGBA fg(255, 255, 255, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);

	const PRectangle rc = PRectangle::FromInts(8, 4, 24, 28);
	const XYPOSITION ybase = 4.0 + surface->Ascent(font.get());
	surface->DrawTextClipped(rc, font.get(), ybase, "WWWW", fg, back);

	// Outside the clip rect, background clear remains (not the text back fill either).
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), bg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(30, 10), bg));
	// Inside the clip: either back fill or glyph ink.
	const ColourRGBA inside = surface->Buffer().ReadPixel(10, 10);
	REQUIRE_FALSE(ExactColour(inside, bg));
}

TEST_CASE("DrawText uses shaped width for AV kerning pair") {
	FontCache fonts;
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	std::shared_ptr<FontFace> face = fonts.LoadPath(primary, FontParameters("fixture", 16.0));
	std::shared_ptr<Font> font = FontFromFace(face);

	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 64, 40, {});
	const XYPOSITION width = surface->WidthText(font.get(), "AV");
	const ShapedRun run = ShapeText("AV", face);
	REQUIRE(width == run.Width());
	REQUIRE(width > 0.0);
}

TEST_CASE("DrawText fallback span paints primary and snowman faces") {
	FontCache fonts;
	const std::filesystem::path primaryPath =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	const std::filesystem::path snowmanPath =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackSnowman.ttf";
	std::shared_ptr<FontFace> primary = fonts.LoadPath(primaryPath, FontParameters("fixture", 16.0));
	std::shared_ptr<FontFace> snowman = fonts.LoadPath(snowmanPath, FontParameters("fixture", 16.0));
	std::shared_ptr<Font> font = FontFromFace(primary);

	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface =
		CreateDrawSurface(renderer, 96, 40, {snowman});
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fg(255, 255, 255, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);

	const std::string text = std::string("A") + "\xE2\x98\x83" + "B";
	const XYPOSITION ybase = surface->Ascent(font.get());
	surface->DrawTextTransparent(PRectangle::FromInts(2, 0, 96, 40), font.get(), ybase, text, fg);
	REQUIRE(HasNonBackgroundInk(surface->Buffer(), bg));
	// Width matches the multi-face shaped run.
	const ShapedRun run = ShapeText(text, primary, {snowman});
	CHECK(surface->WidthText(font.get(), text) == run.Width());
}

TEST_CASE("CreateDrawSurface clears through Renderer into its colour buffer") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 8, 6);
	const ColourRGBA green(0, 255, 0, 255);
	surface->BindDrawTarget();
	renderer.Clear(green);
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), green));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(7, 5), green));
}

TEST_CASE("nested clips limit solid fills; outer pixels stay previous colour") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 10, 10);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA outer(255, 0, 0, 255);
	const ColourRGBA inner(0, 255, 0, 255);

	surface->BindDrawTarget();
	renderer.Clear(bg);

	surface->SetClip(PRectangle::FromInts(2, 2, 8, 8));
	surface->FillRectangle(PRectangle::FromInts(0, 0, 10, 10), Fill(outer));
	// Outside the outer clip remains background.
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), bg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(9, 9), bg));
	// Inside outer clip is red.
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(2, 2), outer));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(7, 7), outer));

	surface->SetClip(PRectangle::FromInts(4, 4, 6, 6));
	surface->FillRectangle(PRectangle::FromInts(0, 0, 10, 10), Fill(inner));
	// Nested region is green.
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(4, 4), inner));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(5, 5), inner));
	// Still inside outer clip but outside nested clip stays red.
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(2, 2), outer));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(7, 7), outer));
	// Outside outer clip still background.
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(1, 1), bg));

	surface->PopClip();
	surface->PopClip();
	// After popping, a full fill covers everything.
	surface->FillRectangle(PRectangle::FromInts(0, 0, 10, 10), Fill(ColourRGBA(0, 0, 255, 255)));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), ColourRGBA(0, 0, 255, 255)));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(9, 9), ColourRGBA(0, 0, 255, 255)));
}

TEST_CASE("opaque FillRectangle respects half-open bounds") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 5, 5);
	const ColourRGBA bg(10, 10, 10, 255);
	const ColourRGBA fg(200, 100, 50, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);
	// Fill [1,3) x [1,3) — pixels (1,1),(2,1),(1,2),(2,2) only.
	surface->FillRectangle(PRectangle::FromInts(1, 1, 3, 3), Fill(fg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(1, 1), fg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(2, 2), fg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), bg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(3, 1), bg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(1, 3), bg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(3, 3), bg));
}

bool NearColour(ColourRGBA a, ColourRGBA b, int tol = 1) {
	auto diff = [](unsigned char x, unsigned char y) {
		return std::abs(static_cast<int>(x) - static_cast<int>(y));
	};
	return diff(a.GetRed(), b.GetRed()) <= tol &&
		diff(a.GetGreen(), b.GetGreen()) <= tol &&
		diff(a.GetBlue(), b.GetBlue()) <= tol &&
		diff(a.GetAlpha(), b.GetAlpha()) <= tol;
}

TEST_CASE("alpha fill blends over background within tolerance") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 4, 4);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fg(255, 0, 0, 128);
	surface->BindDrawTarget();
	renderer.Clear(bg);
	surface->FillRectangle(PRectangle::FromInts(1, 1, 3, 3), Fill(fg));
	// Expected: src-over 0.5 * red over black ≈ (128,0,0) with ±1.
	const ColourRGBA got = surface->Buffer().ReadPixel(1, 1);
	REQUIRE(NearColour(got, ColourRGBA(128, 0, 0, 255), 1));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), bg));
}

TEST_CASE("horizontal LineDraw paints the segment interior") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 12, 8);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fg(0, 255, 0, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);
	surface->LineDraw(Point(2, 4), Point(9, 4), Stroke(fg, 2.0f));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(5, 4), fg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), bg));
}

TEST_CASE("LineDraw includes both aligned endpoint pixels") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 12, 8);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fg(0, 255, 0, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);
	surface->LineDraw(Point(2.5f, 4.5f), Point(9.5f, 4.5f), Stroke(fg, 1.0f));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(2, 4), fg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(9, 4), fg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(1, 4), bg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(10, 4), bg));
}

TEST_CASE("Polygon triangle fill covers centroid and not outside bounds") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 20, 20);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fg(0, 0, 255, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);
	const Point pts[3] = {Point(2, 2), Point(17, 2), Point(10, 17)};
	surface->Polygon(pts, 3, FillStroke(fg, ColourRGBA(0, 0, 0, 0), 0.0f));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(10, 6), fg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), bg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(19, 19), bg));
}

TEST_CASE("Polygon fills a concave plus without filling its corners") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 24, 24);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fg(255, 0, 0, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);
	const Point plus[] = {
		Point(4, 10), Point(10, 10), Point(10, 4), Point(14, 4),
		Point(14, 10), Point(20, 10), Point(20, 14), Point(14, 14),
		Point(14, 20), Point(10, 20), Point(10, 14), Point(4, 14),
	};
	surface->Polygon(plus, std::size(plus), FillStroke(fg, ColourRGBA(0, 0, 0, 0), 0.0f));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(12, 6), fg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(6, 12), fg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(9, 4), bg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(7, 7), bg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(16, 16), bg));
}

TEST_CASE("Ellipse fill covers centre and leaves far corner of bounds empty") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 20, 20);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fg(255, 255, 0, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);
	// Ellipse in [2,18) x [2,18)
	surface->Ellipse(PRectangle::FromInts(2, 2, 18, 18), FillStroke(fg, fg, 0.0f));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(10, 10), fg));
	// Corner of the bounding box is outside the ellipse.
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(2, 2), bg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), bg));
}

TEST_CASE("Stadium semicircle ends cover centre line") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 40, 12);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fg(255, 0, 255, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);
	surface->Stadium(PRectangle::FromInts(4, 2, 36, 10), FillStroke(fg, fg, 0.0f),
		Surface::Ends::semiCircles);
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(20, 6), fg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), bg));
}

TEST_CASE("Stadium angle end tapers to its midpoint") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 24, 12);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fg(255, 0, 255, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);
	const Surface::Ends angleFlat = static_cast<Surface::Ends>(
		static_cast<int>(Surface::Ends::leftAngle) |
		static_cast<int>(Surface::Ends::rightFlat));
	surface->Stadium(PRectangle::FromInts(2, 2, 22, 10), FillStroke(fg, fg, 0.0f), angleFlat);
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(2, 2), bg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(2, 6), fg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(21, 2), fg));
}

TEST_CASE("RoundedRectangle / AlphaRectangle fill interior") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 24, 24);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fg(0, 200, 200, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);
	surface->AlphaRectangle(PRectangle::FromInts(4, 4, 20, 20), 4.0f, FillStroke(fg, fg, 0.0f));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(12, 12), fg));
	// Outside the rounded rect remains background.
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), bg));
}

TEST_CASE("left-to-right gradient samples ends, midpoint, and off-centre stop") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 100, 4);
	surface->BindDrawTarget();
	renderer.Clear(ColourRGBA(0, 0, 0, 255));
	// Black at 0, white at 0.25, red at 1. Midpoint (t=0.5) lies between white and red.
	std::vector<ColourStop> stops = {
		ColourStop(0.0f, ColourRGBA(0, 0, 0, 255)),
		ColourStop(0.25f, ColourRGBA(255, 255, 255, 255)),
		ColourStop(1.0f, ColourRGBA(255, 0, 0, 255)),
	};
	surface->GradientRectangle(PRectangle::FromInts(0, 0, 100, 4), stops,
		Surface::GradientOptions::leftToRight);

	// Pixel centres map to continuous t; check stop, midpoint, and that the
	// left side is darker than the midpoint and the right side is redder.
	const ColourRGBA left = surface->Buffer().ReadPixel(5, 1);
	const ColourRGBA atStop = surface->Buffer().ReadPixel(25, 1);
	const ColourRGBA mid = surface->Buffer().ReadPixel(50, 1);
	const ColourRGBA right = surface->Buffer().ReadPixel(95, 1);

	REQUIRE(left.GetRed() < 80);
	REQUIRE(left.GetGreen() < 80);
	REQUIRE(NearColour(atStop, ColourRGBA(255, 255, 255, 255), 8));
	// t≈0.5 is about 1/3 of the way from 0.25 to 1.0 → mix white→red by ~0.333
	REQUIRE(NearColour(mid, ColourRGBA(255, 170, 170, 255), 16));
	REQUIRE(right.GetRed() > 200);
	REQUIRE(right.GetGreen() < 40);
	REQUIRE(mid.GetGreen() > left.GetGreen());
}

TEST_CASE("top-to-bottom gradient samples vertical ends") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 4, 40);
	surface->BindDrawTarget();
	renderer.Clear(ColourRGBA(0, 0, 0, 255));
	std::vector<ColourStop> stops = {
		ColourStop(0.0f, ColourRGBA(0, 0, 255, 255)),
		ColourStop(1.0f, ColourRGBA(0, 255, 0, 255)),
	};
	surface->GradientRectangle(PRectangle::FromInts(0, 0, 4, 40), stops,
		Surface::GradientOptions::topToBottom);
	const ColourRGBA top = surface->Buffer().ReadPixel(1, 2);
	const ColourRGBA bottom = surface->Buffer().ReadPixel(1, 37);
	const ColourRGBA mid = surface->Buffer().ReadPixel(1, 20);
	REQUIRE(top.GetBlue() > 200);
	REQUIRE(top.GetGreen() < 40);
	REQUIRE(bottom.GetGreen() > 200);
	REQUIRE(bottom.GetBlue() < 40);
	REQUIRE(NearColour(mid, ColourRGBA(0, 128, 128, 255), 20));
}

TEST_CASE("DrawRGBAImage places top-left pixel correctly") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 8, 8);
	surface->BindDrawTarget();
	renderer.Clear(ColourRGBA(0, 0, 0, 255));
	// 2x2 image: red, green / blue, white
	const unsigned char img[] = {
		255, 0, 0, 255,   0, 255, 0, 255,
		0, 0, 255, 255,   255, 255, 255, 255,
	};
	surface->DrawRGBAImage(PRectangle::FromInts(2, 2, 4, 4), 2, 2, img);
	REQUIRE(NearColour(surface->Buffer().ReadPixel(2, 2), ColourRGBA(255, 0, 0, 255), 1));
	REQUIRE(NearColour(surface->Buffer().ReadPixel(3, 2), ColourRGBA(0, 255, 0, 255), 1));
	REQUIRE(NearColour(surface->Buffer().ReadPixel(2, 3), ColourRGBA(0, 0, 255, 255), 1));
	REQUIRE(NearColour(surface->Buffer().ReadPixel(3, 3), ColourRGBA(255, 255, 255, 255), 1));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), ColourRGBA(0, 0, 0, 255)));
}

TEST_CASE("transparent targets read back straight alpha after blending") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 4, 4);
	surface->BindDrawTarget();
	renderer.Clear(ColourRGBA(0, 0, 0, 0));
	surface->FillRectangle(PRectangle::FromInts(0, 0, 2, 2), Fill(ColourRGBA(255, 0, 0, 128)));
	REQUIRE(NearColour(surface->Buffer().ReadPixel(0, 0), ColourRGBA(255, 0, 0, 128), 1));
	const std::vector<uint8_t> pixels = surface->Buffer().ReadPixelsTopDown();
	REQUIRE(NearColour(PixelAt(pixels, 4, 0, 0), ColourRGBA(255, 0, 0, 128), 1));

	const unsigned char image[] = {0, 255, 0, 128};
	surface->DrawRGBAImage(PRectangle::FromInts(2, 0, 3, 1), 1, 1, image);
	REQUIRE(NearColour(surface->Buffer().ReadPixel(2, 0), ColourRGBA(0, 255, 0, 128), 1));
}

TEST_CASE("AllocatePixMap Copy and pattern fill") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 16, 16);
	surface->BindDrawTarget();
	renderer.Clear(ColourRGBA(0, 0, 0, 255));

	std::unique_ptr<Surface> pix = surface->AllocatePixMap(4, 4);
	auto *pixSurface = dynamic_cast<DrawSurface *>(pix.get());
	REQUIRE(pixSurface != nullptr);
	pixSurface->BindDrawTarget();
	renderer.Clear(ColourRGBA(0, 0, 255, 255));
	pixSurface->FillRectangle(PRectangle::FromInts(0, 0, 2, 2), Fill(ColourRGBA(255, 0, 0, 255)));

	// Copy pixmap into main surface at (4,4).
	surface->BindDrawTarget();
	surface->Copy(PRectangle::FromInts(4, 4, 8, 8), Point(0, 0), *pix);
	REQUIRE(NearColour(surface->Buffer().ReadPixel(4, 4), ColourRGBA(255, 0, 0, 255), 1));
	REQUIRE(NearColour(surface->Buffer().ReadPixel(6, 6), ColourRGBA(0, 0, 255, 255), 1));

	// Pattern fill tiles the pixmap over a region.
	surface->FillRectangle(PRectangle::FromInts(0, 0, 8, 4), *pix);
	REQUIRE(NearColour(surface->Buffer().ReadPixel(0, 0), ColourRGBA(255, 0, 0, 255), 1));
	REQUIRE(NearColour(surface->Buffer().ReadPixel(2, 0), ColourRGBA(0, 0, 255, 255), 1));
	REQUIRE(NearColour(surface->Buffer().ReadPixel(4, 0), ColourRGBA(255, 0, 0, 255), 1));
}

TEST_CASE("surface clip survives drawing to a sibling pixmap") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 10, 10);
	std::unique_ptr<Surface> pixmap = surface->AllocatePixMap(4, 4);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fg(255, 0, 0, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);
	surface->SetClip(PRectangle::FromInts(2, 2, 8, 8));
	pixmap->FillRectangle(PRectangle::FromInts(0, 0, 4, 4), Fill(ColourRGBA(0, 255, 0, 255)));
	surface->FillRectangle(PRectangle::FromInts(0, 0, 10, 10), Fill(fg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(1, 1), bg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(2, 2), fg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(7, 7), fg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(8, 8), bg));
	surface->PopClip();
}

TEST_CASE("DrawGlyph paints shaped coverage and reuses the glyph cache") {
	FontCache fonts;
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	std::shared_ptr<FontFace> face = fonts.LoadPath(primary, FontParameters("fixture", 16.0));
	const ShapedRun run = ShapeText("A", face);
	REQUIRE_FALSE(run.glyphs.empty());

	GlContext context;
	Renderer renderer(context);
	ColourBuffer buffer;
	buffer.Resize(48, 48);
	renderer.SetDrawTarget(buffer.FramebufferName(), buffer.Width(), buffer.Height());
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fg(255, 255, 255, 255);
	renderer.Clear(bg);

	const FontMetrics metrics = face->Metrics();
	const XYPOSITION baseline = metrics.ascent;
	REQUIRE(renderer.GlyphCacheSize() == 0);
	renderer.DrawGlyph(4.0, baseline, face, run.glyphs[0].glyphId, fg);
	REQUIRE(renderer.GlyphCacheSize() == 1);
	REQUIRE(HasNonBackgroundInk(buffer, bg));

	// Far corner stays clear when the glyph sits near the origin.
	REQUIRE(ExactColour(buffer.ReadPixel(47, 47), bg));

	// Second draw of the same id reuses the cached texture entry.
	renderer.DrawGlyph(20.0, baseline, face, run.glyphs[0].glyphId, fg);
	REQUIRE(renderer.GlyphCacheSize() == 1);
}

TEST_CASE("Glyph cache retains each face used by its textures") {
	GlContext context;
	Renderer renderer(context);
	ColourBuffer buffer;
	buffer.Resize(32, 32);
	renderer.SetDrawTarget(buffer.FramebufferName(), buffer.Width(), buffer.Height());

	std::weak_ptr<FontFace> cachedFace;
	{
		FontCache fonts;
		const std::filesystem::path primary =
			std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
		std::shared_ptr<FontFace> face = fonts.LoadPath(primary, FontParameters("fixture", 16.0));
		const ShapedRun run = ShapeText("A", face);
		REQUIRE_FALSE(run.glyphs.empty());
		cachedFace = face;
		renderer.DrawGlyph(4.0, face->Metrics().ascent, face, run.glyphs[0].glyphId,
			ColourRGBA(255, 255, 255, 255));
	}

	REQUIRE(renderer.GlyphCacheSize() == 1);
	CHECK_FALSE(cachedFace.expired());
}

TEST_CASE("DrawGlyph respects clip and translucent fore colour") {
	FontCache fonts;
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	std::shared_ptr<FontFace> face = fonts.LoadPath(primary, FontParameters("fixture", 16.0));
	const ShapedRun run = ShapeText("A", face);
	REQUIRE_FALSE(run.glyphs.empty());

	GlContext context;
	Renderer renderer(context);
	ColourBuffer buffer;
	buffer.Resize(40, 40);
	renderer.SetDrawTarget(buffer.FramebufferName(), buffer.Width(), buffer.Height());
	const ColourRGBA bg(0, 0, 0, 255);
	renderer.Clear(bg);

	const FontMetrics metrics = face->Metrics();
	const XYPOSITION baseline = metrics.ascent;

	// Clip to a 1x1 pixel that is outside a typical glyph at pen (30, baseline).
	renderer.SetClip(PRectangle::FromInts(0, 0, 1, 1));
	renderer.DrawGlyph(30.0, baseline, face, run.glyphs[0].glyphId, ColourRGBA(255, 0, 0, 255));
	renderer.PopClip();
	REQUIRE_FALSE(HasNonBackgroundInk(buffer, bg));

	// Full-target translucent red: some pixel must move off pure black.
	renderer.DrawGlyph(4.0, baseline, face, run.glyphs[0].glyphId, ColourRGBA(255, 0, 0, 128));
	bool sawBlend = false;
	for (int y = 0; y < buffer.Height() && !sawBlend; y++) {
		for (int x = 0; x < buffer.Width(); x++) {
			const ColourRGBA p = buffer.ReadPixel(x, y);
			if (p.GetRed() > 0) {
				sawBlend = true;
				// Coverage * 128/255 should be strictly less than full red on partial pixels.
				break;
			}
		}
	}
	REQUIRE(sawBlend);
}

TEST_CASE("rendered glyph ink bounds sit within shaped layout width") {
	FontCache fonts;
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	std::shared_ptr<FontFace> face = fonts.LoadPath(primary, FontParameters("fixture", 16.0));
	std::shared_ptr<Font> font = FontFromFace(face);

	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 80, 40);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fg(255, 255, 255, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);

	const char *text = "Hi";
	const XYPOSITION originX = 4.0;
	const XYPOSITION ybase = surface->Ascent(font.get());
	const XYPOSITION width = surface->WidthText(font.get(), text);
	const ShapedRun run = ShapeText(text, face);
	REQUIRE(width == run.Width());

	surface->DrawTextTransparent(PRectangle(originX, 0.0, originX + width + 8.0, 40.0),
		font.get(), ybase, text, fg);

	const InkBounds ink = FindInkBounds(surface->Buffer(), bg);
	REQUIRE_FALSE(ink.Empty());
	// Ink may start slightly left of origin via negative bearings; allow one pixel.
	CHECK(ink.left + 1 >= static_cast<int>(std::floor(originX)) - 2);
	// Right edge of ink stays within ceil(origin + width) plus anti-alias slack.
	CHECK(ink.right <= static_cast<int>(std::ceil(originX + width)) + 2);

	// Caret stops map to x positions inside the shaped advance span.
	for (size_t stop : run.caretStops) {
		XYPOSITION x = originX;
		if (stop > 0) {
			x = originX + run.byteEndPositions[stop - 1];
		}
		CHECK(x + 1e-6 >= originX);
		CHECK(x <= originX + width + 1e-6);
	}
}

TEST_CASE("selection fill under text matches layout byte positions") {
	FontCache fonts;
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	std::shared_ptr<FontFace> face = fonts.LoadPath(primary, FontParameters("fixture", 16.0));
	std::shared_ptr<Font> font = FontFromFace(face);

	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 80, 40);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA sel(0, 0, 180, 255);
	const ColourRGBA fg(255, 255, 255, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);

	const char *text = "Hi";
	const XYPOSITION originX = 8.0;
	const XYPOSITION ybase = surface->Ascent(font.get());
	const ShapedRun run = ShapeText(text, face);
	REQUIRE(run.byteEndPositions.size() >= 2);
	// Select the first character only: [0, end of first cluster).
	const XYPOSITION selLeft = originX;
	const XYPOSITION selRight = originX + run.byteEndPositions[0];
	const PRectangle selRc(selLeft, 2.0, selRight, 30.0);
	surface->FillRectangle(selRc, Fill(sel));
	surface->DrawTextTransparent(PRectangle(originX, 0.0, 80.0, 40.0), font.get(), ybase, text, fg);

	// Interior of selection (away from text ink) should still be selection blue.
	const int midX = static_cast<int>(std::floor((selLeft + selRight) * 0.5));
	// Sample near the bottom of the selection band where glyph ink is unlikely.
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(midX, 28), sel));
	// Just outside the half-open selection right edge stays background.
	const int outside = static_cast<int>(std::ceil(selRight));
	if (outside < surface->Buffer().Width()) {
		// May be bg or second-glyph ink; must not be solid selection blue unless ink.
		const ColourRGBA p = surface->Buffer().ReadPixel(outside, 28);
		if (ExactColour(p, sel)) {
			// Selection half-open: right column exclusive.
			FAIL("selection painted at exclusive right edge");
		}
	}
	REQUIRE(HasNonBackgroundInk(surface->Buffer(), bg));
}

TEST_CASE("fallback run ink spans three shaped advance regions") {
	FontCache fonts;
	const std::filesystem::path primaryPath =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	const std::filesystem::path snowmanPath =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackSnowman.ttf";
	std::shared_ptr<FontFace> primary = fonts.LoadPath(primaryPath, FontParameters("fixture", 16.0));
	std::shared_ptr<FontFace> snowman = fonts.LoadPath(snowmanPath, FontParameters("fixture", 16.0));
	std::shared_ptr<Font> font = FontFromFace(primary);

	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 120, 48, {snowman});
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fg(255, 255, 255, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);

	const std::string text = std::string("A") + "\xE2\x98\x83" + "B";
	const ShapedRun run = ShapeText(text, primary, {snowman});
	REQUIRE(run.byteEndPositions.size() == 5);
	const XYPOSITION originX = 4.0;
	const XYPOSITION ybase = surface->Ascent(font.get());
	surface->DrawTextTransparent(PRectangle(originX, 0.0, 120.0, 48.0), font.get(), ybase, text, fg);

	// Three bands from cluster advances: A [0,e0), snowman [e0,e3], B (e3,end].
	const XYPOSITION aEnd = originX + run.byteEndPositions[0];
	const XYPOSITION snowEnd = originX + run.byteEndPositions[3];
	const XYPOSITION bEnd = originX + run.Width();

	auto bandHasInk = [&](XYPOSITION left, XYPOSITION right) {
		const int x0 = std::max(0, static_cast<int>(std::floor(left)));
		const int x1 = std::min(surface->Buffer().Width(), static_cast<int>(std::ceil(right)));
		for (int y = 0; y < surface->Buffer().Height(); y++) {
			for (int x = x0; x < x1; x++) {
				if (!ExactColour(surface->Buffer().ReadPixel(x, y), bg)) {
					return true;
				}
			}
		}
		return false;
	};
	REQUIRE(bandHasInk(originX, aEnd));
	REQUIRE(bandHasInk(aEnd, snowEnd));
	REQUIRE(bandHasInk(snowEnd, bEnd));
}

TEST_CASE("composed caret line sits at shaped caret x") {
	FontCache fonts;
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	std::shared_ptr<FontFace> face = fonts.LoadPath(primary, FontParameters("fixture", 16.0));
	std::shared_ptr<Font> font = FontFromFace(face);

	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 64, 32);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA caret(255, 0, 0, 255);
	const ColourRGBA fg(200, 200, 200, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);

	const char *text = "Hi";
	const XYPOSITION originX = 6.0;
	const XYPOSITION ybase = surface->Ascent(font.get());
	const ShapedRun run = ShapeText(text, face);
	surface->DrawTextTransparent(PRectangle(originX, 0.0, 64.0, 32.0), font.get(), ybase, text, fg);

	// Caret after first character.
	const XYPOSITION caretX = originX + run.byteEndPositions[0];
	const int cx = static_cast<int>(std::floor(caretX));
	surface->FillRectangle(PRectangle::FromInts(cx, 2, cx + 1, 28), Fill(caret));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(cx, 4), caret));
	// Neighbour column is not forced to caret red (may be ink or bg).
	if (cx + 1 < surface->Buffer().Width()) {
		REQUIRE_FALSE(ExactColour(surface->Buffer().ReadPixel(cx + 1, 4), caret));
	}
}

TEST_CASE("composed indicator underline sits under the baseline") {
	FontCache fonts;
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	std::shared_ptr<FontFace> face = fonts.LoadPath(primary, FontParameters("fixture", 16.0));
	std::shared_ptr<Font> font = FontFromFace(face);

	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 64, 40);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA under(0, 255, 0, 255);
	const ColourRGBA fg(255, 255, 255, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);

	const XYPOSITION originX = 4.0;
	const XYPOSITION ybase = surface->Ascent(font.get());
	const XYPOSITION width = surface->WidthText(font.get(), "Hi");
	surface->DrawTextTransparent(PRectangle(originX, 0.0, 64.0, 40.0), font.get(), ybase, "Hi", fg);

	const int underY = static_cast<int>(std::floor(ybase)) + 1;
	surface->FillRectangle(
		PRectangle(originX, static_cast<XYPOSITION>(underY), originX + width,
			static_cast<XYPOSITION>(underY + 1)),
		Fill(under));
	const int midX = static_cast<int>(std::floor(originX + width * 0.5));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(midX, underY), under));
}

TEST_CASE("composed margin band and line-number text") {
	FontCache fonts;
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	std::shared_ptr<FontFace> face = fonts.LoadPath(primary, FontParameters("fixture", 16.0));
	std::shared_ptr<Font> font = FontFromFace(face);

	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 80, 36);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA margin(40, 40, 40, 255);
	const ColourRGBA fg(255, 255, 0, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);

	const int marginRight = 24;
	surface->FillRectangle(PRectangle::FromInts(0, 0, marginRight, 36), Fill(margin));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), margin));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(marginRight, 0), bg));

	const XYPOSITION ybase = surface->Ascent(font.get());
	surface->DrawTextTransparent(PRectangle::FromInts(2, 0, marginRight, 36), font.get(),
		ybase, "1", fg);
	// Line-number ink appears inside the margin band.
	bool inkInMargin = false;
	for (int y = 0; y < 36 && !inkInMargin; y++) {
		for (int x = 0; x < marginRight; x++) {
			const ColourRGBA p = surface->Buffer().ReadPixel(x, y);
			if (!ExactColour(p, margin) && !ExactColour(p, bg)) {
				inkInMargin = true;
				break;
			}
		}
	}
	REQUIRE(inkInMargin);
	// Text area to the right of the margin stays clear.
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(marginRight + 2, 2), bg));
}

TEST_CASE("scrolled text clip shows only the viewport portion") {
	FontCache fonts;
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	std::shared_ptr<FontFace> face = fonts.LoadPath(primary, FontParameters("fixture", 16.0));
	std::shared_ptr<Font> font = FontFromFace(face);

	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 48, 32);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fg(255, 255, 255, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);

	const XYPOSITION ybase = surface->Ascent(font.get());
	// Viewport is the full buffer; draw as if scrolled so the first glyphs sit left of 0.
	const XYPOSITION originX = -12.0;
	surface->SetClip(PRectangle::FromInts(0, 0, 48, 32));
	surface->DrawTextTransparent(PRectangle(originX, 0.0, 80.0, 32.0), font.get(), ybase,
		"Hello", fg);
	surface->PopClip();

	REQUIRE(HasNonBackgroundInk(surface->Buffer(), bg));
	// Leftmost column may or may not have ink depending on how much scrolled;
	// right side of a long string should have ink when origin is negative.
	const InkBounds ink = FindInkBounds(surface->Buffer(), bg);
	REQUIRE_FALSE(ink.Empty());
	CHECK(ink.left >= 0);
	CHECK(ink.right <= 48);
}

TEST_CASE("DrawGlyph applies synthetic xOffset and yOffset placement") {
	// Fixture fonts do not provide Latin combining marks; prove the pen+offset
	// path places a second glyph relative to the same origin.
	FontCache fonts;
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	std::shared_ptr<FontFace> face = fonts.LoadPath(primary, FontParameters("fixture", 16.0));
	const ShapedRun run = ShapeText("o", face);
	REQUIRE_FALSE(run.glyphs.empty());
	const uint32_t glyphId = run.glyphs[0].glyphId;

	GlContext context;
	Renderer renderer(context);
	ColourBuffer buffer;
	buffer.Resize(48, 48);
	renderer.SetDrawTarget(buffer.FramebufferName(), buffer.Width(), buffer.Height());
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fg(255, 255, 255, 255);
	renderer.Clear(bg);

	const FontMetrics metrics = face->Metrics();
	const XYPOSITION baseline = metrics.ascent;
	const XYPOSITION penX = 8.0;
	renderer.DrawGlyph(penX, baseline, face, glyphId, fg);
	const InkBounds baseInk = FindInkBounds(buffer, bg);
	REQUIRE_FALSE(baseInk.Empty());

	renderer.Clear(bg);
	// Positive HarfBuzz yOffset moves up after conversion to surface coordinates.
	const XYPOSITION yOffset = 6.0;
	renderer.DrawGlyph(penX + 0.0, baseline - yOffset, face, glyphId, fg);
	const InkBounds shifted = FindInkBounds(buffer, bg);
	REQUIRE_FALSE(shifted.Empty());
	CHECK(shifted.top + 4 <= baseInk.top);

	renderer.Clear(bg);
	const XYPOSITION xOffset = 10.0;
	renderer.DrawGlyph(penX + xOffset, baseline, face, glyphId, fg);
	const InkBounds right = FindInkBounds(buffer, bg);
	REQUIRE_FALSE(right.Empty());
	CHECK(right.left >= baseInk.left + 6);
}
