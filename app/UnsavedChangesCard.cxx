#include "UnsavedChangesCard.h"

#include <algorithm>

namespace Scalpel {

namespace {

int CardContentHeight(const UiStyle &style) noexcept {
	return style.cardPad + style.cardTitleHeight + style.unsavedTitleSubtitleGap +
		style.unsavedSubtitleHeight + style.cardButtonTopGap +
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

UnsavedChangesCardLayout LayoutUnsavedChangesCard(int width, int height) noexcept {
	const UiStyle &style = DefaultUiStyle();
	UnsavedChangesCardLayout layout{
		Scintilla::Internal::PRectangle{},
		Scintilla::Internal::PRectangle{},
		Scintilla::Internal::PRectangle{},
		Scintilla::Internal::PRectangle{},
		Scintilla::Internal::PRectangle{},
		Scintilla::Internal::PRectangle{},
		Scintilla::Internal::PRectangle{},
	};
	if (width <= 0 || height <= 0) {
		return layout;
	}

	layout.scrim = Scintilla::Internal::PRectangle::FromInts(0, 0, width, height);

	const int usedHeight = CardContentHeight(style);
	// Prefer the design width; shrink only when the client cannot hold it.
	const int maxByClient = std::max(0, width - 2 * style.cardMargin);
	int usedWidth = style.unsavedCardMaxWidth;
	if (maxByClient > 0) {
		usedWidth = std::min(style.unsavedCardMaxWidth, maxByClient);
	}
	// Keep a usable row even on very narrow clients.
	if (usedWidth < style.unsavedCardMinWidth && width >= style.unsavedCardMinWidth) {
		usedWidth = std::min(style.unsavedCardMinWidth, width);
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
	if (cardTop + usedHeight > height && height >= usedHeight) {
		cardTop = height - usedHeight;
	}

	layout.card = Scintilla::Internal::PRectangle::FromInts(
		cardLeft, cardTop, cardLeft + usedWidth, cardTop + usedHeight);

	const int innerLeft = cardLeft + style.cardPad;
	const int innerRight = cardLeft + usedWidth - style.cardPad;
	const int innerWidth = std::max(0, innerRight - innerLeft);

	int y = cardTop + style.cardPad;
	layout.title = Scintilla::Internal::PRectangle::FromInts(
		innerLeft, y, innerRight, y + style.cardTitleHeight);
	y += style.cardTitleHeight + style.unsavedTitleSubtitleGap;
	layout.subtitle = Scintilla::Internal::PRectangle::FromInts(
		innerLeft, y, innerRight, y + style.unsavedSubtitleHeight);
	y += style.unsavedSubtitleHeight + style.cardButtonTopGap;

	const int buttonCount = style.unsavedButtonCount;
	const int totalGaps = style.unsavedButtonGap * (buttonCount - 1);
	const int buttonWidth = innerWidth > totalGaps
		? (innerWidth - totalGaps) / buttonCount
		: std::max(1, innerWidth / buttonCount);
	const int usedButtonsWidth = buttonWidth * buttonCount +
		(innerWidth > totalGaps ? totalGaps : 0);
	const int leftover = std::max(0, innerWidth - usedButtonsWidth);

	int x = innerLeft;
	const auto makeButton = [&](int extraWidth) {
		const int w = std::max(1, buttonWidth + extraWidth);
		Scintilla::Internal::PRectangle rect =
			Scintilla::Internal::PRectangle::FromInts(
				x, y, x + w, y + style.cardButtonHeight);
		x += w + (innerWidth > totalGaps ? style.unsavedButtonGap : 0);
		return rect;
	};
	layout.saveButton = makeButton(0);
	layout.discardButton = makeButton(0);
	layout.cancelButton = makeButton(leftover);

	return layout;
}

UnsavedCardHit HitTestUnsavedChangesCard(const UnsavedChangesCardLayout &layout,
	Scintilla::Internal::Point point) noexcept {
	if (NonEmptyContains(layout.saveButton, point)) {
		return UnsavedCardHit::Save;
	}
	if (NonEmptyContains(layout.discardButton, point)) {
		return UnsavedCardHit::Discard;
	}
	if (NonEmptyContains(layout.cancelButton, point)) {
		return UnsavedCardHit::Cancel;
	}
	return UnsavedCardHit::None;
}

int CycleUnsavedCardFocus(int focusedIndex, int delta) noexcept {
	const int count = DefaultUiStyle().unsavedButtonCount;
	int index = focusedIndex % count;
	if (index < 0) {
		index += count;
	}
	index = (index + delta) % count;
	if (index < 0) {
		index += count;
	}
	return index;
}

UnsavedChangesCardPainter::UnsavedChangesCardPainter(const UiStyle &styleIn)
	: style(styleIn) {
	// Match Platform::DefaultFontSize (16pt) and a slightly smaller body, using
	// the same 96-DPI pixel conversion as ViewStyle::FontRealised.
	titleFont = Font::Allocate(FontParameters{
		style.fontName, UiPixelSizeFromPoints(style.cardTitlePoints)});
	bodyFont = Font::Allocate(FontParameters{
		style.fontName, UiPixelSizeFromPoints(style.cardBodyPoints)});
}

void UnsavedChangesCardPainter::Paint(Surface &surface,
	const UnsavedChangesCardLayout &layout,
	std::string_view title,
	std::string_view subtitle,
	int focusedButtonIndex) const {
	// Dim the whole client; corner radius 0 for a flat scrim.
	surface.AlphaRectangle(layout.scrim, 0.0,
		FillStroke(style.cardScrim, ColourRGBA(0, 0, 0, 0), 0.0));

	if (!layout.card.Empty()) {
		surface.FillRectangle(layout.card, Fill(style.cardFill));
		DrawInsideFrame(surface, layout.card, style.cardBorder, 1.0);
	}

	DrawCenteredLabel(surface, layout.title, titleFont.get(), title, style.text);
	DrawCenteredLabel(surface, layout.subtitle, bodyFont.get(), subtitle,
		style.mutedText);

	DrawButton(surface, layout.saveButton, bodyFont.get(), "Save",
		focusedButtonIndex == 0, style);
	DrawButton(surface, layout.discardButton, bodyFont.get(), "Discard",
		focusedButtonIndex == 1, style);
	DrawButton(surface, layout.cancelButton, bodyFont.get(), "Cancel",
		focusedButtonIndex == 2, style);
}

}
