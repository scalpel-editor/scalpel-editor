#include "PlatOWUI.h"

#include <OnlyWayUi/Core/Colour.h>
#include <OnlyWayUi/Core/Core.h>
#include <OnlyWayUi/Core/FontEngineInterface.h>
#include <OnlyWayUi/Core/FontMetrics.h>
#include <OnlyWayUi/Core/Geometry.h>
#include <OnlyWayUi/Core/Mesh.h>
#include <OnlyWayUi/Core/MeshUtilities.h>
#include <OnlyWayUi/Core/Rectangle.h>
#include <OnlyWayUi/Core/RenderManager.h>
#include <OnlyWayUi/Core/StringUtilities.h>
#include <OnlyWayUi/Core/TextShapingContext.h>
#include <OnlyWayUi/Core/Texture.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "EditorBasicTypes.h"
#include "EditorDocumentTypes.h"
#include "EditorStyleTypes.h"
#include "EditorInputTypes.h"
#include "EditorLayoutTypes.h"
#include "Geometry.h"
#include "Platform.h"

namespace Scintilla::Internal {
namespace {

using OnlyWayUi::Colourb;
using OnlyWayUi::ColourbPremultiplied;
using OnlyWayUi::Editor::SurfaceTarget;
using OnlyWayUi::FontEngineInterface;
using OnlyWayUi::FontFaceHandle;
using OnlyWayUi::FontMetrics;
using OnlyWayUi::Geometry;
using OnlyWayUi::Mesh;
using OnlyWayUi::MeshUtilities;
using OnlyWayUi::Rectanglei;
using OnlyWayUi::RenderManager;
using OnlyWayUi::Span;
using OnlyWayUi::String;
using OnlyWayUi::StringView;
using OnlyWayUi::TextShapingContext;
using OnlyWayUi::TexturedMeshList;
using OnlyWayUi::Vector2f;

namespace StringUtilities = OnlyWayUi::StringUtilities;

const String empty_language;
const TextShapingContext text_shaping_context{empty_language, OnlyWayUi::Style::Direction::Ltr, OnlyWayUi::Style::FontKerning::Auto, 0.0f};

StringView ToStringView(std::string_view text) noexcept
{
	return text.empty() ? StringView() : StringView(text.data(), text.data() + text.size());
}

ColourbPremultiplied ConvertColour(ColourRGBA colour) noexcept
{
	return Colourb(colour.GetRed(), colour.GetGreen(), colour.GetBlue(), colour.GetAlpha()).ToPremultiplied();
}

Vector2f RectanglePosition(PRectangle rectangle) noexcept
{
	return {static_cast<float>(rectangle.left), static_cast<float>(rectangle.top)};
}

Vector2f RectangleSize(PRectangle rectangle) noexcept
{
	return {static_cast<float>(rectangle.Width()), static_cast<float>(rectangle.Height())};
}

Rectanglei ScissorRectangle(PRectangle rectangle, Vector2f origin) noexcept
{
	return Rectanglei::FromCorners(
		{static_cast<int>(std::floor(rectangle.left + origin.x)), static_cast<int>(std::floor(rectangle.top + origin.y))},
		{static_cast<int>(std::ceil(rectangle.right + origin.x)), static_cast<int>(std::ceil(rectangle.bottom + origin.y))});
}

class FontImpl final : public Font {
public:
	explicit FontImpl(const FontParameters& parameters)
	{
		font_engine = OnlyWayUi::GetFontEngineInterface();
		if (!font_engine || !parameters.faceName)
			return;

		const char* family_begin = parameters.faceName[0] == '!' ? parameters.faceName + 1 : parameters.faceName;
		const String family = StringUtilities::ToLower(family_begin);
		const auto style = parameters.italic ? OnlyWayUi::Style::FontStyle::Italic : OnlyWayUi::Style::FontStyle::Normal;
		const auto weight = static_cast<OnlyWayUi::Style::FontWeight>(static_cast<unsigned int>(parameters.weight));
		const int size = std::max(1, static_cast<int>(std::lround(parameters.size)));
		handle = font_engine->GetFontFaceHandle(family, style, weight, size);
	}

	FontEngineInterface* font_engine = nullptr;
	FontFaceHandle handle = {};
};

const FontImpl* AsFont(const Font* font) noexcept
{
	return static_cast<const FontImpl*>(font);
}

class SurfaceImpl final : public Surface {
public:
	~SurfaceImpl() override { RestoreClips(); }

