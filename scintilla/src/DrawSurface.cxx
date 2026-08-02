// scalpel-editor concrete Surface: shaped-run measure plus GL drawing.

#include "DrawSurface.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "EditorStyleTypes.h"
#include "Geometry.h"
#include "ShapedLayout.h"
#include "ShapedRun.h"

namespace Scintilla::Internal {

DrawSurface::DrawSurface(Renderer *renderer_, FontFallback fallback_) :
	renderer(renderer_),
	fallback(std::move(fallback_)) {
}

DrawSurface::~DrawSurface() {
	if (buffer.Valid() && renderer) {
		renderer->MakeCurrent();
		buffer.Destroy();
	}
}

void DrawSurface::SetFallbacks(FontFallback fallback_) {
	fallback = std::move(fallback_);
	runCache.Clear();
}

void DrawSurface::EnsureRenderer() const {
	if (!renderer) {
		throw std::runtime_error("DrawSurface operation requires a Renderer");
	}
}

void DrawSurface::BindDrawTarget() {
	EnsureRenderer();
	const unsigned framebuffer = buffer.Valid() ? buffer.FramebufferName() : externalFramebuffer;
	const int width = buffer.Valid() ? buffer.Width() : externalWidth;
	const int height = buffer.Valid() ? buffer.Height() : externalHeight;
	const int logicalWidth = buffer.Valid() ? width : externalLogicalWidth;
	const int logicalHeight = buffer.Valid() ? height : externalLogicalHeight;
	if (!buffer.Valid() && !hasExternalTarget) {
		throw std::runtime_error("DrawSurface::BindDrawTarget without a framebuffer");
	}
	const bool targetChanged = renderer->TargetFramebuffer() != framebuffer ||
		renderer->TargetWidth() != width || renderer->TargetHeight() != height ||
		renderer->TargetLogicalWidth() != logicalWidth ||
		renderer->TargetLogicalHeight() != logicalHeight;
	// Only the frame/window surface (external target) pushes output scale.
	// Owned colour buffers and pixmaps must not change it mid-paint.
	if (hasExternalTarget) {
		renderer->SetOutputRasterScale(rasterScale);
	}
	renderer->SetDrawTarget(
		framebuffer, width, height, logicalWidth, logicalHeight);
	if (targetChanged) {
		for (const PRectangle rc : clipStack) {
			renderer->SetClip(rc);
		}
	}
	renderer->BindCurrentTarget();
}

void DrawSurface::SetExternalDrawTarget(unsigned framebuffer,
	int bufferWidth, int bufferHeight, int logicalWidth, int logicalHeight,
	RasterScale surfaceRasterScale) {
	if (bufferWidth <= 0 || bufferHeight <= 0 ||
		logicalWidth <= 0 || logicalHeight <= 0) {
		throw std::invalid_argument("external draw target requires a positive size");
	}
	externalFramebuffer = framebuffer;
	externalWidth = bufferWidth;
	externalHeight = bufferHeight;
	externalLogicalWidth = logicalWidth;
	externalLogicalHeight = logicalHeight;
	hasExternalTarget = true;
	rasterScale = surfaceRasterScale;
}

void DrawSurface::Init(WindowID) {
	initialised = true;
}

void DrawSurface::Init(SurfaceID, WindowID) {
	initialised = true;
}

std::unique_ptr<Surface> DrawSurface::AllocatePixMap(int width, int height) {
	// Step 5 later commits fill the pixmap buffer; for now allocate a sibling
	// surface in the same context so call sites do not need a separate type.
	EnsureRenderer();
	auto pix = std::make_unique<DrawSurface>(renderer, fallback);
	pix->mode = mode;
	pix->initialised = true;
	if (width > 0 && height > 0) {
		renderer->MakeCurrent();
		pix->buffer.Resize(width, height);
	}
	return pix;
}

void DrawSurface::SetMode(SurfaceMode mode_) {
	mode = mode_;
}

void DrawSurface::Release() noexcept {
	if (buffer.Valid() && renderer) {
		try {
			renderer->MakeCurrent();
			buffer.Destroy();
		} catch (...) {
			// Best-effort teardown.
		}
	}
	initialised = false;
	clipStack.clear();
	hasExternalTarget = false;
	externalFramebuffer = 0;
	externalWidth = 0;
	externalHeight = 0;
	externalLogicalWidth = 0;
	externalLogicalHeight = 0;
	rasterScale = {};
}

int DrawSurface::SupportsFeature(Scintilla::Supports feature) noexcept {
	// Measurement is single-threaded through the shaped-run cache.
	// LineDraw will draw the final pixel once geometry is implemented.
	switch (feature) {
	case Scintilla::Supports::LineDrawsFinal:
		return 1;
	case Scintilla::Supports::PixelDivisions:
		return 1;
	case Scintilla::Supports::FractionalStrokeWidth:
		return 0;
	case Scintilla::Supports::TranslucentStroke:
		return 1;
	case Scintilla::Supports::PixelModification:
		return 0;
	case Scintilla::Supports::ThreadSafeMeasureWidths:
		return 0;
	}
	return 0;
}

bool DrawSurface::Initialised() {
	return initialised;
}

int DrawSurface::LogPixelsY() {
	return 96;
}

int DrawSurface::PixelDivisions() {
	return 1;
}

int DrawSurface::DeviceHeightFont(int points) {
	return (points * LogPixelsY() + 36) / 72;
}

void DrawSurface::LineDraw(Point start, Point end, Stroke stroke) {
	EnsureRenderer();
	BindDrawTarget();
	renderer->LineDraw(start, end, stroke);
}

void DrawSurface::PolyLine(const Point *pts, size_t npts, Stroke stroke) {
	EnsureRenderer();
	BindDrawTarget();
	renderer->PolyLine(pts, npts, stroke);
}

void DrawSurface::Polygon(const Point *pts, size_t npts, FillStroke fillStroke) {
	EnsureRenderer();
	BindDrawTarget();
	renderer->Polygon(pts, npts, fillStroke);
}

void DrawSurface::RectangleDraw(PRectangle rc, FillStroke fillStroke) {
	EnsureRenderer();
	BindDrawTarget();
	renderer->RectangleDraw(rc, fillStroke);
}

void DrawSurface::RectangleFrame(PRectangle rc, Stroke stroke) {
	EnsureRenderer();
	BindDrawTarget();
	renderer->RectangleFrame(rc, stroke);
}

void DrawSurface::FillRectangle(PRectangle rc, Fill fill) {
	EnsureRenderer();
	BindDrawTarget();
	renderer->FillRectangle(rc, fill.colour);
}

void DrawSurface::FillRectangleAligned(PRectangle rc, Fill fill) {
	FillRectangle(PixelAlign(rc, PixelDivisions()), fill);
}

void DrawSurface::FillRectangle(PRectangle rc, Surface &surfacePattern) {
	EnsureRenderer();
	auto *pattern = dynamic_cast<DrawSurface *>(&surfacePattern);
	if (!pattern || !pattern->Buffer().Valid()) {
		return;
	}
	BindDrawTarget();
	renderer->FillRectanglePattern(rc, pattern->Buffer());
}

void DrawSurface::RoundedRectangle(PRectangle rc, FillStroke fillStroke) {
	EnsureRenderer();
	BindDrawTarget();
	const XYPOSITION radius = std::min(rc.Width(), rc.Height()) * 0.25f;
	renderer->RoundedRectangle(rc, fillStroke, radius);
}

void DrawSurface::AlphaRectangle(PRectangle rc, XYPOSITION cornerSize, FillStroke fillStroke) {
	EnsureRenderer();
	BindDrawTarget();
	renderer->AlphaRectangle(rc, cornerSize, fillStroke);
}

void DrawSurface::GradientRectangle(PRectangle rc, const std::vector<ColourStop> &stops,
	GradientOptions options) {
	EnsureRenderer();
	BindDrawTarget();
	const int opt = (options == GradientOptions::topToBottom)
		? Renderer::kGradientTopToBottom
		: Renderer::kGradientLeftToRight;
	renderer->GradientRectangle(rc, stops, opt);
}

void DrawSurface::DrawRGBAImage(PRectangle rc, int width, int height, const unsigned char *pixelsImage) {
	EnsureRenderer();
	BindDrawTarget();
	if (!pixelsImage || width <= 0 || height <= 0) {
		return;
	}
	renderer->DrawRGBAImage(rc, width, height, pixelsImage);
}

void DrawSurface::Ellipse(PRectangle rc, FillStroke fillStroke) {
	EnsureRenderer();
	BindDrawTarget();
	renderer->Ellipse(rc, fillStroke);
}

void DrawSurface::Stadium(PRectangle rc, FillStroke fillStroke, Ends ends) {
	EnsureRenderer();
	BindDrawTarget();
	renderer->Stadium(rc, fillStroke, static_cast<int>(ends));
}

void DrawSurface::Copy(PRectangle rc, Point from, Surface &surfaceSource) {
	EnsureRenderer();
	auto *source = dynamic_cast<DrawSurface *>(&surfaceSource);
	if (!source || !source->Buffer().Valid()) {
		return;
	}
	BindDrawTarget();
	renderer->Copy(rc, from, source->Buffer());
}

std::unique_ptr<IScreenLineLayout> DrawSurface::Layout(const IScreenLine *screenLine) {
	return LayoutScreenLine(screenLine);
}

void DrawSurface::DrawTextCommon(PRectangle rc, const Font *font_, XYPOSITION ybase,
	std::string_view text, ColourRGBA fore, bool fillBack, ColourRGBA back, bool clipToRc) {
	if (!renderer) {
		// Measure-only surfaces never paint; match the previous no-op contract.
		return;
	}
	if (fillBack && !rc.Empty()) {
		BindDrawTarget();
		renderer->FillRectangle(rc, back);
	}
	if (text.empty() || !font_ || fore.GetAlpha() == 0) {
		return;
	}
	std::shared_ptr<FontFace> primary = SharedFaceFromFont(font_);
	if (!primary) {
		return;
	}
	const std::shared_ptr<const ShapedRun> run = runCache.Get(text, primary, fallback);
	if (!run || run->glyphs.empty()) {
		return;
	}
	BindDrawTarget();
	if (clipToRc) {
		clipStack.push_back(rc);
		try {
			renderer->SetClip(rc);
		} catch (...) {
			clipStack.pop_back();
			throw;
		}
	}
	XYPOSITION penX = rc.left;
	for (const ShapedGlyph &glyph : run->glyphs) {
		if (glyph.face) {
			// HarfBuzz uses font coordinates with Y up; surfaces use Y down.
			renderer->DrawGlyph(penX + glyph.xOffset, ybase - glyph.yOffset,
				glyph.face, glyph.glyphId, fore);
		}
		penX += glyph.xAdvance;
	}
	if (clipToRc) {
		renderer->PopClip();
		clipStack.pop_back();
	}
}

void DrawSurface::DrawTextNoClip(PRectangle rc, const Font *font_, XYPOSITION ybase,
	std::string_view text, ColourRGBA fore, ColourRGBA back) {
	DrawTextCommon(rc, font_, ybase, text, fore, true, back, false);
}

void DrawSurface::DrawTextClipped(PRectangle rc, const Font *font_, XYPOSITION ybase,
	std::string_view text, ColourRGBA fore, ColourRGBA back) {
	DrawTextCommon(rc, font_, ybase, text, fore, true, back, true);
}

void DrawSurface::DrawTextTransparent(PRectangle rc, const Font *font_, XYPOSITION ybase,
	std::string_view text, ColourRGBA fore) {
	DrawTextCommon(rc, font_, ybase, text, fore, false, ColourRGBA(), false);
}

std::shared_ptr<FontFace> DrawSurface::RequireFace(const Font *font_) const {
	std::shared_ptr<FontFace> face = SharedFaceFromFont(font_);
	if (!face) {
		throw std::runtime_error("DrawSurface requires a platform Font with a face");
	}
	return face;
}

FontMetrics DrawSurface::MetricsOf(const Font *font_) const {
	return RequireFace(font_)->Metrics();
}

void DrawSurface::MeasureWidths(const Font *font_, std::string_view text, XYPOSITION *positions) {
	if (!positions || text.empty()) {
		return;
	}
	MeasureWidthsShaped(text, RequireFace(font_), fallback, positions, &runCache);
}

XYPOSITION DrawSurface::WidthText(const Font *font_, std::string_view text) {
	if (text.empty()) {
		return 0.0;
	}
	return WidthTextShaped(text, RequireFace(font_), fallback, &runCache);
}

XYPOSITION DrawSurface::Ascent(const Font *font_) {
	return static_cast<XYPOSITION>(MetricsOf(font_).ascent);
}

XYPOSITION DrawSurface::Descent(const Font *font_) {
	return static_cast<XYPOSITION>(MetricsOf(font_).descent);
}

XYPOSITION DrawSurface::InternalLeading(const Font *font_) {
	return static_cast<XYPOSITION>(MetricsOf(font_).internalLeading);
}

XYPOSITION DrawSurface::Height(const Font *font_) {
	return static_cast<XYPOSITION>(MetricsOf(font_).height);
}

XYPOSITION DrawSurface::AverageCharWidth(const Font *font_) {
	return WidthText(font_, "x");
}

void DrawSurface::SetClip(PRectangle rc) {
	EnsureRenderer();
	BindDrawTarget();
	clipStack.push_back(rc);
	try {
		renderer->SetClip(rc);
	} catch (...) {
		clipStack.pop_back();
		throw;
	}
}

void DrawSurface::PopClip() {
	EnsureRenderer();
	BindDrawTarget();
	if (clipStack.empty()) {
		return;
	}
	renderer->PopClip();
	clipStack.pop_back();
}

void DrawSurface::FlushCachedState() {}
void DrawSurface::FlushDrawing() {
	if (renderer) {
		renderer->MakeCurrent();
		// Commands are immediate; no batch to flush yet.
	}
}

std::unique_ptr<DrawSurface> CreateDrawSurface(Renderer &renderer, int width, int height,
	FontFallback fallback) {
	auto surface = std::make_unique<DrawSurface>(&renderer, std::move(fallback));
	surface->Init(WindowID{});
	renderer.MakeCurrent();
	surface->Buffer().Resize(width, height);
	surface->BindDrawTarget();
	return surface;
}

std::unique_ptr<DrawSurface> CreateExternalDrawSurface(Renderer &renderer, unsigned framebuffer,
	int width, int height, FontFallback fallback) {
	return CreateExternalDrawSurface(renderer, framebuffer, width, height,
		width, height, RasterScale{}, std::move(fallback));
}

std::unique_ptr<DrawSurface> CreateExternalDrawSurface(Renderer &renderer, unsigned framebuffer,
	int bufferWidth, int bufferHeight, int logicalWidth, int logicalHeight,
	FontFallback fallback) {
	return CreateExternalDrawSurface(renderer, framebuffer, bufferWidth, bufferHeight,
		logicalWidth, logicalHeight, RasterScale{}, std::move(fallback));
}

std::unique_ptr<DrawSurface> CreateExternalDrawSurface(Renderer &renderer, unsigned framebuffer,
	int bufferWidth, int bufferHeight, int logicalWidth, int logicalHeight,
	RasterScale rasterScale, FontFallback fallback) {
	auto surface = std::make_unique<DrawSurface>(&renderer, std::move(fallback));
	surface->Init(WindowID{});
	surface->SetExternalDrawTarget(
		framebuffer, bufferWidth, bufferHeight, logicalWidth, logicalHeight, rasterScale);
	renderer.MakeCurrent();
	surface->BindDrawTarget();
	return surface;
}

std::unique_ptr<DrawSurface> CreateMeasureOnlySurface(FontFallback fallback) {
	auto surface = std::make_unique<DrawSurface>(nullptr, std::move(fallback));
	surface->Init(WindowID{});
	return surface;
}

std::unique_ptr<Surface> Surface::Allocate() {
	// Measure-only by default. Drawing surfaces need a Renderer and size from
	// CreateDrawSurface or a host override of CreateDrawingSurface. Production
	// uses the shared FontCache resolver; editor tests inject fixture faces.
	return CreateMeasureOnlySurface(DefaultSurfaceFallback(10.0));
}

}
