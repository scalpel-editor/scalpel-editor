// scalpel-editor measure-only surface: widths and screen-line layout from shaped runs.

#ifndef MEASURESURFACE_H
#define MEASURESURFACE_H

#include <memory>
#include <vector>

#include "FontPlatform.h"
#include "Platform.h"
#include "ShapedRun.h"

namespace Scintilla::Internal {

/**
 * Surface that implements measurement and screen-line layout only.
 *
 * MeasureWidths, WidthText, and Layout consume ShapeText (via an internal
 * ShapedRunCache). Font metrics come from FontFace. Drawing methods are
 * no-ops so layout tests can run before the renderer exists. Step 5 should
 * fold this measure path into the real surface rather than re-measure text.
 *
 * Fonts must be platform faces (Font::Allocate or FontFromFace). Optional
 * fallbacks are used when shaping spans for Layout and MeasureWidths.
 */
class MeasureSurface final : public Surface {
public:
	explicit MeasureSurface(std::vector<std::shared_ptr<FontFace>> fallbacks = {});

	void SetFallbacks(std::vector<std::shared_ptr<FontFace>> fallbacks);
	[[nodiscard]] ShapedRunCache &RunCache() noexcept { return runCache; }
	[[nodiscard]] const ShapedRunCache &RunCache() const noexcept { return runCache; }

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

	bool initialised = false;
	SurfaceMode mode{};
	ShapedRunCache runCache;
	std::vector<std::shared_ptr<FontFace>> fallbacks;
};

std::unique_ptr<Surface> CreateMeasureSurface(
	std::vector<std::shared_ptr<FontFace>> fallbacks = {});

}

#endif