	void Init(WindowID wid) override
	{
		Release();
		target = static_cast<SurfaceTarget*>(wid);
	}

	void Init(SurfaceID sid, WindowID wid) override
	{
		Release();
		target = static_cast<SurfaceTarget*>(sid ? sid : wid);
	}

	std::unique_ptr<Surface> AllocatePixMap(int, int) override
	{
		auto surface = std::make_unique<SurfaceImpl>();
		surface->mode = mode;
		surface->pixmap_stub = true;
		return surface;
	}

	void SetMode(SurfaceMode new_mode) override { mode = new_mode; }

	void Release() noexcept override
	{
		RestoreClips();
		target = nullptr;
	}

	int SupportsFeature(Scintilla::Supports) noexcept override { return 0; }

	bool Initialised() override { return target != nullptr || pixmap_stub; }
	int LogPixelsY() override { return target ? std::max(1, target->pixels_per_inch) : 96; }
	int PixelDivisions() override { return 1; }

	int DeviceHeightFont(int points) override
	{
		const int pixels_per_inch = LogPixelsY();
		return (points * pixels_per_inch + 36) / 72;
	}

	void LineDraw(Point, Point, Stroke) override {}
	void PolyLine(const Point*, size_t, Stroke) override {}
	void Polygon(const Point*, size_t, FillStroke) override {}
	void RectangleDraw(PRectangle, FillStroke) override {}
	void RectangleFrame(PRectangle, Stroke) override {}

	void FillRectangle(PRectangle rectangle, Fill fill) override { DrawRectangle(rectangle, fill.colour, false); }
	void FillRectangleAligned(PRectangle rectangle, Fill fill) override { DrawRectangle(rectangle, fill.colour, true); }
	void FillRectangle(PRectangle, Surface&) override {}
	void RoundedRectangle(PRectangle, FillStroke) override {}
	void AlphaRectangle(PRectangle, XYPOSITION, FillStroke) override {}
	void GradientRectangle(PRectangle, const std::vector<ColourStop>&, GradientOptions) override {}
	void DrawRGBAImage(PRectangle, int, int, const unsigned char*) override {}
	void Ellipse(PRectangle, FillStroke) override {}
	void Stadium(PRectangle, FillStroke, Ends) override {}
	void Copy(PRectangle, Point, Surface&) override {}

	std::unique_ptr<IScreenLineLayout> Layout(const IScreenLine*) override { return {}; }

	void DrawTextNoClip(PRectangle rectangle, const Font* font, XYPOSITION ybase, std::string_view text, ColourRGBA fore,
		ColourRGBA back) override
	{
		FillRectangleAligned(rectangle, back);
		DrawText(rectangle, font, ybase, text, fore);
	}

	void DrawTextClipped(PRectangle rectangle, const Font* font, XYPOSITION ybase, std::string_view text, ColourRGBA fore,
		ColourRGBA back) override
	{
		FillRectangleAligned(rectangle, back);
		SetClip(rectangle);
		DrawText(rectangle, font, ybase, text, fore);
		PopClip();
	}

	void DrawTextTransparent(PRectangle rectangle, const Font* font, XYPOSITION ybase, std::string_view text, ColourRGBA fore) override
	{
		DrawText(rectangle, font, ybase, text, fore);
	}

	void MeasureWidths(const Font* font, std::string_view text, XYPOSITION* positions) override
	{
		if (!font || !positions || text.empty())
			return;
		const FontImpl* font_impl = AsFont(font);
		if (!font_impl->font_engine || !font_impl->handle)
		{
			std::fill_n(positions, text.size(), 0.0);
			return;
		}

		std::vector<int> byte_positions(text.size());
		font_impl->font_engine->MeasureString(font_impl->handle, ToStringView(text), Span<int>(byte_positions), text_shaping_context);
		std::transform(byte_positions.begin(), byte_positions.end(), positions,
			[](int position) { return static_cast<XYPOSITION>(position); });
	}

	XYPOSITION WidthText(const Font* font, std::string_view text) override
	{
		if (!font)
			return 0.0;
		const FontImpl* font_impl = AsFont(font);
		if (!font_impl->font_engine || !font_impl->handle)
			return 0.0;
		return static_cast<XYPOSITION>(font_impl->font_engine->GetStringWidth(font_impl->handle, ToStringView(text), text_shaping_context));
	}

