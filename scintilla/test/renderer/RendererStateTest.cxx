#include "RendererTest.h"

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

namespace {

[[nodiscard]] GLint DrawFramebufferBinding() {
	GLint bound = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &bound);
	return bound;
}

[[nodiscard]] GLint ReadFramebufferBinding() {
	GLint bound = 0;
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &bound);
	return bound;
}

struct Viewport {
	GLint x = 0;
	GLint y = 0;
	GLint width = 0;
	GLint height = 0;
};

[[nodiscard]] Viewport CurrentViewport() {
	GLint values[4] = {};
	glGetIntegerv(GL_VIEWPORT, values);
	return {values[0], values[1], values[2], values[3]};
}

void RequireNoEmittedSetup(const GlContext::DrawSetupCounts &counts) {
	CHECK(counts.contextEmitted == 0);
	CHECK(counts.framebufferEmitted == 0);
	CHECK(counts.viewportEmitted == 0);
	CHECK(counts.scissorEmitted == 0);
}

[[nodiscard]] std::shared_ptr<Font> LoadFixtureFont() {
	static FontCache fonts;
	const std::filesystem::path primary =
		std::filesystem::path(SCALPEL_TEST_FONT_DIR) / "FallbackPrimary.ttf";
	return FontFromFace(fonts.LoadPath(primary, FontParameters("fixture", 16.0)));
}

}

TEST_CASE("Renderer state repeated preparation keeps pixels and GL target") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 8, 8);
	const ColourRGBA bg(10, 20, 30, 255);
	const ColourRGBA fg(255, 0, 0, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);
	surface->FillRectangle(PRectangle::FromInts(2, 2, 6, 6), Fill(fg));
	const auto first = surface->Buffer().ReadPixelsTopDown();
	REQUIRE(DrawFramebufferBinding() ==
		static_cast<GLint>(surface->Buffer().FramebufferName()));
	REQUIRE(ReadFramebufferBinding() == DrawFramebufferBinding());

	surface->BindDrawTarget();
	surface->BindDrawTarget();
	const auto second = surface->Buffer().ReadPixelsTopDown();
	REQUIRE(first == second);
	REQUIRE(DrawFramebufferBinding() ==
		static_cast<GLint>(surface->Buffer().FramebufferName()));
	REQUIRE(context.SurfacesCurrent(GlContext::SurfaceTarget::Editor));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(2, 2), fg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), bg));
}

TEST_CASE("Renderer state reacquires after context release") {
	GlContext context;
	Renderer renderer(context);
	ColourBuffer buffer;
	buffer.Resize(4, 4);
	renderer.SetDrawTarget(buffer.FramebufferName(), 4, 4);
	const ColourRGBA red(255, 0, 0, 255);
	const ColourRGBA blue(0, 0, 255, 255);
	renderer.Clear(red);
	REQUIRE(context.IsCurrent());
	REQUIRE(context.SurfacesCurrent(GlContext::SurfaceTarget::Editor));

	context.ReleaseCurrent();
	REQUIRE_FALSE(context.IsCurrent());
	REQUIRE_FALSE(context.SurfacesCurrent(GlContext::SurfaceTarget::Editor));
	REQUIRE(GlContext::CurrentOnThread() == nullptr);

	renderer.Clear(blue);
	REQUIRE(context.IsCurrent());
	REQUIRE(context.SurfacesCurrent(GlContext::SurfaceTarget::Editor));
	REQUIRE(GlContext::CurrentOnThread() == &context);
	REQUIRE(DrawFramebufferBinding() == static_cast<GLint>(buffer.FramebufferName()));
	REQUIRE(ExactColour(buffer.ReadPixel(0, 0), blue));
	REQUIRE(ExactColour(buffer.ReadPixel(3, 3), blue));
}

