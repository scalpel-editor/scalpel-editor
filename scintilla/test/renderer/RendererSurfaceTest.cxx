#include "RendererTest.h"

#include <utility>

TEST_CASE("headless GlContext creates OpenGL 3.3 on a software EGL device") {
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
	const std::string renderer = context.RendererString();
	REQUIRE((renderer.find("llvmpipe") != std::string::npos ||
		renderer.find("softpipe") != std::string::npos));
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

TEST_CASE("Renderer binds its selected window surface target") {
	GlContext context;
	Renderer renderer(context, GlContext::SurfaceTarget::Editor);
	context.ReleaseCurrent();
	renderer.MakeCurrent();
	CHECK(context.IsCurrent());
	CHECK(context.CurrentTarget() == GlContext::SurfaceTarget::Editor);
	CHECK_THROWS_WITH(
		[&context] {
			Renderer popupRenderer(
				context, GlContext::SurfaceTarget::Popup);
		}(),
		"GlContext::MakeCurrent popup surface missing");
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

TEST_CASE("scaled sibling pixmap text matches direct framebuffer text") {
	GlContext context;
	Renderer renderer(context);
	ColourBuffer target;
	target.Resize(80, 40);
	std::unique_ptr<DrawSurface> surface = CreateExternalDrawSurface(
		renderer, target.FramebufferName(), 80, 40, 40, 20,
		RasterScale::FromParts(2, 1));

	FontCache fonts;
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	std::shared_ptr<FontFace> face =
		fonts.LoadPath(primary, FontParameters("fixture", 12.0));
	std::shared_ptr<Font> font = FontFromFace(face);
	const ColourRGBA background(255, 255, 255, 255);
	const ColourRGBA foreground(0, 0, 0, 255);
	renderer.Clear(background);

	const XYPOSITION baseline = surface->Ascent(font.get()) + 2.0f;
	surface->DrawTextTransparent(PRectangle::FromInts(2, 0, 18, 20),
		font.get(), baseline, "Ag", foreground);

	std::unique_ptr<Surface> pixmap = surface->AllocatePixMap(20, 20);
	auto *drawPixmap = dynamic_cast<DrawSurface *>(pixmap.get());
	REQUIRE(drawPixmap != nullptr);
	CHECK(drawPixmap->Buffer().Width() == 40);
	CHECK(drawPixmap->Buffer().Height() == 40);
	pixmap->FillRectangle(PRectangle::FromInts(0, 0, 20, 20), Fill(background));
	pixmap->DrawTextTransparent(PRectangle::FromInts(2, 0, 18, 20),
		font.get(), baseline, "Ag", foreground);
	surface->Copy(PRectangle::FromInts(20, 0, 40, 20), Point(0, 0), *pixmap);

	const std::vector<uint8_t> pixels = target.ReadPixelsTopDown();
	for (int y = 0; y < 40; y++) {
		const auto left = pixels.begin() + static_cast<ptrdiff_t>(y * 80 * 4);
		const auto middle = left + 40 * 4;
		CHECK(std::equal(left, middle, middle));
	}
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

TEST_CASE("color emoji DrawText edge coverage stays soft at physical scales") {
	FontCache fonts;
	const std::filesystem::path emojiPath =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "EmojiFixture.ttf";
	std::shared_ptr<FontFace> emoji = fonts.LoadPath(emojiPath, FontParameters("fixture-emoji", 16.0));
	std::shared_ptr<Font> font = FontFromFace(emoji);
	const std::string grinning = "\xF0\x9F\x98\x80";
	const ColourRGBA fg(255, 255, 255, 255);
	const ColourRGBA black(0, 0, 0, 255);

	GlContext context;
	Renderer renderer(context);
	const RasterScale scales[] = {
		RasterScale{},
		RasterScale::FromParts(5, 4),
		RasterScale::FromParts(3, 2),
		RasterScale::FromParts(2, 1),
	};
	constexpr int logicalWidth = 64;
	constexpr int logicalHeight = 48;
	constexpr double kPi = 3.14159265358979323846;

	for (const RasterScale scale : scales) {
		INFO("scale " << scale.Numerator() << "/" << scale.Denominator());
		const int bufferWidth = logicalWidth * static_cast<int>(scale.Numerator()) /
			static_cast<int>(scale.Denominator());
		const int bufferHeight = logicalHeight * static_cast<int>(scale.Numerator()) /
			static_cast<int>(scale.Denominator());
		ColourBuffer buffer;
		buffer.Resize(bufferWidth, bufferHeight);
		std::unique_ptr<DrawSurface> surface = CreateExternalDrawSurface(renderer,
			buffer.FramebufferName(), bufferWidth, bufferHeight, logicalWidth, logicalHeight, scale);
		const XYPOSITION ybase = surface->Ascent(font.get());
		renderer.Clear(black);
		surface->DrawTextTransparent(PRectangle::FromInts(2, 0, logicalWidth, logicalHeight),
			font.get(), ybase, grinning, fg);

		REQUIRE(HasNonBackgroundInk(buffer, black));
		const InkBounds ink = FindInkBounds(buffer, black);
		REQUIRE_FALSE(ink.Empty());

		std::vector<std::pair<int, int>> partial;
		int solidCount = 0;
		const double cx = 0.5 * (ink.left + ink.right);
		const double cy = 0.5 * (ink.top + ink.bottom);
		const int maxRadius =
			std::max(ink.right - ink.left, ink.bottom - ink.top) + 2;
		for (int y = ink.top; y < ink.bottom; y++) {
			for (int x = ink.left; x < ink.right; x++) {
				const ColourRGBA px = buffer.ReadPixel(x, y);
				if (ExactColour(px, black)) {
					continue;
				}
				const int maxCh = std::max({static_cast<int>(px.GetRed()),
					static_cast<int>(px.GetGreen()), static_cast<int>(px.GetBlue())});
				if (maxCh >= 200) {
					++solidCount;
				} else if (maxCh >= 16) {
					partial.emplace_back(x, y);
				}
			}
		}
		CHECK(solidCount >= 4);
		CHECK(partial.size() >= 4);

		int softAngleHits = 0;
		for (int i = 0; i < 8; i++) {
			const double angle0 = kPi * 0.25 * static_cast<double>(i);
			const double angle1 = angle0 + kPi * 0.25;
			bool hit = false;
			for (const auto [x, y] : partial) {
				const double dx = static_cast<double>(x) + 0.5 - cx;
				const double dy = static_cast<double>(y) + 0.5 - cy;
				double angle = std::atan2(dy, dx);
				if (angle < 0.0) {
					angle += 2.0 * kPi;
				}
				if (angle >= angle0 && angle < angle1 &&
					std::hypot(dx, dy) >= 0.25 * maxRadius) {
					hit = true;
					break;
				}
			}
			if (hit) {
				++softAngleHits;
			}
		}
		CHECK(softAngleHits >= 4);
	}
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

TEST_CASE("Partial repaint skips warm text runs without reshaping") {
	FontCache fonts;
	const auto face = fonts.LoadPath(std::filesystem::path(SCALPEL_TEST_FONT_DIR) /
		"FallbackPrimary.ttf", FontParameters("fixture", 16.0));
	const auto font = FontFromFace(face);
	GlContext context;
	Renderer renderer(context);
	auto surface = CreateDrawSurface(renderer, 240, 48, {});
	const ColourRGBA background(33, 43, 53);
	const ColourRGBA foreground(203, 173, 153, 193);
	const auto paint = [&] {
		surface->DrawTextTransparent(PRectangle(4, 0, 80, 40), font.get(), 24,
			"left", foreground);
		surface->DrawTextTransparent(PRectangle(90, 0, 160, 40), font.get(), 24,
			"middle", foreground);
		surface->DrawTextTransparent(PRectangle(174, 0, 240, 40), font.get(), 24,
			"right", foreground);
	};
	renderer.Clear(background);
	paint();
	const auto reference = surface->Buffer().ReadPixelsTopDown();
	const size_t misses = surface->RunCache().MissCount();
	renderer.Clear(background);
	surface->SetClip(PRectangle(90, 0, 110, 48));
	renderer.ResetGlyphCounts();
	surface->ResetTextCounts();
	paint();
	CHECK(surface->TextCounts().attempted == 3);
	CHECK(surface->TextCounts().clipped == 2);
	CHECK(surface->RunCache().MissCount() == misses);
	CHECK(renderer.GlyphCounts().attempted == 6);
	CHECK(renderer.GlyphCounts().uploaded == 0);
	const auto pixels = surface->Buffer().ReadPixelsTopDown();
	for (int y = 0; y < 48; ++y) {
		for (int x = 0; x < 240; ++x) {
			REQUIRE(PixelAt(pixels, 240, x, y) ==
				(x >= 90 && x < 110 ? PixelAt(reference, 240, x, y) : background));
		}
	}
	// A previously rejected run moved into the clip must be drawn again.
	surface->ResetTextCounts();
	renderer.ResetGlyphCounts();
	surface->DrawTextTransparent(PRectangle(92, 0, 160, 40), font.get(), 24,
		"right", foreground);
	CHECK(surface->TextCounts().clipped == 0);
	CHECK(renderer.GlyphCounts().submitted > 0);
}

TEST_CASE("Partial repaint fractional regions cover each buffer pixel once") {
	for (const int scale : {125, 150, 200}) {
		CAPTURE(scale);
		GlContext context;
		Renderer renderer(context);
		ColourBuffer buffer;
		const int width = (81 * scale + 99) / 100;
		const int height = (61 * scale + 99) / 100;
		buffer.Resize(width, height);
		auto surface = CreateExternalDrawSurface(renderer, buffer.FramebufferName(),
			width, height, 81, 61, RasterScale::FromParts(scale, 100), {});
		const std::vector<PRectangle> damage{PRectangle(1, 1, 7, 9),
			PRectangle(7, 1, 12, 5), PRectangle(3, 9, 10, 14), PRectangle(50, 40, 60, 50)};
		const auto regions = surface->PrepareRepaint(damage);
		const ColourRGBA background(20, 30, 40);
		const ColourRGBA foreground(200, 100, 60, 111);
		renderer.Clear(background);
		surface->FillRectangle(PRectangle(0, 0, 81, 61), Fill(foreground));
		const auto reference = buffer.ReadPixelsTopDown();
		renderer.Clear(background);
		for (const auto &region : regions) {
			surface->SetBufferClip(region.clip);
			// A nested logical clip must not reopen rounded overlaps.
			surface->SetClip(region.area);
			surface->FillRectangle(PRectangle(0, 0, 81, 61), Fill(foreground));
			surface->PopClip();
		}
		const auto pixels = buffer.ReadPixelsTopDown();
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				bool covered = false;
				for (const PRectangle area : damage) {
					const PixelRect clip = renderer.LogicalPixelRect(area);
					covered |= x >= clip.left && x < clip.right && y >= clip.top && y < clip.bottom;
				}
				REQUIRE(PixelAt(pixels, width, x, y) ==
					(covered ? PixelAt(reference, width, x, y) : background));
			}
		}
	}
}

TEST_CASE("Partial repaint keeps negative bearings and whole emoji shaping units") {
	FontCache fonts;
	const auto mono = fonts.LoadPath(std::filesystem::path(SCALPEL_TEST_FONT_DIR) /
		"FallbackEmojiMono.ttf", FontParameters("fixture", 24.0));
	const auto emoji = fonts.LoadPath(std::filesystem::path(SCALPEL_TEST_FONT_DIR) /
		"EmojiFixture.ttf", FontParameters("emoji", 24.0));
	const auto font = FontFromFace(mono);
	const auto j = ShapeText("j", mono);
	REQUIRE_FALSE(j.glyphs.empty());
	REQUIRE(mono->RasterizeGlyph(j.glyphs.front().glyphId).left < 0);
	GlContext context;
	Renderer renderer(context);
	auto surface = CreateDrawSurface(renderer, 160, 60, FontFallback::Fixed({emoji}));
	const ColourRGBA background(30, 40, 50);
	const ColourRGBA foreground(220, 180, 120);
	const std::string text = "j 1\xEF\xB8\x8F\xE2\x83\xA3 \xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x9A\x80";
	const auto paint = [&] {
		surface->DrawTextTransparent(PRectangle(40, 0, 160, 60), font.get(), 34,
			text, foreground);
	};
	renderer.Clear(background);
	paint();
	const auto reference = surface->Buffer().ReadPixelsTopDown();
	const size_t misses = surface->RunCache().MissCount();
	for (const PRectangle clip : {PRectangle(39, 0, 40, 60), PRectangle(61, 0, 66, 60),
		PRectangle(95, 0, 100, 60)}) {
		surface->ResetClips();
		renderer.Clear(background);
		surface->SetClip(clip);
		paint();
		CHECK(surface->RunCache().MissCount() == misses);
		const auto pixels = surface->Buffer().ReadPixelsTopDown();
		for (int y = 0; y < 60; ++y) {
			for (int x = 0; x < 160; ++x) {
				REQUIRE(PixelAt(pixels, 160, x, y) ==
					(x >= clip.left && x < clip.right ? PixelAt(reference, 160, x, y) : background));
			}
		}
	}
}
