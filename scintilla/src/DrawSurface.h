// scalpel-editor concrete Surface: shaped-run measure plus GL drawing.
//
// Measurement, Layout, and DrawText* all consume the shaped-run path (same
// helpers as MeasureSurface). Glyphs are rasterized and cached on Renderer.
//
// Surface::Allocate returns a measure-only DrawSurface (no GL buffer). Painting
// targets are built with CreateDrawSurface against a Renderer and ColourBuffer.

#ifndef DRAWSURFACE_H
#define DRAWSURFACE_H

#include <memory>
#include <vector>

#include "FontPlatform.h"
#include "Platform.h"
#include "Renderer.h"
#include "ShapedRun.h"

namespace Scintilla::Internal {

/**
 * One Surface implementation for measure and draw.
 *
 * Drawing surfaces hold a ColourBuffer in the parent Renderer context. Measure-
 * only surfaces have no buffer and still answer MeasureWidths, WidthText,
 * Layout, and font metrics; DrawText* is a no-op without a Renderer. Pixmaps
 * share the parent Renderer (same GL context).
 */
class DrawSurface final : public Surface {
public:
	/**
	 * @param renderer_ Required for drawing and pixmaps; may be null only for
	 *        measure-only surfaces that never draw or allocate pixmaps.
	 */
	explicit DrawSurface(Renderer *renderer_ = nullptr,
		std::vector<std::shared_ptr<FontFace>> fallbacks = {});

	~DrawSurface() override;

	void SetFallbacks(std::vector<std::shared_ptr<FontFace>> fallbacks);
	[[nodiscard]] ShapedRunCache &RunCache() noexcept { return runCache; }
	[[nodiscard]] const ShapedRunCache &RunCache() const noexcept { return runCache; }
	[[nodiscard]] Renderer *GetRenderer() const noexcept { return renderer; }
	[[nodiscard]] ColourBuffer &Buffer() noexcept { return buffer; }
	[[nodiscard]] const ColourBuffer &Buffer() const noexcept { return buffer; }

	/** Bind this surface's colour buffer as the renderer draw target. */
	void BindDrawTarget();

	void Init(WindowID wid) override;
	void Init(SurfaceID sid, WindowID wid) override;
	std::unique_ptr<Surface> AllocatePixMap(int width, int height) override;
	void SetMode(SurfaceMode mode) override;
	void Release() noexcept override;
	int SupportsFeature(Scintilla::Supports feature) noexcept override;
	bool Initialised() override;
	int LogPixelsY() override;
	int PixelDivisions() override;
	int DeviceHeightFont(int points) override;

	void LineDraw(Point start, Point end, Stroke stroke) override;
	void PolyLine(const Point *pts, size_t npts, Stroke stroke) override;
	void Polygon(const Point *pts, size_t npts, FillStroke fillStroke) override;
	void RectangleDraw(PRectangle rc, FillStroke fillStroke) override;
	void RectangleFrame(PRectangle rc, Stroke stroke) override;
	void FillRectangle(PRectangle rc, Fill fill) override;
	void FillRectangleAligned(PRectangle rc, Fill fill) override;
	void FillRectangle(PRectangle rc, Surface &surfacePattern) override;
	void RoundedRectangle(PRectangle rc, FillStroke fillStroke) override;
	void AlphaRectangle(PRectangle rc, XYPOSITION cornerSize, FillStroke fillStroke) override;
	void GradientRectangle(PRectangle rc, const std::vector<ColourStop> &stops, GradientOptions options) override;
	void DrawRGBAImage(PRectangle rc, int width, int height, const unsigned char *pixelsImage) override;
	void Ellipse(PRectangle rc, FillStroke fillStroke) override;
	void Stadium(PRectangle rc, FillStroke fillStroke, Ends ends) override;
	void Copy(PRectangle rc, Point from, Surface &surfaceSource) override;

	std::unique_ptr<IScreenLineLayout> Layout(const IScreenLine *screenLine) override;
	void DrawTextNoClip(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text,
		ColourRGBA fore, ColourRGBA back) override;
	void DrawTextClipped(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text,
		ColourRGBA fore, ColourRGBA back) override;
	void DrawTextTransparent(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text,
		ColourRGBA fore) override;
	void MeasureWidths(const Font *font_, std::string_view text, XYPOSITION *positions) override;
	XYPOSITION WidthText(const Font *font_, std::string_view text) override;
	XYPOSITION Ascent(const Font *font_) override;
	XYPOSITION Descent(const Font *font_) override;
	XYPOSITION InternalLeading(const Font *font_) override;
	XYPOSITION Height(const Font *font_) override;
	XYPOSITION AverageCharWidth(const Font *font_) override;
	void SetClip(PRectangle rc) override;
	void PopClip() override;
	void FlushCachedState() override;
	void FlushDrawing() override;

private:
	std::shared_ptr<FontFace> RequireFace(const Font *font_) const;
	FontMetrics MetricsOf(const Font *font_) const;
	void EnsureRenderer() const;
	void DrawTextCommon(PRectangle rc, const Font *font_, XYPOSITION ybase, std::string_view text,
		ColourRGBA fore, bool fillBack, ColourRGBA back, bool clipToRc);

	Renderer *renderer = nullptr;
	ColourBuffer buffer;
	bool initialised = false;
	SurfaceMode mode{};
	ShapedRunCache runCache;
	std::vector<std::shared_ptr<FontFace>> fallbacks;
	std::vector<PRectangle> clipStack;
};

/** Drawing surface with an offscreen colour buffer of the given size. */
std::unique_ptr<DrawSurface> CreateDrawSurface(Renderer &renderer, int width, int height,
	std::vector<std::shared_ptr<FontFace>> fallbacks = {});

/** Measure-only surface (no GL buffer). Same measure path as drawing surfaces. */
std::unique_ptr<DrawSurface> CreateMeasureOnlySurface(
	std::vector<std::shared_ptr<FontFace>> fallbacks = {});

}

#endif