TEST_CASE("Renderer state alternates renderers on one context") {
	GlContext context;
	Renderer first(context);
	Renderer second(context);
	ColourBuffer large;
	ColourBuffer small;
	large.Resize(8, 8);
	small.Resize(4, 4);
	const ColourRGBA red(255, 0, 0, 255);
	const ColourRGBA blue(0, 0, 255, 255);
	const ColourRGBA green(0, 255, 0, 255);

	first.SetDrawTarget(large.FramebufferName(), 8, 8);
	first.Clear(red);
	first.SetClip(PRectangle::FromInts(2, 2, 6, 6));
	REQUIRE(first.ClipDepth() == 1);

	second.SetDrawTarget(small.FramebufferName(), 4, 4);
	second.Clear(blue);
	REQUIRE(second.ClipDepth() == 0);
	REQUIRE(DrawFramebufferBinding() == static_cast<GLint>(small.FramebufferName()));
	const Viewport smallViewport = CurrentViewport();
	REQUIRE(smallViewport.width == 4);
	REQUIRE(smallViewport.height == 4);

	first.FillRectangleOpaque(PRectangle::FromInts(0, 0, 8, 8), green);
	REQUIRE(DrawFramebufferBinding() == static_cast<GLint>(large.FramebufferName()));
	const Viewport largeViewport = CurrentViewport();
	REQUIRE(largeViewport.width == 8);
	REQUIRE(largeViewport.height == 8);
	REQUIRE(first.ClipDepth() == 1);
	REQUIRE(ExactColour(large.ReadPixel(0, 0), red));
	REQUIRE(ExactColour(large.ReadPixel(3, 3), green));
	REQUIRE(ExactColour(small.ReadPixel(0, 0), blue));
	REQUIRE(ExactColour(small.ReadPixel(3, 3), blue));
}

TEST_CASE("Renderer state recovers after another context is current") {
	GlContext first;
	Renderer renderer(first);
	ColourBuffer buffer;
	buffer.Resize(4, 4);
	renderer.SetDrawTarget(buffer.FramebufferName(), 4, 4);
	const ColourRGBA red(255, 0, 0, 255);
	const ColourRGBA blue(0, 0, 255, 255);
	renderer.Clear(red);

	{
		GlContext second;
		REQUIRE(second.IsCurrent());
		REQUIRE_FALSE(first.IsCurrent());
		REQUIRE(GlContext::CurrentOnThread() == &second);
		renderer.Clear(blue);
		REQUIRE(first.IsCurrent());
		REQUIRE(GlContext::CurrentOnThread() == &first);
		REQUIRE(DrawFramebufferBinding() ==
			static_cast<GLint>(buffer.FramebufferName()));
	}

	REQUIRE(ExactColour(buffer.ReadPixel(0, 0), blue));
	renderer.Clear(red);
	REQUIRE(ExactColour(buffer.ReadPixel(1, 1), red));
}

TEST_CASE("Renderer state preserves target across allocation readback and deletion") {
	GlContext context;
	Renderer renderer(context);
	ColourBuffer target;
	target.Resize(6, 4);
	renderer.SetDrawTarget(target.FramebufferName(), 6, 4);
	const ColourRGBA red(255, 0, 0, 255);
	const ColourRGBA green(0, 255, 0, 255);
	renderer.Clear(red);
	REQUIRE(DrawFramebufferBinding() == static_cast<GLint>(target.FramebufferName()));

	ColourBuffer extra;
	extra.Resize(5, 5);
	REQUIRE(DrawFramebufferBinding() == static_cast<GLint>(target.FramebufferName()));
	REQUIRE(ReadFramebufferBinding() == DrawFramebufferBinding());
	REQUIRE(ExactColour(target.ReadPixel(0, 0), red));
	REQUIRE(DrawFramebufferBinding() == static_cast<GLint>(target.FramebufferName()));
	const auto pixels = target.ReadPixelsTopDown();
	REQUIRE(pixels.size() == 6u * 4u * 4u);
	REQUIRE(DrawFramebufferBinding() == static_cast<GLint>(target.FramebufferName()));

	extra.Destroy();
	REQUIRE(DrawFramebufferBinding() == static_cast<GLint>(target.FramebufferName()));
	renderer.Clear(green);
	REQUIRE(ExactColour(target.ReadPixel(0, 0), green));
	REQUIRE(ExactColour(target.ReadPixel(5, 3), green));
}

