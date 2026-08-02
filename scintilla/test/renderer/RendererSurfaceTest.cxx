#include "RendererTest.h"

TEST_CASE("headless GlContext creates OpenGL 3.3 without a window system display") {
	GlContext context;
	REQUIRE(context.IsCurrent());
	CHECK_FALSE(context.HasWindowSurface());
	CHECK_THROWS_AS(context.SwapBuffers(), std::runtime_error);
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

TEST_CASE("DrawSurface paints into a caller-owned framebuffer") {
	GlContext context;
	Renderer renderer(context);
	ColourBuffer target;
	target.Resize(7, 5);

	std::unique_ptr<DrawSurface> surface = CreateExternalDrawSurface(
		renderer, target.FramebufferName(), target.Width(), target.Height());
	CHECK_FALSE(surface->Buffer().Valid());
	surface->FillRectangle(PRectangle::FromInts(0, 0, 7, 5),
		Fill(ColourRGBA(20, 40, 60, 255)));

	CHECK(ExactColour(target.ReadPixel(0, 0), ColourRGBA(20, 40, 60, 255)));
	CHECK(ExactColour(target.ReadPixel(6, 4), ColourRGBA(20, 40, 60, 255)));
}

TEST_CASE("DrawSurface maps logical coordinates into a scaled framebuffer") {
	GlContext context;
	Renderer renderer(context);
	ColourBuffer target;
	target.Resize(8, 6);
	std::unique_ptr<DrawSurface> surface = CreateExternalDrawSurface(
		renderer, target.FramebufferName(), 8, 6, 4, 3);
	const ColourRGBA black(0, 0, 0, 255);
	const ColourRGBA green(0, 255, 0, 255);
	renderer.Clear(black);

	surface->SetClip(PRectangle::FromInts(1, 1, 3, 2));
	surface->FillRectangle(
		PRectangle::FromInts(0, 0, 4, 3), Fill(green));

	CHECK(ExactColour(target.ReadPixel(1, 2), black));
	CHECK(ExactColour(target.ReadPixel(2, 2), green));
	CHECK(ExactColour(target.ReadPixel(5, 3), green));
	CHECK(ExactColour(target.ReadPixel(6, 3), black));
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

TEST_CASE("color emoji DrawText paints non-monochrome pixels without RGB tint") {
	FontCache fonts;
	const std::filesystem::path emojiPath =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "EmojiFixture.ttf";
	std::shared_ptr<FontFace> emoji = fonts.LoadPath(emojiPath, FontParameters("fixture-emoji", 16.0));
	std::shared_ptr<Font> font = FontFromFace(emoji);

	GlContext context;
	Renderer renderer(context);
	// Tall enough for baseline + downscaled colour glyph.
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 64, 48);
	const ColourRGBA bg(0, 0, 0, 255);
	// Magenta foreground: colour emoji must not become magenta-tinted.
	const ColourRGBA fg(255, 0, 255, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);

	const std::string grinning = "\xF0\x9F\x98\x80";
	const XYPOSITION ybase = surface->Ascent(font.get());
	surface->DrawTextTransparent(PRectangle::FromInts(2, 0, 64, 48), font.get(), ybase,
		grinning, fg);
	REQUIRE(HasNonBackgroundInk(surface->Buffer(), bg));

	bool sawNonMagenta = false;
	for (int y = 0; y < surface->Buffer().Height(); y++) {
		for (int x = 0; x < surface->Buffer().Width(); x++) {
			const ColourRGBA px = surface->Buffer().ReadPixel(x, y);
			if (ExactColour(px, bg)) {
				continue;
			}
			// Blended on black: any non-zero G or unequal R/B shows colour, not magenta mask.
			if (px.GetGreen() > 8 || std::abs(static_cast<int>(px.GetRed()) -
					static_cast<int>(px.GetBlue())) > 8) {
				sawNonMagenta = true;
			}
		}
	}
	CHECK(sawNonMagenta);

	const ShapedRun run = ShapeText(grinning, emoji);
	CHECK(surface->WidthText(font.get(), grinning) == run.Width());
	CHECK(run.Width() > 0.0);
	// Logical ink width stays near the scaled advance, not the 136px strike.
	const InkBounds ink = FindInkBounds(surface->Buffer(), bg);
	REQUIRE_FALSE(ink.Empty());
	CHECK(ink.right - ink.left < 40);
	CHECK(ink.bottom - ink.top < 40);

	// Cache reuses the same face+glyph entry.
	const size_t cacheBefore = renderer.GlyphCacheSize();
	surface->DrawTextTransparent(PRectangle::FromInts(2, 0, 64, 48), font.get(), ybase,
		grinning, fg);
	CHECK(renderer.GlyphCacheSize() == cacheBefore);
}

TEST_CASE("color emoji DrawText edge coverage stays soft without dark fringes") {
	FontCache fonts;
	const std::filesystem::path emojiPath =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "EmojiFixture.ttf";
	std::shared_ptr<FontFace> emoji = fonts.LoadPath(emojiPath, FontParameters("fixture-emoji", 16.0));
	std::shared_ptr<Font> font = FontFromFace(emoji);
	const std::string grinning = "\xF0\x9F\x98\x80";
	const ColourRGBA fg(255, 255, 255, 255);
	const ColourRGBA black(0, 0, 0, 255);
	const ColourRGBA white(255, 255, 255, 255);

	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 64, 48);
	surface->BindDrawTarget();
	const XYPOSITION ybase = surface->Ascent(font.get());
	const auto paint = [&](ColourRGBA bg) {
		renderer.Clear(bg);
		surface->DrawTextTransparent(PRectangle::FromInts(2, 0, 64, 48), font.get(), ybase,
			grinning, fg);
	};

	// Capture black-background samples first (partial coverage is visible as dim RGB).
	paint(black);
	REQUIRE(HasNonBackgroundInk(surface->Buffer(), black));
	const InkBounds ink = FindInkBounds(surface->Buffer(), black);
	REQUIRE_FALSE(ink.Empty());

	struct Sample {
		int x = 0;
		int y = 0;
		ColourRGBA blackPx{};
	};
	std::vector<Sample> partial;
	std::vector<Sample> solid;
	int softAngleHits = 0;
	const double cx = 0.5 * (ink.left + ink.right);
	const double cy = 0.5 * (ink.top + ink.bottom);
	const int maxRadius =
		std::max(ink.right - ink.left, ink.bottom - ink.top) + 2;
	constexpr double kPi = 3.14159265358979323846;

	for (int y = ink.top; y < ink.bottom; y++) {
		for (int x = ink.left; x < ink.right; x++) {
			const ColourRGBA px = surface->Buffer().ReadPixel(x, y);
			if (ExactColour(px, black)) {
				continue;
			}
			const int maxCh = std::max({static_cast<int>(px.GetRed()),
				static_cast<int>(px.GetGreen()), static_cast<int>(px.GetBlue())});
			if (maxCh >= 200) {
				solid.push_back(Sample{x, y, px});
			} else if (maxCh >= 16) {
				// Intermediate coverage after area reduction + residual linear filter.
				partial.push_back(Sample{x, y, px});
			}
		}
	}
	CHECK(solid.size() >= 4);
	CHECK(partial.size() >= 4);

	// Several angular sectors around the ink centre contain partial edge samples.
	for (int i = 0; i < 8; i++) {
		const double angle0 = kPi * 0.25 * static_cast<double>(i);
		const double angle1 = angle0 + kPi * 0.25;
		bool hit = false;
		for (const Sample &s : partial) {
			const double dx = static_cast<double>(s.x) + 0.5 - cx;
			const double dy = static_cast<double>(s.y) + 0.5 - cy;
			double ang = std::atan2(dy, dx);
			if (ang < 0.0) {
				ang += 2.0 * kPi;
			}
			double a0 = angle0;
			double a1 = angle1;
			if (a0 < 0.0) {
				a0 += 2.0 * kPi;
				a1 += 2.0 * kPi;
			}
			// Normalize sample into [0, 2pi) already; sector is contiguous in that range.
			const bool inSector = (ang >= a0 && ang < a1) ||
				(a1 > 2.0 * kPi && (ang >= a0 || ang < a1 - 2.0 * kPi));
			if (inSector) {
				const double r = std::hypot(dx, dy);
				// Prefer the outer half of the ink so we score rim coverage.
				if (r >= 0.25 * maxRadius) {
					hit = true;
					break;
				}
			}
		}
		if (hit) {
			++softAngleHits;
		}
	}
	CHECK(softAngleHits >= 4);

	// Re-paint on white and compare partial samples: dark fringes from filtering
	// straight alpha would make the white-bg edge darker than the black-bg sample.
	paint(white);
	int fringeViolations = 0;
	int channelSpreadViolations = 0;
	for (const Sample &s : partial) {
		const ColourRGBA w = surface->Buffer().ReadPixel(s.x, s.y);
		const int bLuma = static_cast<int>(s.blackPx.GetRed()) +
			static_cast<int>(s.blackPx.GetGreen()) + static_cast<int>(s.blackPx.GetBlue());
		const int wLuma =
			static_cast<int>(w.GetRed()) + static_cast<int>(w.GetGreen()) +
			static_cast<int>(w.GetBlue());
		// White destination should not produce a darker composite on partial edges.
		if (wLuma + 48 < bLuma) {
			++fringeViolations;
		}
		// Premultiplied-friendly colour: no single channel on black-bg partial
		// samples runs far ahead of the others (straight-alpha fringe symptom).
		const int maxCh = std::max({static_cast<int>(s.blackPx.GetRed()),
			static_cast<int>(s.blackPx.GetGreen()), static_cast<int>(s.blackPx.GetBlue())});
		const int minCh = std::min({static_cast<int>(s.blackPx.GetRed()),
			static_cast<int>(s.blackPx.GetGreen()), static_cast<int>(s.blackPx.GetBlue())});
		if (maxCh >= 24 && maxCh - minCh > maxCh * 9 / 10 + 12) {
			// Allow strong yellow (R/G >> B) but reject near-single-channel spikes.
			const int midCh = static_cast<int>(s.blackPx.GetRed()) +
				static_cast<int>(s.blackPx.GetGreen()) + static_cast<int>(s.blackPx.GetBlue()) -
				maxCh - minCh;
			if (midCh < maxCh / 5) {
				++channelSpreadViolations;
			}
		}
	}
	CHECK(fringeViolations == 0);
	CHECK(channelSpreadViolations == 0);
}