	XYPOSITION Ascent(const Font* font) override { return Metrics(font).ascent; }
	XYPOSITION Descent(const Font* font) override { return Metrics(font).descent; }

	XYPOSITION InternalLeading(const Font* font) override
	{
		const FontMetrics& metrics = Metrics(font);
		return std::max(0.0f, metrics.line_spacing - metrics.ascent - metrics.descent);
	}

	XYPOSITION Height(const Font* font) override
	{
		const FontMetrics& metrics = Metrics(font);
		return metrics.ascent + metrics.descent;
	}

	XYPOSITION AverageCharWidth(const Font* font) override { return WidthText(font, "n"); }

	void SetClip(PRectangle rectangle) override
	{
		RenderManager* render_manager = RenderManagerForDrawing();
		if (!render_manager)
			return;

		const Rectanglei previous = render_manager->GetScissorRegion();
		clip_stack.push_back(previous);
		Rectanglei next = ScissorRectangle(rectangle, target->origin);
		if (previous.Valid())
			next = next.Intersect(previous);
		render_manager->SetScissorRegion(next);
	}

	void PopClip() override
	{
		RenderManager* render_manager = RenderManagerForDrawing();
		if (!render_manager || clip_stack.empty())
			return;
		render_manager->SetScissorRegion(clip_stack.back());
		clip_stack.pop_back();
	}

	void FlushCachedState() override {}
	void FlushDrawing() override {}

private:
	RenderManager* RenderManagerForDrawing() const noexcept { return target ? target->render_manager : nullptr; }

	void RestoreClips() noexcept
	{
		RenderManager* render_manager = RenderManagerForDrawing();
		if (render_manager && !clip_stack.empty())
			render_manager->SetScissorRegion(clip_stack.front());
		clip_stack.clear();
	}

	const FontMetrics& Metrics(const Font* font) const noexcept
	{
		static const FontMetrics empty_metrics = {};
		if (!font)
			return empty_metrics;
		const FontImpl* font_impl = AsFont(font);
		if (!font_impl->font_engine || !font_impl->handle)
			return empty_metrics;
		return font_impl->font_engine->GetFontMetrics(font_impl->handle);
	}

	void DrawRectangle(PRectangle rectangle, ColourRGBA colour, bool align)
	{
		RenderManager* render_manager = RenderManagerForDrawing();
		if (!render_manager || rectangle.Empty() || colour.GetAlpha() == 0)
			return;

		if (align)
		{
			rectangle.left = std::floor(rectangle.left);
			rectangle.top = std::floor(rectangle.top);
			rectangle.right = std::ceil(rectangle.right);
			rectangle.bottom = std::ceil(rectangle.bottom);
		}

		Mesh mesh;
		MeshUtilities::GenerateQuad(mesh, RectanglePosition(rectangle), RectangleSize(rectangle), ConvertColour(colour));
		Geometry geometry = render_manager->MakeGeometry(std::move(mesh));
		geometry.Render(target->origin.Round());
	}

	void DrawText(PRectangle rectangle, const Font* font, XYPOSITION ybase, std::string_view text, ColourRGBA colour)
	{
		RenderManager* render_manager = RenderManagerForDrawing();
		if (!render_manager || !font || text.empty() || colour.GetAlpha() == 0)
			return;

		const FontImpl* font_impl = AsFont(font);
		if (!font_impl->font_engine || !font_impl->handle)
			return;

		TexturedMeshList mesh_list;
		font_impl->font_engine->GenerateString(*render_manager, font_impl->handle, {}, ToStringView(text),
			{static_cast<float>(rectangle.left), static_cast<float>(ybase)}, ConvertColour(colour), 1.0f, text_shaping_context, mesh_list);
		for (auto& textured_mesh : mesh_list)
		{
			Geometry geometry = render_manager->MakeGeometry(std::move(textured_mesh.mesh));
			geometry.Render(target->origin.Round(), textured_mesh.texture);
		}
	}

	SurfaceMode mode;
	SurfaceTarget* target = nullptr;
	bool pixmap_stub = false;
	std::vector<Rectanglei> clip_stack;
};

} // namespace

std::shared_ptr<Font> Font::Allocate(const FontParameters& parameters)
{
	return std::make_shared<FontImpl>(parameters);
}

std::unique_ptr<Surface> Surface::Allocate(Scintilla::Technology)
{
	return std::make_unique<SurfaceImpl>();
}

} // namespace Scintilla::Internal