TEST_CASE("Renderer state same-sized targets switch framebuffer not clip identity") {
	GlContext context;
	Renderer renderer(context);
	ColourBuffer first;
	ColourBuffer second;
	first.Resize(8, 8);
	second.Resize(8, 8);
	const ColourRGBA red(255, 0, 0, 255);
	const ColourRGBA blue(0, 0, 255, 255);

	renderer.SetDrawTarget(first.FramebufferName(), 8, 8);
	renderer.Clear(red);
	renderer.SetClip(PRectangle::FromInts(1, 1, 4, 4));
	REQUIRE(renderer.ClipDepth() == 1);
	renderer.SetDrawTarget(first.FramebufferName(), 8, 8);
	REQUIRE(renderer.ClipDepth() == 1);

	renderer.SetDrawTarget(second.FramebufferName(), 8, 8);
	REQUIRE(renderer.ClipDepth() == 0);
	REQUIRE(DrawFramebufferBinding() == static_cast<GLint>(second.FramebufferName()));
	const Viewport viewport = CurrentViewport();
	REQUIRE(viewport.width == 8);
	REQUIRE(viewport.height == 8);
	renderer.Clear(blue);

	renderer.SetDrawTarget(first.FramebufferName(), 8, 8);
	REQUIRE(renderer.ClipDepth() == 0);
	REQUIRE(ExactColour(first.ReadPixel(0, 0), red));
	REQUIRE(ExactColour(second.ReadPixel(0, 0), blue));
}

TEST_CASE("Renderer state different-sized targets restore viewport") {
	GlContext context;
	Renderer renderer(context);
	ColourBuffer square;
	ColourBuffer wide;
	square.Resize(8, 8);
	wide.Resize(16, 4);
	const ColourRGBA red(255, 0, 0, 255);
	const ColourRGBA blue(0, 0, 255, 255);

	renderer.SetDrawTarget(square.FramebufferName(), 8, 8, 8, 8);
	renderer.SetClip(PRectangle::FromInts(0, 0, 8, 8));
	REQUIRE(renderer.ClipDepth() == 1);
	renderer.Clear(red);

	renderer.SetDrawTarget(wide.FramebufferName(), 16, 4, 16, 4);
	REQUIRE(renderer.ClipDepth() == 0);
	const Viewport wideViewport = CurrentViewport();
	REQUIRE(wideViewport.width == 16);
	REQUIRE(wideViewport.height == 4);
	renderer.Clear(blue);
	REQUIRE(ExactColour(wide.ReadPixel(15, 3), blue));

	renderer.SetDrawTarget(square.FramebufferName(), 8, 8, 4, 4);
	REQUIRE(renderer.ClipDepth() == 0);
	const Viewport squareViewport = CurrentViewport();
	REQUIRE(squareViewport.width == 8);
	REQUIRE(squareViewport.height == 8);
	REQUIRE(ExactColour(square.ReadPixel(0, 0), red));
}

TEST_CASE("Renderer state repeated unchanged preparation emits no setup calls") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 8, 8);
	surface->BindDrawTarget();
	renderer.ResetDrawSetupCounts();
	surface->BindDrawTarget();
	surface->BindDrawTarget();
	RequireNoEmittedSetup(renderer.DrawSetupCounts());
	CHECK(renderer.DrawSetupCounts().contextRequested > 0);
	CHECK(renderer.DrawSetupCounts().framebufferRequested > 0);
}

