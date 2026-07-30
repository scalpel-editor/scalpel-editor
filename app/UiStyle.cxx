#include "UiStyle.h"

#include <algorithm>

namespace Scalpel {

const UiStyle &DefaultUiStyle() noexcept {
	static const UiStyle style{};
	return style;
}

namespace {

using Scintilla::Internal::ColourRGBA;
using Scintilla::Internal::Fill;
using Scintilla::Internal::Font;
using Scintilla::Internal::PRectangle;
using Scintilla::Internal::Surface;
using Scintilla::Internal::XYPOSITION;

}

void DrawInsideFrame(Surface &surface, const PRectangle &rc, ColourRGBA colour,
	XYPOSITION thickness) {
	if (rc.Empty() || thickness <= 0.0) {
		return;
	}
	const XYPOSITION t =
		std::min(thickness, std::min(rc.Width(), rc.Height()) / 2.0);
	const Fill fill(colour);
	// Top and bottom span the full width; left and right sit between them.
	surface.FillRectangle(PRectangle(rc.left, rc.top, rc.right, rc.top + t), fill);
	surface.FillRectangle(
		PRectangle(rc.left, rc.bottom - t, rc.right, rc.bottom), fill);
	surface.FillRectangle(
		PRectangle(rc.left, rc.top + t, rc.left + t, rc.bottom - t), fill);
	surface.FillRectangle(
		PRectangle(rc.right - t, rc.top + t, rc.right, rc.bottom - t), fill);
}

void DrawCenteredLabel(Surface &surface, const PRectangle &rc, const Font *font,
	std::string_view text, ColourRGBA fore) {
	if (!font || text.empty() || rc.Empty()) {
		return;
	}
	const XYPOSITION textWidth = surface.WidthText(font, text);
	const XYPOSITION ascent = surface.Ascent(font);
	const XYPOSITION height = surface.Height(font);
	const XYPOSITION x = static_cast<XYPOSITION>(static_cast<int>(
		rc.left + (rc.Width() - textWidth) / 2.0));
	const XYPOSITION ybase = static_cast<XYPOSITION>(static_cast<int>(
		rc.top + (rc.Height() - height) / 2.0 + ascent));
	const PRectangle textRc(x, rc.top, x + textWidth, rc.bottom);
	surface.DrawTextTransparent(textRc, font, ybase, text, fore);
}

void DrawLeftAlignedLabel(Surface &surface, const PRectangle &rc,
	const Font *font, std::string_view text, ColourRGBA fore) {
	if (!font || text.empty() || rc.Empty()) {
		return;
	}
	const XYPOSITION ascent = surface.Ascent(font);
	const XYPOSITION height = surface.Height(font);
	const XYPOSITION x = static_cast<XYPOSITION>(static_cast<int>(rc.left));
	const XYPOSITION ybase = static_cast<XYPOSITION>(static_cast<int>(
		rc.top + (rc.Height() - height) / 2.0 + ascent));
	const PRectangle textRc(x, rc.top, rc.right, rc.bottom);
	surface.DrawTextTransparent(textRc, font, ybase, text, fore);
}

void DrawRightAlignedLabel(Surface &surface, const PRectangle &rc,
	const Font *font, std::string_view text, ColourRGBA fore) {
	if (!font || text.empty() || rc.Empty()) {
		return;
	}
	const XYPOSITION textWidth = surface.WidthText(font, text);
	const XYPOSITION ascent = surface.Ascent(font);
	const XYPOSITION height = surface.Height(font);
	const XYPOSITION x = static_cast<XYPOSITION>(static_cast<int>(
		std::max(rc.left, rc.right - textWidth)));
	const XYPOSITION ybase = static_cast<XYPOSITION>(static_cast<int>(
		rc.top + (rc.Height() - height) / 2.0 + ascent));
	const PRectangle textRc(x, rc.top, rc.right, rc.bottom);
	surface.DrawTextTransparent(textRc, font, ybase, text, fore);
}

}
