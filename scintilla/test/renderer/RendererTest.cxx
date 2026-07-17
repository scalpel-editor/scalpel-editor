// scalpel-editor offscreen renderer tests (headless EGL, no compositor).

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include "Geometry.h"
#include "GlContext.h"
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
