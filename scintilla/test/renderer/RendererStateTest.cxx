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
