// scalpel-editor OpenGL drawing implementation (one path for offscreen and window).
//
// Pixel contract (locked for step 5 tests and later paint):
// - Colour attachments are GL_RGBA8, linear (framebuffer sRGB disabled).
// - GL colour attachments store premultiplied RGBA8 for correct source-over.
// - Public pixel buffers are top-to-bottom RGBA8 converted back to straight alpha.
// - glReadPixels is bottom-to-top; ReadPixelsTopDown flips rows.
// - PRectangle includes left/top and excludes right/bottom (see Geometry.h).
// - Dithering and multisampling are disabled on the owning GlContext.
// - Blend uses premultiplied source-over: ONE / ONE_MINUS_SRC_ALPHA for RGBA.
// - Exact pixel equality for clears and solid opaque interiors; ±1 per channel
//   for alpha blends and gradients.
// - Surface coordinates: origin top-left, y increases downward. The orthographic
//   transform maps that space into OpenGL NDC for triangle draws.

#ifndef RENDERER_H
#define RENDERER_H

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "FontPlatform.h"
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
	/**
	 * Select a buffer-sized target whose drawing coordinates use a distinct
	 * logical size.
	 */
	void SetDrawTarget(unsigned framebuffer, int bufferWidth, int bufferHeight,
		int logicalWidth, int logicalHeight);

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

	/** Stroke a line segment (surface coords). Width comes from stroke. */
	void LineDraw(Point start, Point end, Stroke stroke);

	/** Polyline through pts[0..npts). */
	void PolyLine(const Point *pts, size_t npts, Stroke stroke);

	/** Filled polygon with optional stroke (triangle fan; npts >= 3). */
	void Polygon(const Point *pts, size_t npts, FillStroke fillStroke);

	/** Fill then stroke an axis-aligned rectangle. */
	void RectangleDraw(PRectangle rc, FillStroke fillStroke);

	/** Stroke only the rectangle frame (inset by half stroke where useful). */
	void RectangleFrame(PRectangle rc, Stroke stroke);

	/** Filled/stroked rounded rectangle (corner radius in surface units). */
	void RoundedRectangle(PRectangle rc, FillStroke fillStroke, XYPOSITION radius);

	/** Translucent filled rect with optional rounded corners and stroke. */
	void AlphaRectangle(PRectangle rc, XYPOSITION cornerSize, FillStroke fillStroke);

	/** Filled/stroked ellipse inscribed in rc. */
	void Ellipse(PRectangle rc, FillStroke fillStroke);

	/**
	 * Stadium (capsule): rectangle with semicircular or flat/angled ends.
	 * ends encodes Surface::Ends flags.
	 */
	void Stadium(PRectangle rc, FillStroke fillStroke, int ends);

	/**
	 * Linear gradient fill. Stops are sorted by position in [0,1].
	 * Interpolation is linear between adjacent stops (not smoothstep).
	 * options: leftToRight or topToBottom.
	 */
	void GradientRectangle(PRectangle rc, const std::vector<ColourStop> &stops, int options);

	/** Draw a top-down RGBA8 image into rc (may scale). */
	void DrawRGBAImage(PRectangle rc, int width, int height, const unsigned char *pixels);

	/**
	 * Rasterize (or reuse a cached texture for) one FreeType glyph and draw it.
	 *
	 * penX/penY is the baseline origin in surface coordinates (y down). FreeType
	 * bearings place the bitmap: dest left = penX + left, dest top = penY - top.
	 * fore modulates coverage as straight alpha. Empty bitmaps are a no-op after
	 * the first miss is cached. Glyph textures stay alive until this Renderer is
	 * destroyed. Cache entries retain their face, and face identity is part of
	 * the key.
	 */
	void DrawGlyph(XYPOSITION penX, XYPOSITION penY, const std::shared_ptr<FontFace> &face,
		uint32_t glyphId, ColourRGBA fore);

	/** Number of face+glyphId entries in the glyph texture cache. */
	[[nodiscard]] size_t GlyphCacheSize() const noexcept { return glyphCache.size(); }

	/**
	 * Copy a rectangle from a colour buffer texture into the current target.
	 * from is the top-left of the source region in source pixel space.
	 * rc is the destination rectangle in the current target.
	 */
	void Copy(PRectangle rc, Point from, const ColourBuffer &source);

	/**
	 * Tile source texture across rc (pattern fill). Source must be non-empty.
	 */
	void FillRectanglePattern(PRectangle rc, const ColourBuffer &pattern);

	[[nodiscard]] int TargetWidth() const noexcept { return targetWidth; }
	[[nodiscard]] int TargetHeight() const noexcept { return targetHeight; }
	[[nodiscard]] int TargetLogicalWidth() const noexcept { return targetLogicalWidth; }
	[[nodiscard]] int TargetLogicalHeight() const noexcept { return targetLogicalHeight; }
	[[nodiscard]] unsigned TargetFramebuffer() const noexcept { return targetFbo; }
	[[nodiscard]] size_t ClipDepth() const noexcept { return clipStack.size(); }

	/** GradientOptions values mirrored here to avoid pulling Platform into every caller. */
	static constexpr int kGradientLeftToRight = 0;
	static constexpr int kGradientTopToBottom = 1;

