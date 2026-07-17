// scalpel-editor OpenGL drawing implementation (one path for offscreen and window).
//
// Pixel contract (locked for step 5 tests and later paint):
// - Colour attachments are GL_RGBA8, linear (framebuffer sRGB disabled).
// - Public pixel buffers are top-to-bottom RGBA8 with straight alpha.
// - glReadPixels is bottom-to-top; ReadPixelsTopDown flips rows.
// - PRectangle includes left/top and excludes right/bottom (see Geometry.h).
// - Dithering and multisampling are disabled on the owning GlContext.
// - Blend: GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA for RGB and alpha.
// - Exact pixel equality for clears and solid opaque interiors; ±1 per channel
//   for alpha blends and gradients.
// - Surface coordinates: origin top-left, y increases downward. The orthographic
//   transform maps that space into OpenGL NDC for triangle draws.

#ifndef RENDERER_H
#define RENDERER_H

#include <cstdint>
#include <vector>

#include "Geometry.h"
#include "GlContext.h"

namespace Scintilla::Internal {

/**
 * Texture-backed colour attachment in the current GlContext.
 *
 * Pixmap surfaces and the main offscreen target each own one of these in the
 * parent context. Destroy while the context is current.
 */
class ColourBuffer {
public:
	ColourBuffer() = default;
	~ColourBuffer() noexcept;

	ColourBuffer(const ColourBuffer &) = delete;
	ColourBuffer(ColourBuffer &&) = delete;
	ColourBuffer &operator=(const ColourBuffer &) = delete;
	ColourBuffer &operator=(ColourBuffer &&) = delete;

	/** Allocate or replace the GL_RGBA8 texture and FBO. Context must be current. */
	void Resize(int width, int height);

	/** Delete GL objects. Context must be current if objects exist. */
	void Destroy() noexcept;

	[[nodiscard]] bool Valid() const noexcept { return fbo != 0; }
	[[nodiscard]] int Width() const noexcept { return width; }
	[[nodiscard]] int Height() const noexcept { return height; }
	[[nodiscard]] unsigned FramebufferName() const noexcept { return fbo; }
	[[nodiscard]] unsigned TextureName() const noexcept { return texture; }

	/**
	 * Read the full buffer as top-to-bottom RGBA8 (straight alpha).
	 * Context must be current. Size is width * height * 4.
	 */
	[[nodiscard]] std::vector<uint8_t> ReadPixelsTopDown() const;

	/** Sample one pixel in top-down coordinates (0,0) = top-left. */
	[[nodiscard]] ColourRGBA ReadPixel(int x, int y) const;

private:
	unsigned texture = 0;
	unsigned fbo = 0;
	int width = 0;
	int height = 0;
};

/** Integer half-open rectangle in top-down pixel space (matches PRectangle). */
struct PixelRect {
	int left = 0;
	int top = 0;
	int right = 0;
	int bottom = 0;

	[[nodiscard]] bool Empty() const noexcept {
		return right <= left || bottom <= top;
	}

	[[nodiscard]] int Width() const noexcept { return right - left; }
	[[nodiscard]] int Height() const noexcept { return bottom - top; }
};

/** Convert a PRectangle to a pixel rect (floor left/top, ceil right/bottom). */
[[nodiscard]] PixelRect PixelRectFromPRectangle(PRectangle rc) noexcept;

/** Intersect two half-open pixel rects; empty if no overlap. */
[[nodiscard]] PixelRect IntersectPixelRect(PixelRect a, PixelRect b) noexcept;

/**
 * Per-context drawing state. Window and offscreen hosts each construct their
 * own Renderer on their own GlContext; they share this implementation, not GL
 * object names.
 */
class Renderer {
public:
	explicit Renderer(GlContext &context);
	~Renderer() noexcept;

	Renderer(const Renderer &) = delete;
	Renderer(Renderer &&) = delete;
	Renderer &operator=(const Renderer &) = delete;
	Renderer &operator=(Renderer &&) = delete;

	[[nodiscard]] GlContext &Context() noexcept { return context; }
	[[nodiscard]] const GlContext &Context() const noexcept { return context; }

	/** Make the owning context current. */
	void MakeCurrent();

	/**
	 * Select the draw target. Pass framebuffer 0 only for a window default
	 * framebuffer (step 9+). Offscreen uses a ColourBuffer FBO name.
	 * Resets the clip stack when the target identity or size changes.
	 */
	void SetDrawTarget(unsigned framebuffer, int width, int height);

	/** Re-bind the current target FBO and viewport without clearing clips. */
	void BindCurrentTarget();

	/** Clear the current draw target fully (ignores clip stack). */
	void Clear(ColourRGBA colour);

	/**
	 * Push a clip rectangle (surface coords, top-left origin). Intersects with
	 * the current clip (or the full target). Empty intersection disables drawing
	 * until PopClip.
	 */
	void SetClip(PRectangle rc);

	/** Restore the previous clip; no-op if the stack is empty. */
	void PopClip();

	/** Opaque axis-aligned fill of a half-open rectangle (scissor + clear). */
	void FillRectangleOpaque(PRectangle rc, ColourRGBA colour);

	/**
	 * Axis-aligned fill with blending when alpha < 255. Uses the solid-colour
	 * program and the orthographic transform.
	 */
	void FillRectangle(PRectangle rc, ColourRGBA colour);

	[[nodiscard]] int TargetWidth() const noexcept { return targetWidth; }
	[[nodiscard]] int TargetHeight() const noexcept { return targetHeight; }
	[[nodiscard]] size_t ClipDepth() const noexcept { return clipStack.size(); }

private:
	void DestroyGl() noexcept;
	void EnsureSolidProgram();
	void ApplyScissor() const;
	void UploadProjection() const;
	void DrawSolidQuad(float x0, float y0, float x1, float y1, ColourRGBA colour);
	[[nodiscard]] PixelRect CurrentClip() const noexcept;

	GlContext &context;
	unsigned targetFbo = 0;
	int targetWidth = 0;
	int targetHeight = 0;

	std::vector<PixelRect> clipStack;

	unsigned programSolid = 0;
	unsigned vao = 0;
	unsigned vbo = 0;
	int uniformTransform = -1;
	int uniformColour = -1;
};

}

#endif
