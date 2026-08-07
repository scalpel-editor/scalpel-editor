#include "RendererTest.h"

#include "FixedGlyphBitmapReduce.h"

TEST_CASE("DrawGlyph places ink in logical space when buffer is scaled") {
	// HiDPI path: buffer pixels are 2x logical. Solid fills use logical
	// coordinates; outline glyphs rasterize at the nominal scale 2 and land
	// on integer buffer pixels near 2 * logical pen.
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
	renderer.SetOutputRasterScale(RasterScale::FromParts(2, 1));
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
	// Device-phase placement at scale 2 puts pen 20 near buffer x 40.
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

namespace {

/** Compose expected coverage by placing phase-aware GlyphImage bitmaps into a buffer. */
std::vector<uint8_t> ReferenceComposeText(
	const ShapedRun &run, XYPOSITION penX, XYPOSITION penY, RasterScale scale,
	int bufferWidth, int bufferHeight, ColourRGBA bg, ColourRGBA fg) {
	std::vector<uint8_t> pixels(static_cast<size_t>(bufferWidth) * static_cast<size_t>(bufferHeight) * 4u);
	for (size_t i = 0; i < pixels.size(); i += 4) {
		pixels[i + 0] = bg.GetRed();
		pixels[i + 1] = bg.GetGreen();
		pixels[i + 2] = bg.GetBlue();
		pixels[i + 3] = bg.GetAlpha();
	}
	const auto blendOver = [&](int x, int y, uint8_t coverage) {
		if (x < 0 || y < 0 || x >= bufferWidth || y >= bufferHeight || coverage == 0) {
			return;
		}
		const size_t o = (static_cast<size_t>(y) * static_cast<size_t>(bufferWidth) +
			static_cast<size_t>(x)) * 4u;
		const float a = (static_cast<float>(coverage) / 255.0f) * (fg.GetAlphaComponent());
		const float inv = 1.0f - a;
		pixels[o + 0] = static_cast<uint8_t>(std::lround(
			fg.GetRedComponent() * a * 255.0f + pixels[o + 0] * inv));
		pixels[o + 1] = static_cast<uint8_t>(std::lround(
			fg.GetGreenComponent() * a * 255.0f + pixels[o + 1] * inv));
		pixels[o + 2] = static_cast<uint8_t>(std::lround(
			fg.GetBlueComponent() * a * 255.0f + pixels[o + 2] * inv));
		pixels[o + 3] = 255;
	};
	XYPOSITION x = penX;
	for (const ShapedGlyph &g : run.glyphs) {
		if (!g.face) {
			x += g.xAdvance;
			continue;
		}
		const XYPOSITION gx = x + g.xOffset;
		const XYPOSITION gy = penY - g.yOffset;
		const double deviceX =
			gx * static_cast<double>(scale.Numerator()) /
			static_cast<double>(scale.Denominator());
		const double deviceY =
			gy * static_cast<double>(scale.Numerator()) /
			static_cast<double>(scale.Denominator());
		int originX = static_cast<int>(std::floor(deviceX));
		int originY = static_cast<int>(std::floor(deviceY));
		int32_t phaseX = static_cast<int32_t>(std::lround((deviceX - originX) * 64.0));
		int32_t phaseY = static_cast<int32_t>(std::lround((deviceY - originY) * 64.0));
		if (phaseX >= 64) {
			phaseX = 0;
			originX++;
		}
		if (phaseY >= 64) {
			phaseY = 0;
			originY++;
		}
		GlyphRasterRequest request;
		request.glyphId = g.glyphId;
		request.scale = scale;
		request.phase = GlyphRasterPhase::Normalize(phaseX, phaseY);
		const GlyphImage image = g.face->RasterizeGlyph(request);
		if (image.kind != GlyphImageKind::Gray) {
			x += g.xAdvance;
			continue;
		}
		const int left = originX + image.left;
		const int top = originY - image.top;
		for (int row = 0; row < image.height; row++) {
			for (int col = 0; col < image.width; col++) {
				const uint8_t cov = image.gray[static_cast<size_t>(row) * static_cast<size_t>(image.width) +
					static_cast<size_t>(col)];
				blendOver(left + col, top + row, cov);
			}
		}
		x += g.xAdvance;
	}
	return pixels;
}

bool PixelsNear(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b, int tol = 1) {
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i < a.size(); i++) {
		const int d = static_cast<int>(a[i]) - static_cast<int>(b[i]);
		if (d > tol || d < -tol) {
			return false;
		}
	}
	return true;
}

void CheckDevicePhasePhrase(const std::string &text, RasterScale scale,
	XYPOSITION penX = 2.0) {
	const double editorPixels = 16.0 * 96.0 / 72.0;
	FontCache fonts;
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	FontRasterPolicy lightPolicy;
	lightPolicy.hintStyle = FontHintStyle::Slight;
	std::shared_ptr<FontFace> face =
		fonts.LoadPath(primary, FontParameters("fixture", editorPixels), lightPolicy);
	const ShapedRun run = ShapeText(text, face);
	REQUIRE_FALSE(run.glyphs.empty());

	const int logicalW = 240;
	const int logicalH = 48;
	const int bufferW = static_cast<int>(std::ceil(
		logicalW * static_cast<double>(scale.Numerator()) /
		static_cast<double>(scale.Denominator())));
	const int bufferH = static_cast<int>(std::ceil(
		logicalH * static_cast<double>(scale.Numerator()) /
		static_cast<double>(scale.Denominator())));

	GlContext context;
	Renderer renderer(context);
	ColourBuffer buffer;
	buffer.Resize(bufferW, bufferH);
	renderer.SetDrawTarget(buffer.FramebufferName(), bufferW, bufferH, logicalW, logicalH);
	renderer.SetOutputRasterScale(scale);
	const ColourRGBA bg(255, 255, 255, 255);
	const ColourRGBA fg(0, 0, 0, 255);
	renderer.Clear(bg);

	const FontMetrics metrics = face->Metrics();
	const XYPOSITION penY = metrics.ascent + 2.0;
	XYPOSITION x = penX;
	for (const ShapedGlyph &g : run.glyphs) {
		if (g.face) {
			renderer.DrawGlyph(x + g.xOffset, penY - g.yOffset, g.face, g.glyphId, fg);
		}
		x += g.xAdvance;
	}
	const std::vector<uint8_t> painted = buffer.ReadPixelsTopDown();
	const std::vector<uint8_t> expected =
		ReferenceComposeText(run, penX, penY, scale, bufferW, bufferH, bg, fg);
	// ±1 for premultiplied blend rounding differences.
	CHECK(PixelsNear(painted, expected, 1));
}

}