TEST_CASE("color emoji DrawText respects clip and overall text alpha") {
	FontCache fonts;
	const std::filesystem::path emojiPath =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "EmojiFixture.ttf";
	std::shared_ptr<FontFace> emoji = fonts.LoadPath(emojiPath, FontParameters("fixture-emoji", 16.0));
	std::shared_ptr<Font> font = FontFromFace(emoji);

	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 48, 48);
	const ColourRGBA bg(32, 32, 32, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);

	const std::string grinning = "\xF0\x9F\x98\x80";
	const XYPOSITION ybase = surface->Ascent(font.get());
	// Clip to a thin left strip so most of the glyph is discarded.
	surface->SetClip(PRectangle::FromInts(0, 0, 4, 48));
	surface->DrawTextTransparent(PRectangle::FromInts(0, 0, 48, 48), font.get(), ybase,
		grinning, ColourRGBA(255, 255, 255, 255));
	surface->PopClip();

	const InkBounds clipped = FindInkBounds(surface->Buffer(), bg);
	if (!clipped.Empty()) {
		CHECK(clipped.right <= 4);
	}

	const ColourRGBA alphaBg(0, 0, 0, 255);
	renderer.Clear(alphaBg);
	surface->DrawTextTransparent(PRectangle::FromInts(2, 0, 48, 48), font.get(), ybase,
		grinning, ColourRGBA(255, 255, 255, 128));
	REQUIRE(HasNonBackgroundInk(surface->Buffer(), alphaBg));

	const auto brightestChannel = [&]() {
		uint8_t brightest = 0;
		for (int y = 0; y < surface->Buffer().Height(); y++) {
			for (int x = 0; x < surface->Buffer().Width(); x++) {
				const ColourRGBA pixel = surface->Buffer().ReadPixel(x, y);
				brightest = std::max({brightest, pixel.GetRed(), pixel.GetGreen(), pixel.GetBlue()});
			}
		}
		return brightest;
	};
	const uint8_t halfAlphaBrightness = brightestChannel();

	renderer.Clear(alphaBg);
	surface->DrawTextTransparent(PRectangle::FromInts(2, 0, 48, 48), font.get(), ybase,
		grinning, ColourRGBA(255, 255, 255, 255));
	const uint8_t opaqueBrightness = brightestChannel();
	CHECK(halfAlphaBrightness < opaqueBrightness);
}
