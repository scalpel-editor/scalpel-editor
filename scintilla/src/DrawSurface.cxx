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

DrawSurface::DrawSurface(Renderer *renderer_, std::vector<std::shared_ptr<FontFace>> fallbacks_) :
	renderer(renderer_),
	fallbacks(std::move(fallbacks_)) {
}

DrawSurface::~DrawSurface() {
	if (buffer.Valid() && renderer) {
		renderer->MakeCurrent();
		buffer.Destroy();
	}
}

void DrawSurface::SetFallbacks(std::vector<std::shared_ptr<FontFace>> fallbacks_) {
	fallbacks = std::move(fallbacks_);
	runCache.Clear();
}

void DrawSurface::EnsureRenderer() const {
	if (!renderer) {
		throw std::runtime_error("DrawSurface operation requires a Renderer");
	}
}

void DrawSurface::BindDrawTarget() {
	EnsureRenderer();
	if (!buffer.Valid()) {
		throw std::runtime_error("DrawSurface::BindDrawTarget without a colour buffer");
	}
	// SetDrawTarget resets the clip stack only when the target changes.
	renderer->SetDrawTarget(buffer.FramebufferName(), buffer.Width(), buffer.Height());
	renderer->BindCurrentTarget();
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
	auto pix = std::make_unique<DrawSurface>(renderer, fallbacks);
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

void DrawSurface::FillRectangle(PRectangle, Surface &) {}

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

void DrawSurface::GradientRectangle(PRectangle, const std::vector<ColourStop> &, GradientOptions) {}
void DrawSurface::DrawRGBAImage(PRectangle, int, int, const unsigned char *) {}

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

void DrawSurface::Copy(PRectangle, Point, Surface &) {}

std::unique_ptr<IScreenLineLayout> DrawSurface::Layout(const IScreenLine *screenLine) {
	return LayoutScreenLine(screenLine);
}

// Text drawing arrives in step 6 (glyph atlas). Keep the API honest: no fake glyphs.
void DrawSurface::DrawTextNoClip(PRectangle, const Font *, XYPOSITION, std::string_view, ColourRGBA, ColourRGBA) {}
void DrawSurface::DrawTextClipped(PRectangle, const Font *, XYPOSITION, std::string_view, ColourRGBA, ColourRGBA) {}
void DrawSurface::DrawTextTransparent(PRectangle, const Font *, XYPOSITION, std::string_view, ColourRGBA) {}

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
	MeasureWidthsShaped(text, RequireFace(font_), fallbacks, positions, &runCache);
}

XYPOSITION DrawSurface::WidthText(const Font *font_, std::string_view text) {
	if (text.empty()) {
		return 0.0;
	}
	return WidthTextShaped(text, RequireFace(font_), fallbacks, &runCache);
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
	renderer->SetClip(rc);
}

void DrawSurface::PopClip() {
	EnsureRenderer();
	BindDrawTarget();
	renderer->PopClip();
}

void DrawSurface::FlushCachedState() {}
void DrawSurface::FlushDrawing() {
	if (renderer) {
		renderer->MakeCurrent();
		// Commands are immediate; no batch to flush yet.
	}
}

std::unique_ptr<DrawSurface> CreateDrawSurface(Renderer &renderer, int width, int height,
	std::vector<std::shared_ptr<FontFace>> fallbacks) {
	auto surface = std::make_unique<DrawSurface>(&renderer, std::move(fallbacks));
	surface->Init(WindowID{});
	renderer.MakeCurrent();
	surface->Buffer().Resize(width, height);
	surface->BindDrawTarget();
	return surface;
}

std::unique_ptr<DrawSurface> CreateMeasureOnlySurface(
	std::vector<std::shared_ptr<FontFace>> fallbacks) {
	auto surface = std::make_unique<DrawSurface>(nullptr, std::move(fallbacks));
	surface->Init(WindowID{});
	return surface;
}

}