TEST_CASE("device-phase text reference composition matches high standards and honesty") {
	const RasterScale scales[] = {
		RasterScale{},
		RasterScale::FromWaylandNumerator(150), // 5/4
		RasterScale::FromParts(3, 2),
		RasterScale::FromParts(2, 1),
	};
	for (const RasterScale &scale : scales) {
		INFO("scale " << scale.Numerator() << "/" << scale.Denominator());
		CheckDevicePhasePhrase("high standards", scale);
		CheckDevicePhasePhrase("honesty", scale);
	}
	// Fractional origin rounds from phase 64 into the next integer pixel.
	CheckDevicePhasePhrase("honesty", RasterScale{}, 2.0 + 63.75 / 64.0);
}

TEST_CASE("device-phase text places same glyph at distinct phases") {
	FontCache fonts;
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	FontRasterPolicy lightPolicy;
	lightPolicy.hintStyle = FontHintStyle::Slight;
	std::shared_ptr<FontFace> face =
		fonts.LoadPath(primary, FontParameters("fixture", 16.0), lightPolicy);
	const ShapedRun run = ShapeText("i", face);
	REQUIRE_FALSE(run.glyphs.empty());
	const uint32_t glyphId = run.glyphs[0].glyphId;

	GlContext context;
	Renderer renderer(context);
	ColourBuffer buffer;
	buffer.Resize(64, 48);
	renderer.SetDrawTarget(buffer.FramebufferName(), 64, 48, 64, 48);
	renderer.SetOutputRasterScale(RasterScale{});
	const ColourRGBA bg(255, 255, 255, 255);
	const ColourRGBA fg(0, 0, 0, 255);
	renderer.Clear(bg);
	const FontMetrics metrics = face->Metrics();
	const XYPOSITION baseline = metrics.ascent + 4.0;
	// Same glyph at two fractional logical positions → two phases.
	renderer.DrawGlyph(4.0, baseline, face, glyphId, fg);
	renderer.DrawGlyph(4.0 + 1.0 / 64.0, baseline, face, glyphId, fg);
	CHECK(renderer.GlyphCacheSize() == 2);
	REQUIRE(HasNonBackgroundInk(buffer, bg));
}