TEST_CASE("Renderer state warm text setup calls do not grow with glyph count") {
	std::shared_ptr<Font> font = LoadFixtureFont();
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 96, 40);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fg(255, 255, 255, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);
	const XYPOSITION ybase = surface->Ascent(font.get());
	const PRectangle rc = PRectangle::FromInts(2, 0, 96, 40);
	surface->DrawTextTransparent(rc, font.get(), ybase, "Hi", fg);
	REQUIRE(HasNonBackgroundInk(surface->Buffer(), bg));

	renderer.ResetDrawSetupCounts();
	renderer.ResetGlyphCounts();
	surface->DrawTextTransparent(rc, font.get(), ybase, "Hi", fg);
	const GlContext::DrawSetupCounts shortCounts = renderer.DrawSetupCounts();
	const size_t shortSubmitted = renderer.GlyphCounts().submitted;
	REQUIRE(shortSubmitted > 0);
	RequireNoEmittedSetup(shortCounts);

	renderer.ResetDrawSetupCounts();
	renderer.ResetGlyphCounts();
	surface->DrawTextTransparent(rc, font.get(), ybase, "HiHiHi", fg);
	const GlContext::DrawSetupCounts longCounts = renderer.DrawSetupCounts();
	REQUIRE(renderer.GlyphCounts().submitted > shortSubmitted);
	RequireNoEmittedSetup(longCounts);
	CHECK(longCounts.contextRequested == shortCounts.contextRequested);
	CHECK(longCounts.framebufferRequested == shortCounts.framebufferRequested);
	CHECK(longCounts.viewportRequested == shortCounts.viewportRequested);
	CHECK(longCounts.scissorRequested == shortCounts.scissorRequested);
	REQUIRE(HasNonBackgroundInk(surface->Buffer(), bg));
}

TEST_CASE("Renderer state nested and empty clips keep pixels") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 10, 10);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA outer(255, 0, 0, 255);
	const ColourRGBA inner(0, 0, 255, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);
	surface->SetClip(PRectangle::FromInts(2, 2, 8, 8));
	surface->FillRectangle(PRectangle::FromInts(0, 0, 10, 10), Fill(outer));
	surface->SetClip(PRectangle::FromInts(4, 4, 6, 6));
	surface->FillRectangle(PRectangle::FromInts(0, 0, 10, 10), Fill(inner));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), bg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(2, 2), outer));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(4, 4), inner));
	surface->PopClip();
	surface->PopClip();
	surface->SetClip(PRectangle::FromInts(9, 9, 9, 9));
	REQUIRE(renderer.CurrentClip().Empty());
	REQUIRE(glIsEnabled(GL_SCISSOR_TEST) == GL_FALSE);
	surface->FillRectangle(PRectangle::FromInts(0, 0, 10, 10), Fill(ColourRGBA(0, 255, 0, 255)));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), bg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(4, 4), inner));
	surface->PopClip();
}

TEST_CASE("Renderer state clear restores clip scissor") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 8, 8);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fill(255, 0, 0, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);
	surface->SetClip(PRectangle::FromInts(2, 2, 6, 6));
	renderer.ResetDrawSetupCounts();
	renderer.Clear(fill);
	CHECK(renderer.DrawSetupCounts().scissorEmitted >= 2);
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), fill));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(7, 7), fill));
	surface->FillRectangle(PRectangle::FromInts(0, 0, 8, 8), Fill(ColourRGBA(0, 0, 255, 255)));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), fill));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(3, 3), ColourRGBA(0, 0, 255, 255)));
}

TEST_CASE("Renderer state sibling pixmap restore rebinds the parent target") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 10, 10);
	std::unique_ptr<Surface> pixmap = surface->AllocatePixMap(4, 4);
	auto *pixSurface = dynamic_cast<DrawSurface *>(pixmap.get());
	REQUIRE(pixSurface != nullptr);
	const ColourRGBA bg(0, 0, 0, 255);
	const ColourRGBA fg(255, 0, 0, 255);
	surface->BindDrawTarget();
	renderer.Clear(bg);
	surface->SetClip(PRectangle::FromInts(2, 2, 8, 8));
	renderer.ResetDrawSetupCounts();
	pixSurface->BindDrawTarget();
	renderer.Clear(ColourRGBA(0, 255, 0, 255));
	CHECK(renderer.DrawSetupCounts().framebufferEmitted >= 1);
	CHECK(DrawFramebufferBinding() ==
		static_cast<GLint>(pixSurface->Buffer().FramebufferName()));

	renderer.ResetDrawSetupCounts();
	surface->BindDrawTarget();
	CHECK(renderer.DrawSetupCounts().framebufferEmitted >= 1);
	CHECK(renderer.DrawSetupCounts().viewportEmitted >= 1);
	REQUIRE(DrawFramebufferBinding() ==
		static_cast<GLint>(surface->Buffer().FramebufferName()));
	surface->FillRectangle(PRectangle::FromInts(0, 0, 10, 10), Fill(fg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(1, 1), bg));
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(2, 2), fg));

	surface->Copy(PRectangle::FromInts(0, 0, 4, 4), Point(0, 0), *pixmap);
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), bg));
	surface->PopClip();
	surface->Copy(PRectangle::FromInts(0, 0, 4, 4), Point(0, 0), *pixmap);
	REQUIRE(ExactColour(surface->Buffer().ReadPixel(0, 0), ColourRGBA(0, 255, 0, 255)));
}

