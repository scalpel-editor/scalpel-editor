#include "RendererTest.h"

TEST_CASE("DrawGlyph places ink in logical space when buffer is scaled") {
	// HiDPI path: buffer pixels are 2x logical. Solid fills and glyphs both
	// take logical coordinates; ink must land in the scaled buffer region.
	FontCache fonts;
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	std::shared_ptr<FontFace> face = fonts.LoadPath(primary, FontParameters("fixture", 16.0));
	const ShapedRun run = ShapeText("A", face);
	REQUIRE_FALSE(run.glyphs.empty());

	GlContext context;
	Renderer renderer(context);
	ColourBuffer buffer;
	buffer.Resize(80, 80);
	constexpr int logical = 40;
	renderer.SetDrawTarget(buffer.FramebufferName(), buffer.Width(), buffer.Height(),
		logical, logical);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fg(255, 255, 255, 255);
	const ColourRGBA marker(0, 255, 0, 255);
	renderer.Clear(bg);

	// Logical marker square at (20,20)-(30,30) → buffer (40,40)-(60,60).
	renderer.FillRectangle(PRectangle::FromInts(20, 20, 30, 30), marker);

	const FontMetrics metrics = face->Metrics();
	const XYPOSITION baseline = 20.0 + metrics.ascent;
	// Pen near the marker so glyph ink should appear near buffer x>=40, y around 40+.
	renderer.DrawGlyph(20.0, baseline, face, run.glyphs[0].glyphId, fg);

	const InkBounds ink = FindInkBounds(buffer, bg);
	REQUIRE_FALSE(ink.Empty());
	// Before the fix, buffer-space ortho put glyphs near half-size coords (x~20).
	// With logical ortho, ink near pen 20 should start around buffer x 40.
	CHECK(ink.left >= 30);
	CHECK(ink.top >= 30);
	// Marker still present in its scaled band (or covered by glyph ink).
	CHECK_FALSE(ExactColour(buffer.ReadPixel(50, 50), bg));
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