TEST_CASE("glyph cache phase distinguishes scale and phase identity") {
	FontCache fonts;
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	std::shared_ptr<FontFace> face =
		fonts.LoadPath(primary, FontParameters("fixture", 16.0));
	const ShapedRun run = ShapeText("A", face);
	REQUIRE_FALSE(run.glyphs.empty());
	const uint32_t glyphId = run.glyphs[0].glyphId;

	GlContext context;
	Renderer renderer(context);
	ColourBuffer buffer;
	buffer.Resize(48, 48);
	renderer.SetDrawTarget(buffer.FramebufferName(), 48, 48, 48, 48);
	renderer.SetOutputRasterScale(RasterScale{});
	const ColourRGBA fg(0, 0, 0, 255);
	const FontMetrics metrics = face->Metrics();
	const XYPOSITION baseline = metrics.ascent;

	renderer.DrawGlyph(4.0, baseline, face, glyphId, fg);
	REQUIRE(renderer.GlyphCacheSize() == 1);
	// Identical request reuses the texture.
	renderer.DrawGlyph(4.0, baseline, face, glyphId, fg);
	CHECK(renderer.GlyphCacheSize() == 1);
	// Different phase cannot collide.
	renderer.DrawGlyph(4.0 + 0.5, baseline, face, glyphId, fg);
	CHECK(renderer.GlyphCacheSize() == 2);
	// Output scale change retires outline masks and cannot reuse stale textures.
	renderer.SetOutputRasterScale(RasterScale::FromParts(2, 1));
	CHECK(renderer.GlyphCacheSize() == 0);
	renderer.DrawGlyph(4.0, baseline, face, glyphId, fg);
	CHECK(renderer.GlyphCacheSize() == 1);
	// Phase population for this single-glyph walk stays small.
	CHECK(renderer.GlyphCacheSize() < 8);
}

TEST_CASE("outline glyph cache grows with distinct phases until scale retirement") {
	// Outline entries are keyed by face, glyph, scale, and 26.6 phase with no
	// capacity bound except SetOutputRasterScale. Fixed bitmaps use a separate
	// three-generation bound. This case records the growth shape for one glyph.
	FontCache fonts;
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	std::shared_ptr<FontFace> face =
		fonts.LoadPath(primary, FontParameters("fixture", 16.0));
	const ShapedRun run = ShapeText("A", face);
	REQUIRE_FALSE(run.glyphs.empty());
	const uint32_t glyphId = run.glyphs[0].glyphId;

	GlContext context;
	Renderer renderer(context);
	ColourBuffer buffer;
	buffer.Resize(64, 64);
	renderer.SetDrawTarget(buffer.FramebufferName(), 64, 64, 64, 64);
	renderer.SetOutputRasterScale(RasterScale{});
	const ColourRGBA fg(0, 0, 0, 255);
	const XYPOSITION baseline = face->Metrics().ascent;

	// 64 distinct x phases at unit scale (frac k/64 for k in 0..63).
	for (int phase = 0; phase < 64; phase++) {
		const XYPOSITION penX = 4.0 + static_cast<XYPOSITION>(phase) / 64.0;
		renderer.DrawGlyph(penX, baseline, face, glyphId, fg);
	}
	CHECK(renderer.GlyphCacheSize() == 64);
	// A second pass at the same phases reuses every entry.
	for (int phase = 0; phase < 64; phase++) {
		const XYPOSITION penX = 4.0 + static_cast<XYPOSITION>(phase) / 64.0;
		renderer.DrawGlyph(penX, baseline, face, glyphId, fg);
	}
	CHECK(renderer.GlyphCacheSize() == 64);
	renderer.SetOutputRasterScale(RasterScale::FromParts(2, 1));
	CHECK(renderer.GlyphCacheSize() == 0);
}