TEST_CASE("Renderer state logical resize does not emit viewport") {
	GlContext context;
	Renderer renderer(context);
	ColourBuffer buffer;
	buffer.Resize(8, 8);
	renderer.SetDrawTarget(buffer.FramebufferName(), 8, 8, 8, 8);
	renderer.ResetDrawSetupCounts();
	renderer.SetDrawTarget(buffer.FramebufferName(), 8, 8, 4, 4);
	CHECK(renderer.DrawSetupCounts().viewportEmitted == 0);
	CHECK(renderer.DrawSetupCounts().framebufferEmitted == 0);
	CHECK(renderer.ClipDepth() == 0);
	const Viewport viewport = CurrentViewport();
	REQUIRE(viewport.width == 8);
	REQUIRE(viewport.height == 8);
}

TEST_CASE("Renderer state physical resize emits viewport") {
	GlContext context;
	Renderer renderer(context);
	ColourBuffer first;
	ColourBuffer second;
	first.Resize(8, 8);
	second.Resize(16, 4);
	renderer.SetDrawTarget(first.FramebufferName(), 8, 8);
	renderer.ResetDrawSetupCounts();
	renderer.SetDrawTarget(second.FramebufferName(), 16, 4);
	CHECK(renderer.DrawSetupCounts().viewportEmitted == 1);
	CHECK(renderer.DrawSetupCounts().framebufferEmitted == 1);
	const Viewport viewport = CurrentViewport();
	REQUIRE(viewport.width == 16);
	REQUIRE(viewport.height == 4);
}

TEST_CASE("Renderer state clip-only change emits scissor not viewport") {
	GlContext context;
	Renderer renderer(context);
	std::unique_ptr<DrawSurface> surface = CreateDrawSurface(renderer, 8, 8);
	surface->BindDrawTarget();
	renderer.ResetDrawSetupCounts();
	surface->SetClip(PRectangle::FromInts(1, 1, 4, 4));
	CHECK(renderer.DrawSetupCounts().scissorEmitted >= 1);
	CHECK(renderer.DrawSetupCounts().viewportEmitted == 0);
	CHECK(renderer.DrawSetupCounts().framebufferEmitted == 0);
	surface->PopClip();
}

TEST_CASE("Renderer state fractional scale preparation is stable") {
	GlContext context;
	Renderer renderer(context);
	const RasterScale scale = RasterScale::FromWaylandNumerator(150);
	ColourBuffer buffer;
	buffer.Resize(10, 10);
	std::unique_ptr<DrawSurface> surface = CreateExternalDrawSurface(
		renderer, buffer.FramebufferName(), 10, 10, 8, 8, scale);
	REQUIRE(renderer.TargetRasterScale() == scale);
	surface->BindDrawTarget();
	renderer.ResetDrawSetupCounts();
	surface->BindDrawTarget();
	RequireNoEmittedSetup(renderer.DrawSetupCounts());
	REQUIRE(renderer.TargetWidth() == 10);
	REQUIRE(renderer.TargetLogicalWidth() == 8);
}

