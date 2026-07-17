// scalpel-editor OpenGL drawing implementation (one path for offscreen and window).
//
// Pixel contract (locked for step 5 tests and later paint):
// - Colour attachments are GL_RGBA8, linear (framebuffer sRGB disabled).
// - Public pixel buffers are top-to-bottom RGBA8 with straight alpha.
// - glReadPixels is bottom-to-top; ReadPixelsTopDown flips rows.
// - PRectangle includes left/top and excludes right/bottom (see Geometry.h).
// - Dithering and multisampling are disabled on the owning GlContext.
// - Blend (when enabled later): GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA for RGB
//   and the same for alpha so destination alpha accumulates in the usual way.
// - Exact pixel equality for clears and solid opaque interiors; ±1 per channel
//   for alpha blends and gradients.

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
	 * Bind a draw target for subsequent Clear and geometry. Pass framebuffer 0
	 * only for a window default framebuffer (step 9+). Offscreen uses a
	 * ColourBuffer FBO name.
	 */
	void SetDrawTarget(unsigned framebuffer, int width, int height);

	/** Clear the current draw target to colour (straight alpha, premultiplied not used). */
	void Clear(ColourRGBA colour);

	[[nodiscard]] int TargetWidth() const noexcept { return targetWidth; }
	[[nodiscard]] int TargetHeight() const noexcept { return targetHeight; }

private:
	GlContext &context;
	unsigned targetFbo = 0;
	int targetWidth = 0;
	int targetHeight = 0;
};

}

#endif