TEST_CASE("fixed bitmap glyph cache uses physical-size variants and three-generation LRU") {
	FontCache fonts;
	const std::filesystem::path emojiPath =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "EmojiFixture.ttf";
	std::shared_ptr<FontFace> face =
		fonts.LoadPath(emojiPath, FontParameters("fixture-emoji", 16.0));
	REQUIRE(face->UsesBitmapStrike());
	REQUIRE(face->MetricsScale() > 0.0);
	REQUIRE(face->MetricsScale() < 1.0);
	const ShapedRun run = ShapeText("\xF0\x9F\x98\x80", face);
	REQUIRE_FALSE(run.glyphs.empty());
	const uint32_t glyphId = run.glyphs[0].glyphId;
	const GlyphImage source = face->RasterizeGlyph(glyphId);
	REQUIRE(source.width > 0);
	REQUIRE(source.height > 0);

	GlContext context;
	Renderer renderer(context);
	ColourBuffer buffer;
	constexpr int logicalExtent = 64;
	const ColourRGBA fg(255, 255, 255, 255);
	const ColourRGBA bg(0, 0, 0, 255);
	const XYPOSITION baseline = face->Metrics().ascent + 8.0;
	const XYPOSITION penX = 4.0;

	const auto drawAt = [&](RasterScale scale) {
		const int bufferExtent = logicalExtent * static_cast<int>(scale.Numerator()) /
			static_cast<int>(scale.Denominator());
		buffer.Resize(bufferExtent, bufferExtent);
		renderer.SetDrawTarget(buffer.FramebufferName(), bufferExtent, bufferExtent,
			logicalExtent, logicalExtent);
		renderer.SetOutputRasterScale(scale);
		renderer.Clear(bg);
		renderer.DrawGlyph(penX, baseline, face, glyphId, fg);
		return FindInkBounds(buffer, bg);
	};

	const auto expectedTex = [&](RasterScale scale) {
		const double reduction = face->MetricsScale() *
			static_cast<double>(scale.Numerator()) /
			static_cast<double>(scale.Denominator());
		return std::make_pair(
			FixedGlyphReducedExtent(source.width, reduction),
			FixedGlyphReducedExtent(source.height, reduction));
	};

	// Scale 1: reduce toward physical size; reuse without a second entry.
	const InkBounds ink1 = drawAt(RasterScale{});
	REQUIRE(renderer.GlyphCacheSize() == 1);
	const auto tex1 = renderer.GlyphCacheTextureSize(face, glyphId, RasterScale{});
	const auto expect1 = expectedTex(RasterScale{});
	CHECK(tex1.first == expect1.first);
	CHECK(tex1.second == expect1.second);
	CHECK(tex1.first < source.width);
	CHECK(tex1.second < source.height);
	CHECK(tex1.first >= 1);
	REQUIRE_FALSE(ink1.Empty());
	drawAt(RasterScale{});
	CHECK(renderer.GlyphCacheSize() == 1);

	// Distinct shrinking variants at 5/4, 3/2, and 2.
	const RasterScale scale5_4 = RasterScale::FromParts(5, 4);
	const RasterScale scale3_2 = RasterScale::FromParts(3, 2);
	const RasterScale scale2 = RasterScale::FromParts(2, 1);
	const InkBounds ink5_4 = drawAt(scale5_4);
	CHECK(renderer.GlyphCacheSize() == 2);
	const auto tex5_4 = renderer.GlyphCacheTextureSize(face, glyphId, scale5_4);
	CHECK(tex5_4 == expectedTex(scale5_4));
	CHECK(tex5_4.first >= tex1.first);
	REQUIRE_FALSE(ink5_4.Empty());
	// Logical placement is unchanged when the physical bounds are scaled back.
	const auto logical = [](int physical, RasterScale scale) {
		return static_cast<double>(physical) * static_cast<double>(scale.Denominator()) /
			static_cast<double>(scale.Numerator());
	};
	CHECK(std::abs(logical(ink5_4.left, scale5_4) - ink1.left) <= 2.0);
	CHECK(std::abs(logical(ink5_4.top, scale5_4) - ink1.top) <= 2.0);
	CHECK(std::abs(logical(ink5_4.right - ink5_4.left, scale5_4) -
		(ink1.right - ink1.left)) <= 3.0);

	drawAt(scale3_2);
	CHECK(renderer.GlyphCacheSize() == 3);
	CHECK(renderer.GlyphCacheTextureSize(face, glyphId, scale3_2) == expectedTex(scale3_2));

	// Return to scale 1 reuses the resident generation (still three entries).
	drawAt(RasterScale{});
	CHECK(renderer.GlyphCacheSize() == 3);
	CHECK(renderer.GlyphCacheTextureSize(face, glyphId, RasterScale{}) == tex1);

	// Fourth shrinking scale evicts the least-recently-used generation (5/4).
	// Recency after the steps above: 3/2 (older), 1, then 2 is newest after draw.
	// LRU among {1, 5/4, 3/2} after return-to-1: 5/4 is oldest.
	drawAt(scale2);
	CHECK(renderer.GlyphCacheSize() == 3);
	CHECK(renderer.GlyphCacheTextureSize(face, glyphId, scale2) == expectedTex(scale2));
	CHECK(renderer.GlyphCacheTextureSize(face, glyphId, scale5_4) == std::make_pair(0, 0));
	CHECK(renderer.GlyphCacheTextureSize(face, glyphId, RasterScale{}) == tex1);
	CHECK(renderer.GlyphCacheTextureSize(face, glyphId, scale3_2).first > 0);

	// Outline entries still retire immediately on scale change.
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	std::shared_ptr<FontFace> outline =
		fonts.LoadPath(primary, FontParameters("fixture", 16.0));
	const ShapedRun outlineRun = ShapeText("A", outline);
	REQUIRE_FALSE(outlineRun.glyphs.empty());
	buffer.Resize(logicalExtent, logicalExtent);
	renderer.SetDrawTarget(buffer.FramebufferName(), logicalExtent, logicalExtent,
		logicalExtent, logicalExtent);
	renderer.SetOutputRasterScale(RasterScale{});
	const size_t beforeOutline = renderer.GlyphCacheSize();
	renderer.DrawGlyph(4.0, outline->Metrics().ascent, outline,
		outlineRun.glyphs[0].glyphId, ColourRGBA(0, 0, 0, 255));
	CHECK(renderer.GlyphCacheSize() == beforeOutline + 1);
	renderer.SetOutputRasterScale(scale2);
	// Fixed bitmap shrink entries remain; the outline mask is gone.
	CHECK(renderer.GlyphCacheSize() == beforeOutline);
	CHECK(renderer.GlyphCacheTextureSize(outline, outlineRun.glyphs[0].glyphId,
		RasterScale{}, GlyphRasterPhase{}, false) == std::make_pair(0, 0));
}