TEST_CASE("Renderer state framebuffer 0 alternates equal and unequal sizes") {
	GlContext context;
	Renderer editor(context);
	Renderer popup(context);
	editor.SetDrawTarget(0, 8, 8);
	editor.SetClip(PRectangle::FromInts(1, 1, 7, 7));
	REQUIRE(DrawFramebufferBinding() == 0);
	REQUIRE(ReadFramebufferBinding() == 0);
	Viewport editorViewport = CurrentViewport();
	REQUIRE(editorViewport.width == 8);
	REQUIRE(editorViewport.height == 8);
	REQUIRE(editor.ClipDepth() == 1);

	popup.SetDrawTarget(0, 8, 8);
	REQUIRE(DrawFramebufferBinding() == 0);
	REQUIRE(popup.ClipDepth() == 0);
	REQUIRE(CurrentViewport().width == 8);
	REQUIRE(CurrentViewport().height == 8);
	popup.SetClip(PRectangle::FromInts(2, 2, 4, 4));
	REQUIRE(popup.ClipDepth() == 1);

	editor.BindCurrentTarget();
	REQUIRE(DrawFramebufferBinding() == 0);
	REQUIRE(editor.ClipDepth() == 1);
	editorViewport = CurrentViewport();
	REQUIRE(editorViewport.width == 8);
	REQUIRE(editorViewport.height == 8);

	popup.SetDrawTarget(0, 16, 4);
	REQUIRE(popup.ClipDepth() == 0);
	const Viewport popupViewport = CurrentViewport();
	REQUIRE(popupViewport.width == 16);
	REQUIRE(popupViewport.height == 4);

	editor.BindCurrentTarget();
	editorViewport = CurrentViewport();
	REQUIRE(editorViewport.width == 8);
	REQUIRE(editorViewport.height == 8);
	REQUIRE(editor.ClipDepth() == 1);
}

TEST_CASE("Renderer state framebuffer 0 recovers after popup recreation and release") {
	GlContext context;
	Renderer editor(context);
	editor.SetDrawTarget(0, 10, 6);
	editor.SetClip(PRectangle::FromInts(0, 0, 5, 6));
	{
		Renderer popup(context);
		popup.SetDrawTarget(0, 4, 4);
		REQUIRE(CurrentViewport().width == 4);
		REQUIRE(CurrentViewport().height == 4);
	}
	editor.BindCurrentTarget();
	REQUIRE(DrawFramebufferBinding() == 0);
	REQUIRE(CurrentViewport().width == 10);
	REQUIRE(CurrentViewport().height == 6);
	REQUIRE(editor.ClipDepth() == 1);

	{
		Renderer popup(context);
		popup.SetDrawTarget(0, 12, 8);
		REQUIRE(CurrentViewport().width == 12);
		REQUIRE(CurrentViewport().height == 8);
	}
	context.ReleaseCurrent();
	REQUIRE_FALSE(context.SurfacesCurrent(GlContext::SurfaceTarget::Editor));
	editor.BindCurrentTarget();
	REQUIRE(context.SurfacesCurrent(GlContext::SurfaceTarget::Editor));
	REQUIRE(DrawFramebufferBinding() == 0);
	REQUIRE(CurrentViewport().width == 10);
	REQUIRE(CurrentViewport().height == 6);
	REQUIRE(editor.ClipDepth() == 1);
}

TEST_CASE("Renderer state pixmap bind keeps output raster scale") {
	GlContext context;
	Renderer renderer(context);
	const RasterScale scale = RasterScale::FromWaylandNumerator(150);
	ColourBuffer buffer;
	buffer.Resize(10, 10);
	std::unique_ptr<DrawSurface> surface = CreateExternalDrawSurface(
		renderer, buffer.FramebufferName(), 10, 10, 8, 8, scale);
	std::shared_ptr<Font> font = LoadFixtureFont();
	surface->BindDrawTarget();
	renderer.Clear(ColourRGBA(0, 0, 0, 255));
	surface->DrawTextTransparent(PRectangle::FromInts(0, 0, 8, 8), font.get(),
		surface->Ascent(font.get()), "A", ColourRGBA(255, 255, 255, 255));
	const size_t cacheSize = renderer.GlyphCacheSize();
	REQUIRE(cacheSize > 0);

	std::unique_ptr<Surface> pixmap = surface->AllocatePixMap(4, 4);
	auto *pixSurface = dynamic_cast<DrawSurface *>(pixmap.get());
	REQUIRE(pixSurface != nullptr);
	pixSurface->BindDrawTarget();
	REQUIRE(renderer.TargetRasterScale() == scale);
	REQUIRE(renderer.GlyphCacheSize() == cacheSize);
	surface->BindDrawTarget();
	REQUIRE(renderer.TargetRasterScale() == scale);
	REQUIRE(renderer.GlyphCacheSize() == cacheSize);
}