private:
	struct GlyphKey {
		std::shared_ptr<const FontFace> face;
		uint32_t glyphId = 0;

		bool operator==(const GlyphKey &other) const noexcept {
			return face == other.face && glyphId == other.glyphId;
		}
	};

	struct GlyphKeyHash {
		size_t operator()(const GlyphKey &key) const noexcept {
			return std::hash<std::shared_ptr<const FontFace>>{}(key.face) ^
				(std::hash<uint32_t>{}(key.glyphId) * 0x9e3779b9u);
		}
	};

	struct CachedGlyph {
		unsigned texture = 0;
		int width = 0;
		int height = 0;
		int left = 0;
		int top = 0;
	};

	void DestroyGl() noexcept;
	void ClearGlyphCache() noexcept;
	const CachedGlyph &GetOrCreateGlyph(const std::shared_ptr<FontFace> &face, uint32_t glyphId);
	void EnsureSolidProgram();
	void EnsureTextureProgram();
	void EnsureGradientProgram();
	void ApplyScissor() const;
	void UploadProjection() const;
	void DrawSolidQuad(float x0, float y0, float x1, float y1, ColourRGBA colour);
	void DrawSolidTriangles(const float *xy, size_t vertexCount, ColourRGBA colour);
	void DrawLineSegment(Point start, Point end, XYPOSITION width, ColourRGBA colour);
	void DrawEllipse(PRectangle rc, ColourRGBA fill, ColourRGBA stroke, XYPOSITION strokeWidth, bool doFill, bool doStroke);
	void DrawTexturedQuad(float x0, float y0, float x1, float y1,
		float u0, float v0, float u1, float v1, unsigned texture, bool flipV,
		bool sourceStraightAlpha, ColourRGBA modulate = ColourRGBA(255, 255, 255, 255));
	[[nodiscard]] PixelRect CurrentClip() const noexcept;
	[[nodiscard]] PixelRect LogicalPixelRect(PRectangle rc) const noexcept;
	void BeginDraw();
	void SetBlendForColour(ColourRGBA colour);

	GlContext &context;
	unsigned targetFbo = 0;
	int targetWidth = 0;
	int targetHeight = 0;
	int targetLogicalWidth = 0;
	int targetLogicalHeight = 0;

	std::vector<PixelRect> clipStack;
	std::unordered_map<GlyphKey, CachedGlyph, GlyphKeyHash> glyphCache;

	unsigned programSolid = 0;
	unsigned programTexture = 0;
	unsigned programGradient = 0;
	unsigned vao = 0;
	unsigned vbo = 0;
	int uniformTransform = -1;
	int uniformColour = -1;
	int uniformTexTransform = -1;
	int uniformTexSampler = -1;
	int uniformTexStraightAlpha = -1;
	int uniformTexModulate = -1;
	int uniformGradTransform = -1;
	int uniformGradStart = -1;
	int uniformGradEnd = -1;
	int uniformGradStopCount = -1;
	int uniformGradStops = -1;
	int uniformGradColours = -1;
};

}

#endif
