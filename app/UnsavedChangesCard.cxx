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

}
