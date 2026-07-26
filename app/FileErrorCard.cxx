#include "FileErrorCard.h"

#include <algorithm>

namespace Scalpel {

namespace {

constexpr int kCardMaxWidth = 480;
constexpr int kCardMinWidth = 220;
constexpr int kMargin = 16;
constexpr int kCardPad = 16;
constexpr int kTitleHeight = 24;
constexpr int kPathHeight = 24;
constexpr int kTitlePathGap = 8;
constexpr int kButtonTopGap = 16;
constexpr int kButtonWidth = 96;
constexpr int kButtonHeight = 32;

constexpr int CardContentHeight() noexcept {
	return kCardPad + kTitleHeight + kTitlePathGap + kPathHeight +
		kButtonTopGap + kButtonHeight + kCardPad;
}

bool NonEmptyContains(const Scintilla::Internal::PRectangle &rc,
	Scintilla::Internal::Point point) noexcept {
	return rc.right > rc.left && rc.bottom > rc.top &&
		point.x >= rc.left && point.x < rc.right &&
		point.y >= rc.top && point.y < rc.bottom;
}

}

FileErrorCardLayout LayoutFileErrorCard(int width, int height) noexcept {
	FileErrorCardLayout layout;
	if (width <= 0 || height <= 0) {
		return layout;
	}

	layout.scrim = Scintilla::Internal::PRectangle::FromInts(
		0, 0, width, height);
	const int maxByClient = std::max(0, width - 2 * kMargin);
	int usedWidth = std::min(kCardMaxWidth, maxByClient);
	if (usedWidth < kCardMinWidth && width >= kCardMinWidth) {
		usedWidth = kCardMinWidth;
	}
	usedWidth = std::min(usedWidth, width);
	// Keep the card inside the client so the dismiss control can stay hittable.
	const int usedHeight = std::min(CardContentHeight(), height);
	const int cardLeft = std::max(0, (width - usedWidth) / 2);
	const int cardTop = std::max(0, (height - usedHeight) / 2);
	layout.card = Scintilla::Internal::PRectangle::FromInts(
		cardLeft, cardTop, cardLeft + usedWidth, cardTop + usedHeight);

	const int innerLeft = cardLeft + kCardPad;
	const int innerRight = cardLeft + usedWidth - kCardPad;
	const int available = std::max(0, innerRight - innerLeft);
	const int buttonWidth = std::min(kButtonWidth, available);
	const int buttonLeft = innerLeft + std::max(0, (available - buttonWidth) / 2);

	// Prefer the natural top-down stack; when the client is shorter than the
	// content height, pin OK to the bottom of the card and place text above it.
	const int naturalButtonTop = cardTop + kCardPad + kTitleHeight +
		kTitlePathGap + kPathHeight + kButtonTopGap;
	const int cardBottom = cardTop + usedHeight;
	const int maxButtonBottom = cardBottom;
	int buttonTop = naturalButtonTop;
	int buttonHeight = kButtonHeight;
	if (buttonTop + buttonHeight > maxButtonBottom) {
		buttonHeight = std::min(kButtonHeight, usedHeight);
		buttonTop = std::max(cardTop, cardBottom - buttonHeight);
	}
	layout.dismissButton = Scintilla::Internal::PRectangle::FromInts(
		buttonLeft, buttonTop, buttonLeft + buttonWidth,
		buttonTop + buttonHeight);

	int titleTop = cardTop + kCardPad;
	int pathTop = titleTop + kTitleHeight + kTitlePathGap;
	if (pathTop + kPathHeight > buttonTop) {
		// Short card: stack title and path into the space above the button.
		const int textBottom = std::max(cardTop, buttonTop - kButtonTopGap);
		pathTop = std::max(cardTop, textBottom - kPathHeight);
		titleTop = std::max(cardTop, pathTop - kTitlePathGap - kTitleHeight);
	}
	layout.title = Scintilla::Internal::PRectangle::FromInts(
		innerLeft, titleTop, innerRight, titleTop + kTitleHeight);
	layout.path = Scintilla::Internal::PRectangle::FromInts(
		innerLeft, pathTop, innerRight, pathTop + kPathHeight);
	return layout;
}

bool HitTestFileErrorCard(const FileErrorCardLayout &layout,
	Scintilla::Internal::Point point) noexcept {
	return NonEmptyContains(layout.dismissButton, point);
}

namespace {

using Scintilla::Internal::ColourRGBA;
using Scintilla::Internal::Fill;
using Scintilla::Internal::FillStroke;
using Scintilla::Internal::Font;
using Scintilla::Internal::FontParameters;
using Scintilla::Internal::PRectangle;
using Scintilla::Internal::Surface;
using Scintilla::Internal::XYPOSITION;

const ColourRGBA kScrimFill(0x00, 0x00, 0x00, 0x59);
const ColourRGBA kCardFill(0xf0, 0xf0, 0xf0, 0xff);
const ColourRGBA kCardBorder(0x90, 0x90, 0x90, 0xff);
const ColourRGBA kText(0x20, 0x20, 0x20, 0xff);
const ColourRGBA kMutedText(0x60, 0x60, 0x60, 0xff);
const ColourRGBA kFocusedFill(0xd0, 0xe4, 0xf8, 0xff);
const ColourRGBA kFocusedBorder(0x30, 0x70, 0xb0, 0xff);

constexpr float PixelSizeFromPoints(float points) noexcept {
	return points * 96.0f / 72.0f;
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
	surface.DrawTextTransparent(
		PRectangle(x, rc.top, x + textWidth, rc.bottom),
		font, ybase, text, fore);
}

void DrawInsideFrame(Surface &surface, const PRectangle &rc, ColourRGBA colour,
	XYPOSITION thickness) {
	if (rc.Empty() || thickness <= 0.0) {
		return;
	}
	const XYPOSITION t =
		std::min(thickness, std::min(rc.Width(), rc.Height()) / 2.0);
	const Fill fill(colour);
	surface.FillRectangle(PRectangle(rc.left, rc.top, rc.right, rc.top + t), fill);
	surface.FillRectangle(
		PRectangle(rc.left, rc.bottom - t, rc.right, rc.bottom), fill);
	surface.FillRectangle(
		PRectangle(rc.left, rc.top + t, rc.left + t, rc.bottom - t), fill);
	surface.FillRectangle(
		PRectangle(rc.right - t, rc.top + t, rc.right, rc.bottom - t), fill);
}

}

FileErrorCardPainter::FileErrorCardPainter() {
	titleFont =
		Font::Allocate(FontParameters{"system-ui", PixelSizeFromPoints(16.0f)});
	bodyFont =
		Font::Allocate(FontParameters{"system-ui", PixelSizeFromPoints(14.0f)});
}

void FileErrorCardPainter::Paint(Surface &surface,
	const FileErrorCardLayout &layout,
	std::string_view title,
	std::string_view path) const {
	surface.AlphaRectangle(layout.scrim, 0.0,
		FillStroke(kScrimFill, ColourRGBA(0, 0, 0, 0), 0.0));
	if (!layout.card.Empty()) {
		surface.FillRectangle(layout.card, Fill(kCardFill));
		DrawInsideFrame(surface, layout.card, kCardBorder, 1.0);
	}
	DrawCenteredLabel(surface, layout.title, titleFont.get(), title, kText);
	DrawCenteredLabel(surface, layout.path, bodyFont.get(), path, kMutedText);
	if (!layout.dismissButton.Empty()) {
		surface.FillRectangle(layout.dismissButton, Fill(kFocusedFill));
		DrawCenteredLabel(surface, layout.dismissButton, bodyFont.get(), "OK", kText);
		DrawInsideFrame(surface, layout.dismissButton, kFocusedBorder, 2.0);
	}
}

}