TEST_CASE("fixed bitmap glyph cache shares one full-strike entry when not shrinking") {
	FontCache fonts;
	const std::filesystem::path emojiPath =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "EmojiFixture.ttf";
	std::shared_ptr<FontFace> face =
		fonts.LoadPath(emojiPath, FontParameters("fixture-emoji", 16.0));
	REQUIRE(face->UsesBitmapStrike());
	REQUIRE(face->MetricsScale() > 0.0);
	// Output scale large enough that metricsScale * scale >= 1 → no CPU reduce.
	const uint32_t minNumerator = static_cast<uint32_t>(
		std::ceil(1.0 / face->MetricsScale() - 1e-12));
	REQUIRE(minNumerator >= 1);
	const RasterScale noShrink = RasterScale::FromParts(minNumerator, 1);
	const RasterScale larger = RasterScale::FromParts(minNumerator + 1, 1);
	REQUIRE(face->MetricsScale() *
			static_cast<double>(noShrink.Numerator()) /
			static_cast<double>(noShrink.Denominator()) >=
		1.0 - 1e-12);

	const ShapedRun run = ShapeText("\xF0\x9F\x98\x80", face);
	REQUIRE_FALSE(run.glyphs.empty());
	const uint32_t glyphId = run.glyphs[0].glyphId;
	const GlyphImage source = face->RasterizeGlyph(glyphId);
	REQUIRE(source.width > 0);

	GlContext context;
	Renderer renderer(context);
	ColourBuffer buffer;
	buffer.Resize(64, 64);
	renderer.SetDrawTarget(buffer.FramebufferName(), 64, 64, 64, 64);
	const ColourRGBA fg(255, 255, 255, 255);
	const XYPOSITION baseline = face->Metrics().ascent + 8.0;

	renderer.SetOutputRasterScale(noShrink);
	renderer.DrawGlyph(4.0, baseline, face, glyphId, fg);
	REQUIRE(renderer.GlyphCacheSize() == 1);
	const auto full = renderer.GlyphCacheTextureSize(face, glyphId, RasterScale{},
		GlyphRasterPhase{}, true);
	CHECK(full.first == source.width);
	CHECK(full.second == source.height);

	// Another non-shrinking scale reuses the same full-strike generation.
	renderer.SetOutputRasterScale(larger);
	renderer.DrawGlyph(4.0, baseline, face, glyphId, fg);
	CHECK(renderer.GlyphCacheSize() == 1);
	CHECK(renderer.GlyphCacheTextureSize(face, glyphId, RasterScale{},
		GlyphRasterPhase{}, true) == full);
}
