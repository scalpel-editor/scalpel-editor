// scalpel-editor offscreen renderer tests (headless EGL, no compositor).

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

TEST_CASE("DrawSurface measures through shaped runs; text draw is a no-op") {
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

	// DrawText* must not throw and must not require a renderer.
	surface->DrawTextTransparent(PRectangle::FromInts(0, 0, 10, 10), font.get(), 8.0f, "Hi",
		ColourRGBA(0, 0, 0));
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
