#include "LargeFileCard.h"

#include <algorithm>

namespace Scalpel {

namespace {

// Open and Cancel are fixed behavior, not visual style.
constexpr int kButtonCount = 2;

int CardContentHeight(const UiStyle &style) noexcept {
	return style.cardPad + style.cardTitleHeight + style.largeFileTitlePathGap +
		style.largeFilePathHeight + style.cardButtonTopGap +
		style.cardButtonHeight + style.cardPad;
}

bool NonEmptyContains(const Scintilla::Internal::PRectangle &rc,
	Scintilla::Internal::Point point) noexcept {
	return rc.right > rc.left && rc.bottom > rc.top && rc.Contains(point);
}

using Scintilla::Internal::ColourRGBA;
using Scintilla::Internal::Fill;
using Scintilla::Internal::FillStroke;
using Scintilla::Internal::Font;
using Scintilla::Internal::FontParameters;
using Scintilla::Internal::PRectangle;
using Scintilla::Internal::Surface;
using Scintilla::Internal::XYPOSITION;

void DrawButton(Surface &surface, const PRectangle &rc, const Font *font,
	std::string_view label, bool focused, const UiStyle &style) {
	if (rc.Empty()) {
		return;
	}
	const ColourRGBA fill = focused ? style.focusFill : style.cardButtonFill;
	const ColourRGBA border =
		focused ? style.focusBorder : style.cardButtonBorder;
	const XYPOSITION borderWidth = focused ? 2.0 : 1.0;
	surface.FillRectangle(rc, Fill(fill));
	// Label first, then frame so border edges stay continuous over the text band.
	DrawCenteredLabel(surface, rc, font, label, style.text);
	DrawInsideFrame(surface, rc, border, borderWidth);
}

}

LargeFileCardLayout LayoutLargeFileCard(int width, int height) noexcept {
	const UiStyle &style = DefaultUiStyle();
	LargeFileCardLayout layout;
	if (width <= 0 || height <= 0) {
		return layout;
	}

	layout.scrim = PRectangle::FromInts(0, 0, width, height);

	const int usedHeight = std::min(CardContentHeight(style), height);
	const int maxByClient = std::max(0, width - 2 * style.cardMargin);
	int usedWidth = style.largeFileCardMaxWidth;
	if (maxByClient > 0) {
		usedWidth = std::min(style.largeFileCardMaxWidth, maxByClient);
	}
	if (usedWidth < style.largeFileCardMinWidth &&
		width >= style.largeFileCardMinWidth) {
		usedWidth = std::min(style.largeFileCardMinWidth, width);
	}
	if (usedWidth > width) {
		usedWidth = width;
	}

	int cardLeft = (width - usedWidth) / 2;
	int cardTop = (height - usedHeight) / 2;
	if (cardLeft < 0) {
		cardLeft = 0;
	}
	if (cardTop < 0) {
		cardTop = 0;
	}
	if (cardLeft + usedWidth > width) {
		cardLeft = std::max(0, width - usedWidth);
	}
	layout.card = PRectangle::FromInts(
		cardLeft, cardTop, cardLeft + usedWidth, cardTop + usedHeight);

	const int horizontalPad = std::min(style.cardPad, usedWidth / 2);
	const int innerLeft = cardLeft + horizontalPad;
	const int innerRight = cardLeft + usedWidth - horizontalPad;
	const int innerWidth = std::max(0, innerRight - innerLeft);

	const int naturalPathTop = cardTop + style.cardPad +
		style.cardTitleHeight + style.largeFileTitlePathGap;
	const int naturalButtonTop = naturalPathTop + style.largeFilePathHeight +
		style.cardButtonTopGap;
	const int cardBottom = cardTop + usedHeight;
	const int buttonHeight = std::min(style.cardButtonHeight, usedHeight);
	const int buttonTop = std::min(naturalButtonTop, cardBottom - buttonHeight);

	int pathTop = naturalPathTop;
	int titleTop = cardTop + style.cardPad;
	if (pathTop + style.largeFilePathHeight > buttonTop) {
		const int textBottom =
			std::max(cardTop, buttonTop - style.cardButtonTopGap);
		pathTop = std::max(cardTop, textBottom - style.largeFilePathHeight);
		titleTop = std::max(cardTop,
			pathTop - style.largeFileTitlePathGap - style.cardTitleHeight);
	}
	layout.title = PRectangle::FromInts(
		innerLeft, titleTop, innerRight,
		std::min(cardBottom, titleTop + style.cardTitleHeight));
	layout.path = PRectangle::FromInts(
		innerLeft, pathTop, innerRight,
		std::min(cardBottom, pathTop + style.largeFilePathHeight));

	const int totalGaps = style.largeFileButtonGap * (kButtonCount - 1);
	const int buttonWidth = innerWidth > totalGaps
		? (innerWidth - totalGaps) / kButtonCount
		: innerWidth / kButtonCount;
	const int usedButtonsWidth = buttonWidth * kButtonCount +
		(innerWidth > totalGaps ? totalGaps : 0);
	const int leftover = std::max(0, innerWidth - usedButtonsWidth);

	int x = innerLeft;
	const auto makeButton = [&](int extraWidth) {
		const int w = buttonWidth + extraWidth;
		PRectangle rect = PRectangle::FromInts(
			x, buttonTop, x + w, buttonTop + buttonHeight);
		x += w + (innerWidth > totalGaps ? style.largeFileButtonGap : 0);
		return rect;
	};
	layout.openButton = makeButton(0);
	layout.cancelButton = makeButton(leftover);

	return layout;
}

LargeFileCardHit HitTestLargeFileCard(const LargeFileCardLayout &layout,
	Scintilla::Internal::Point point) noexcept {
	if (NonEmptyContains(layout.openButton, point)) {
		return LargeFileCardHit::Open;
	}
	if (NonEmptyContains(layout.cancelButton, point)) {
		return LargeFileCardHit::Cancel;
	}
	return LargeFileCardHit::None;
}

int CycleLargeFileCardFocus(int focusedIndex, int delta) noexcept {
	int index = focusedIndex % kButtonCount;
	if (index < 0) {
		index += kButtonCount;
	}
	index = (index + delta) % kButtonCount;
	if (index < 0) {
		index += kButtonCount;
	}
	return index;
}

LargeFileCardPainter::LargeFileCardPainter(const UiStyle &styleIn)
	: style(styleIn) {
	titleFont = Font::Allocate(FontParameters{
		style.fontName, UiPixelSizeFromPoints(style.cardTitlePoints)});
	bodyFont = Font::Allocate(FontParameters{
		style.fontName, UiPixelSizeFromPoints(style.cardBodyPoints)});
}

void LargeFileCardPainter::Paint(Surface &surface,
	const LargeFileCardLayout &layout,
	std::string_view title,
	std::string_view path,
	int focusedButtonIndex) const {
	surface.AlphaRectangle(layout.scrim, 0.0,
		FillStroke(style.cardScrim, ColourRGBA(0, 0, 0, 0), 0.0));

	if (!layout.card.Empty()) {
		surface.FillRectangle(layout.card, Fill(style.cardFill));
		DrawInsideFrame(surface, layout.card, style.cardBorder, 1.0);
	}

	DrawCenteredLabel(surface, layout.title, titleFont.get(), title, style.text);
	DrawCenteredLabel(surface, layout.path, bodyFont.get(), path, style.mutedText);

	DrawButton(surface, layout.openButton, bodyFont.get(), "Open",
		focusedButtonIndex == 0, style);
	DrawButton(surface, layout.cancelButton, bodyFont.get(), "Cancel",
		focusedButtonIndex == 1, style);
}

}
