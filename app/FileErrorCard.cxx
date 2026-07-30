#include "FileErrorCard.h"

#include <algorithm>

namespace Scalpel {

namespace {

int CardContentHeight(const UiStyle &style) noexcept {
	return style.cardPad + style.cardTitleHeight + style.fileErrorTitlePathGap +
		style.fileErrorPathHeight + style.cardButtonTopGap +
		style.cardButtonHeight + style.cardPad;
}

bool NonEmptyContains(const Scintilla::Internal::PRectangle &rc,
	Scintilla::Internal::Point point) noexcept {
	return rc.right > rc.left && rc.bottom > rc.top &&
		point.x >= rc.left && point.x < rc.right &&
		point.y >= rc.top && point.y < rc.bottom;
}

using Scintilla::Internal::ColourRGBA;
using Scintilla::Internal::Fill;
using Scintilla::Internal::FillStroke;
using Scintilla::Internal::Font;
using Scintilla::Internal::FontParameters;
using Scintilla::Internal::PRectangle;
using Scintilla::Internal::Surface;

}

FileErrorCardLayout LayoutFileErrorCard(int width, int height) noexcept {
	const UiStyle &style = DefaultUiStyle();
	FileErrorCardLayout layout;
	if (width <= 0 || height <= 0) {
		return layout;
	}

	layout.scrim = Scintilla::Internal::PRectangle::FromInts(
		0, 0, width, height);
	const int maxByClient = std::max(0, width - 2 * style.cardMargin);
	int usedWidth = std::min(style.fileErrorCardMaxWidth, maxByClient);
	if (usedWidth < style.fileErrorCardMinWidth &&
		width >= style.fileErrorCardMinWidth) {
		usedWidth = style.fileErrorCardMinWidth;
	}
	usedWidth = std::min(usedWidth, width);
	// Keep the card inside the client so the dismiss control can stay hittable.
	const int usedHeight = std::min(CardContentHeight(style), height);
	const int cardLeft = std::max(0, (width - usedWidth) / 2);
	const int cardTop = std::max(0, (height - usedHeight) / 2);
	layout.card = Scintilla::Internal::PRectangle::FromInts(
		cardLeft, cardTop, cardLeft + usedWidth, cardTop + usedHeight);

	const int innerLeft = cardLeft + style.cardPad;
	const int innerRight = cardLeft + usedWidth - style.cardPad;
	const int available = std::max(0, innerRight - innerLeft);
	const int buttonWidth = std::min(style.fileErrorButtonWidth, available);
	const int buttonLeft = innerLeft + std::max(0, (available - buttonWidth) / 2);

	// Prefer the natural top-down stack; when the client is shorter than the
	// content height, pin OK to the bottom of the card and place text above it.
	const int naturalButtonTop = cardTop + style.cardPad + style.cardTitleHeight +
		style.fileErrorTitlePathGap + style.fileErrorPathHeight +
		style.cardButtonTopGap;
	const int cardBottom = cardTop + usedHeight;
	const int maxButtonBottom = cardBottom;
	int buttonTop = naturalButtonTop;
	int buttonHeight = style.cardButtonHeight;
	if (buttonTop + buttonHeight > maxButtonBottom) {
		buttonHeight = std::min(style.cardButtonHeight, usedHeight);
		buttonTop = std::max(cardTop, cardBottom - buttonHeight);
	}
	layout.dismissButton = Scintilla::Internal::PRectangle::FromInts(
		buttonLeft, buttonTop, buttonLeft + buttonWidth,
		buttonTop + buttonHeight);

	int titleTop = cardTop + style.cardPad;
	int pathTop = titleTop + style.cardTitleHeight + style.fileErrorTitlePathGap;
	if (pathTop + style.fileErrorPathHeight > buttonTop) {
		// Short card: stack title and path into the space above the button.
		const int textBottom = std::max(cardTop, buttonTop - style.cardButtonTopGap);
		pathTop = std::max(cardTop, textBottom - style.fileErrorPathHeight);
		titleTop = std::max(cardTop,
			pathTop - style.fileErrorTitlePathGap - style.cardTitleHeight);
	}
	layout.title = Scintilla::Internal::PRectangle::FromInts(
		innerLeft, titleTop, innerRight, titleTop + style.cardTitleHeight);
	layout.path = Scintilla::Internal::PRectangle::FromInts(
		innerLeft, pathTop, innerRight, pathTop + style.fileErrorPathHeight);
	return layout;
}

bool HitTestFileErrorCard(const FileErrorCardLayout &layout,
	Scintilla::Internal::Point point) noexcept {
	return NonEmptyContains(layout.dismissButton, point);
}

FileErrorCardPainter::FileErrorCardPainter(const UiStyle &styleIn)
	: style(styleIn) {
	titleFont = Font::Allocate(FontParameters{
		style.fontName, UiPixelSizeFromPoints(style.cardTitlePoints)});
	bodyFont = Font::Allocate(FontParameters{
		style.fontName, UiPixelSizeFromPoints(style.cardBodyPoints)});
}

void FileErrorCardPainter::Paint(Surface &surface,
	const FileErrorCardLayout &layout,
	std::string_view title,
	std::string_view path) const {
	surface.AlphaRectangle(layout.scrim, 0.0,
		FillStroke(style.cardScrim, ColourRGBA(0, 0, 0, 0), 0.0));
	if (!layout.card.Empty()) {
		surface.FillRectangle(layout.card, Fill(style.cardFill));
		DrawInsideFrame(surface, layout.card, style.cardBorder, 1.0);
	}
	DrawCenteredLabel(surface, layout.title, titleFont.get(), title, style.text);
	DrawCenteredLabel(surface, layout.path, bodyFont.get(), path, style.mutedText);
	if (!layout.dismissButton.Empty()) {
		surface.FillRectangle(layout.dismissButton, Fill(style.focusFill));
		DrawCenteredLabel(surface, layout.dismissButton, bodyFont.get(), "OK",
			style.text);
		DrawInsideFrame(surface, layout.dismissButton, style.focusBorder, 2.0);
	}
}

}
