// scalpel-editor measure-only surface: widths and screen-line layout from shaped runs.

#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "EditorStyleTypes.h"
#include "FontPlatform.h"
#include "Geometry.h"
#include "MeasureSurface.h"
#include "Platform.h"
#include "ShapedLayout.h"
#include "ShapedRun.h"

namespace Scintilla::Internal {

MeasureSurface::MeasureSurface(std::vector<std::shared_ptr<FontFace>> fallbacks_) :
	fallbacks(std::move(fallbacks_)) {
}

void MeasureSurface::SetFallbacks(std::vector<std::shared_ptr<FontFace>> fallbacks_) {
	fallbacks = std::move(fallbacks_);
	runCache.Clear();
}

void MeasureSurface::Init(WindowID) {
	initialised = true;
}

void MeasureSurface::Init(SurfaceID, WindowID) {
	initialised = true;
}

std::unique_ptr<Surface> MeasureSurface::AllocatePixMap(int, int) {
	auto pix = std::make_unique<MeasureSurface>(fallbacks);
	pix->initialised = true;
	pix->mode = mode;
	return pix;
}

void MeasureSurface::SetMode(SurfaceMode mode_) {
	mode = mode_;
}

void MeasureSurface::Release() noexcept {
	initialised = false;
}

int MeasureSurface::SupportsFeature(Scintilla::Supports feature) noexcept {
	// Measurement is single-threaded through the shaped-run cache for now.
	(void)feature;
	return 0;
}

bool MeasureSurface::Initialised() {
	return initialised;
}

int MeasureSurface::LogPixelsY() {
	return 96;
}

int MeasureSurface::PixelDivisions() {
	return 1;
}

int MeasureSurface::DeviceHeightFont(int points) {
	return (points * LogPixelsY() + 36) / 72;
}

void MeasureSurface::LineDraw(Point, Point, Stroke) {}
void MeasureSurface::PolyLine(const Point *, size_t, Stroke) {}
void MeasureSurface::Polygon(const Point *, size_t, FillStroke) {}
void MeasureSurface::RectangleDraw(PRectangle, FillStroke) {}
void MeasureSurface::RectangleFrame(PRectangle, Stroke) {}
void MeasureSurface::FillRectangle(PRectangle, Fill) {}
void MeasureSurface::FillRectangleAligned(PRectangle, Fill) {}
void MeasureSurface::FillRectangle(PRectangle, Surface &) {}
void MeasureSurface::RoundedRectangle(PRectangle, FillStroke) {}
void MeasureSurface::AlphaRectangle(PRectangle, XYPOSITION, FillStroke) {}
void MeasureSurface::GradientRectangle(PRectangle, const std::vector<ColourStop> &, GradientOptions) {}
void MeasureSurface::DrawRGBAImage(PRectangle, int, int, const unsigned char *) {}
void MeasureSurface::Ellipse(PRectangle, FillStroke) {}
void MeasureSurface::Stadium(PRectangle, FillStroke, Ends) {}
void MeasureSurface::Copy(PRectangle, Point, Surface &) {}

std::unique_ptr<IScreenLineLayout> MeasureSurface::Layout(const IScreenLine *screenLine) {
	return LayoutScreenLine(screenLine);
}

void MeasureSurface::DrawTextNoClip(PRectangle, const Font *, XYPOSITION, std::string_view, ColourRGBA, ColourRGBA) {}
void MeasureSurface::DrawTextClipped(PRectangle, const Font *, XYPOSITION, std::string_view, ColourRGBA, ColourRGBA) {}
void MeasureSurface::DrawTextTransparent(PRectangle, const Font *, XYPOSITION, std::string_view, ColourRGBA) {}

std::shared_ptr<FontFace> MeasureSurface::RequireFace(const Font *font_) const {
	std::shared_ptr<FontFace> face = SharedFaceFromFont(font_);
	if (!face) {
		throw std::runtime_error("MeasureSurface requires a platform Font with a face");
	}
	return face;
}

FontMetrics MeasureSurface::MetricsOf(const Font *font_) const {
	return RequireFace(font_)->Metrics();
}

void MeasureSurface::MeasureWidths(const Font *font_, std::string_view text, XYPOSITION *positions) {
	if (!positions || text.empty()) {
		return;
	}
	MeasureWidthsShaped(text, RequireFace(font_), fallbacks, positions, &runCache);
}

XYPOSITION MeasureSurface::WidthText(const Font *font_, std::string_view text) {
	if (text.empty()) {
		return 0.0;
	}
	return WidthTextShaped(text, RequireFace(font_), fallbacks, &runCache);
}

XYPOSITION MeasureSurface::Ascent(const Font *font_) {
	return static_cast<XYPOSITION>(MetricsOf(font_).ascent);
}

XYPOSITION MeasureSurface::Descent(const Font *font_) {
	return static_cast<XYPOSITION>(MetricsOf(font_).descent);
}

XYPOSITION MeasureSurface::InternalLeading(const Font *font_) {
	return static_cast<XYPOSITION>(MetricsOf(font_).internalLeading);
}

XYPOSITION MeasureSurface::Height(const Font *font_) {
	return static_cast<XYPOSITION>(MetricsOf(font_).height);
}

XYPOSITION MeasureSurface::AverageCharWidth(const Font *font_) {
	// One ASCII "x" advance is a stable average for English fixture fonts.
	return WidthText(font_, "x");
}

void MeasureSurface::SetClip(PRectangle) {}
void MeasureSurface::PopClip() {}
void MeasureSurface::FlushCachedState() {}
void MeasureSurface::FlushDrawing() {}

std::unique_ptr<Surface> CreateMeasureSurface(
	std::vector<std::shared_ptr<FontFace>> fallbacks) {
	auto surface = std::make_unique<MeasureSurface>(std::move(fallbacks));
	surface->Init(WindowID{});
	return surface;
}

}
