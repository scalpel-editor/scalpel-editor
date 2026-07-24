#include "UnsavedChangesCard.h"

#include <algorithm>

namespace Scalpel {

namespace {

constexpr int kCardMaxWidth = 360;
constexpr int kCardMinWidth = 200;
constexpr int kMargin = 16;
constexpr int kCardPad = 16;
constexpr int kTitleHeight = 24;
constexpr int kSubtitleHeight = 20;
constexpr int kTitleSubtitleGap = 6;
constexpr int kButtonsTopGap = 16;
constexpr int kButtonHeight = 32;
constexpr int kButtonGap = 8;
constexpr int kButtonCount = 3;

constexpr int CardContentHeight() noexcept {
	return kCardPad + kTitleHeight + kTitleSubtitleGap + kSubtitleHeight +
		kButtonsTopGap + kButtonHeight + kCardPad;
}

}

UnsavedChangesCardLayout LayoutUnsavedChangesCard(int width, int height) noexcept {
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

	const int usedHeight = CardContentHeight();
	// Prefer the design width; shrink only when the client cannot hold it.
	const int maxByClient = std::max(0, width - 2 * kMargin);
	int usedWidth = kCardMaxWidth;
	if (maxByClient > 0) {
		usedWidth = std::min(kCardMaxWidth, maxByClient);
	}
	// Keep a usable row even on very narrow clients.
	if (usedWidth < kCardMinWidth && width >= kCardMinWidth) {
		usedWidth = std::min(kCardMinWidth, width);
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

	const int innerLeft = cardLeft + kCardPad;
	const int innerRight = cardLeft + usedWidth - kCardPad;
	const int innerWidth = std::max(0, innerRight - innerLeft);

	int y = cardTop + kCardPad;
	layout.title = Scintilla::Internal::PRectangle::FromInts(
		innerLeft, y, innerRight, y + kTitleHeight);
	y += kTitleHeight + kTitleSubtitleGap;
	layout.subtitle = Scintilla::Internal::PRectangle::FromInts(
		innerLeft, y, innerRight, y + kSubtitleHeight);
	y += kSubtitleHeight + kButtonsTopGap;

	const int totalGaps = kButtonGap * (kButtonCount - 1);
	const int buttonWidth = innerWidth > totalGaps
		? (innerWidth - totalGaps) / kButtonCount
		: std::max(1, innerWidth / kButtonCount);
	const int usedButtonsWidth = buttonWidth * kButtonCount +
		(innerWidth > totalGaps ? totalGaps : 0);
	const int leftover = std::max(0, innerWidth - usedButtonsWidth);

	int x = innerLeft;
	const auto makeButton = [&](int extraWidth) {
		const int w = std::max(1, buttonWidth + extraWidth);
		Scintilla::Internal::PRectangle rect =
			Scintilla::Internal::PRectangle::FromInts(x, y, x + w, y + kButtonHeight);
		x += w + (innerWidth > totalGaps ? kButtonGap : 0);
		return rect;
	};
	layout.saveButton = makeButton(0);
	layout.discardButton = makeButton(0);
	layout.cancelButton = makeButton(leftover);

	return layout;
}

namespace {

bool NonEmptyContains(const Scintilla::Internal::PRectangle &rc,
	Scintilla::Internal::Point point) noexcept {
	return rc.right > rc.left && rc.bottom > rc.top && rc.Contains(point);
}

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
	constexpr int count = 3;
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

namespace {

using Scintilla::Internal::ColourRGBA;
using Scintilla::Internal::Fill;
using Scintilla::Internal::FillStroke;
using Scintilla::Internal::Font;
using Scintilla::Internal::FontParameters;
using Scintilla::Internal::PRectangle;
using Scintilla::Internal::Surface;
using Scintilla::Internal::XYPOSITION;

// Near Platform::Chrome for the card body; dark text like the gutter.
const ColourRGBA kScrimFill(0x00, 0x00, 0x00, 0x59); // ~0.35 alpha
const ColourRGBA kCardFill(0xf0, 0xf0, 0xf0, 0xff);
const ColourRGBA kCardBorder(0x90, 0x90, 0x90, 0xff);
const ColourRGBA kText(0x20, 0x20, 0x20, 0xff);
const ColourRGBA kMutedText(0x60, 0x60, 0x60, 0xff);
const ColourRGBA kButtonFill(0xe0, 0xe0, 0xe0, 0xff);
const ColourRGBA kButtonBorder(0xa0, 0xa0, 0xa0, 0xff);
const ColourRGBA kFocusedFill(0xd0, 0xe4, 0xf8, 0xff);
const ColourRGBA kFocusedBorder(0x30, 0x70, 0xb0, 0xff);

// FontParameters.size is device pixels (see FontPlatform FC_PIXEL_SIZE). Match
// ViewStyle at 96 DPI: points * 96/72, same as DeviceHeightFont / 100.
constexpr float PixelSizeFromPoints(float points) noexcept {
	return points * 96.0f / 72.0f;
}

/**
 * Centre label ink with DrawTextTransparent. DrawTextNoClip fills an opaque
 * background rectangle that erases button borders drawn underneath.
 */
void DrawCenteredLabel(Surface &surface, const PRectangle &rc, const Font *font,
	std::string_view text, ColourRGBA fore) {
	if (!font || text.empty() || rc.Empty()) {
		return;
	}
	const XYPOSITION textWidth = surface.WidthText(font, text);
	const XYPOSITION ascent = surface.Ascent(font);
	const XYPOSITION height = surface.Height(font);
	const XYPOSITION x = rc.left + (rc.Width() - textWidth) / 2.0;
	const XYPOSITION ybase = rc.top + (rc.Height() - height) / 2.0 + ascent;
	// Tight text box around the ink advance; height still needs room for ascent.
	const PRectangle textRc(x, rc.top, x + textWidth, rc.bottom);
	surface.DrawTextTransparent(textRc, font, ybase, text, fore);
}

/** Axis-aligned border that sits inside rc (integer-friendly, no poly-line miters). */
void DrawInsideFrame(Surface &surface, const PRectangle &rc, ColourRGBA colour,
	XYPOSITION thickness) {
	if (rc.Empty() || thickness <= 0.0) {
		return;
	}
	const XYPOSITION t = std::min(thickness, std::min(rc.Width(), rc.Height()) / 2.0);
	const Fill fill(colour);
	// Top and bottom span the full width; left and right sit between them.
	surface.FillRectangle(PRectangle(rc.left, rc.top, rc.right, rc.top + t), fill);
	surface.FillRectangle(PRectangle(rc.left, rc.bottom - t, rc.right, rc.bottom), fill);
	surface.FillRectangle(PRectangle(rc.left, rc.top + t, rc.left + t, rc.bottom - t), fill);
	surface.FillRectangle(PRectangle(rc.right - t, rc.top + t, rc.right, rc.bottom - t), fill);
}

void DrawButton(Surface &surface, const PRectangle &rc, const Font *font,
	std::string_view label, bool focused) {
	if (rc.Empty()) {
		return;
	}
	const ColourRGBA fill = focused ? kFocusedFill : kButtonFill;
	const ColourRGBA border = focused ? kFocusedBorder : kButtonBorder;
	const XYPOSITION borderWidth = focused ? 2.0 : 1.0;
	surface.FillRectangle(rc, Fill(fill));
	// Label first, then frame so border edges stay continuous over the text band.
	DrawCenteredLabel(surface, rc, font, label, kText);
	DrawInsideFrame(surface, rc, border, borderWidth);
}

}

UnsavedChangesCardPainter::UnsavedChangesCardPainter() {
	// 14pt / 12pt → same device sizes the editor uses for similar point sizes.
	titleFont = Font::Allocate(FontParameters{"system-ui", PixelSizeFromPoints(14.0f)});
	bodyFont = Font::Allocate(FontParameters{"system-ui", PixelSizeFromPoints(12.0f)});
}

void UnsavedChangesCardPainter::Paint(Surface &surface,
	const UnsavedChangesCardLayout &layout,
	std::string_view title,
	std::string_view subtitle,
	int focusedButtonIndex) const {
	// Dim the whole client; corner radius 0 for a flat scrim.
	surface.AlphaRectangle(layout.scrim, 0.0,
		FillStroke(kScrimFill, ColourRGBA(0, 0, 0, 0), 0.0));

	if (!layout.card.Empty()) {
		surface.FillRectangle(layout.card, Fill(kCardFill));
		DrawInsideFrame(surface, layout.card, kCardBorder, 1.0);
	}

	DrawCenteredLabel(surface, layout.title, titleFont.get(), title, kText);
	DrawCenteredLabel(surface, layout.subtitle, bodyFont.get(), subtitle, kMutedText);

	DrawButton(surface, layout.saveButton, bodyFont.get(), "Save",
		focusedButtonIndex == 0);
	DrawButton(surface, layout.discardButton, bodyFont.get(), "Discard",
		focusedButtonIndex == 1);
	DrawButton(surface, layout.cancelButton, bodyFont.get(), "Cancel",
		focusedButtonIndex == 2);
}
}
