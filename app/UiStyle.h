// Fixed application chrome style: palette, typography, spacing, focus colours,
// and control dimensions. One immutable default; no runtime theme discovery.

#ifndef UISTYLE_H
#define UISTYLE_H

#include <string_view>

#include "Geometry.h"
#include "Platform.h"

namespace Scalpel {

/**
 * Immutable visual constants for permanent chrome and modal cards.
 * Production layout and painters use DefaultUiStyle().
 */
struct UiStyle {
	UiStyle(const UiStyle &) = default;
	UiStyle &operator=(const UiStyle &) = delete;

	// Typography in points; painters convert to device pixels at 96 DPI.
	const char *const fontName = "system-ui";
	const float chromeLabelPoints = 12.0f;
	const float cardTitlePoints = 16.0f;
	const float cardBodyPoints = 14.0f;

	// Shared ink and focus.
	const Scintilla::Internal::ColourRGBA text{0x20, 0x20, 0x20, 0xff};
	const Scintilla::Internal::ColourRGBA mutedText{0x60, 0x60, 0x60, 0xff};
	const Scintilla::Internal::ColourRGBA disabledText{0xa0, 0xa0, 0xa0, 0xff};
	const Scintilla::Internal::ColourRGBA focusFill{0xd0, 0xe4, 0xf8, 0xff};
	const Scintilla::Internal::ColourRGBA focusBorder{0x30, 0x70, 0xb0, 0xff};
	const Scintilla::Internal::ColourRGBA chromeBorder{0xb8, 0xb8, 0xb8, 0xff};
	const Scintilla::Internal::ColourRGBA hoverFill{0xee, 0xee, 0xee, 0xff};
	const Scintilla::Internal::ColourRGBA panelFill{0xfc, 0xfc, 0xfc, 0xff};

	// Menu bar and dropdown.
	const Scintilla::Internal::ColourRGBA menuBarFill{0xe8, 0xe8, 0xe8, 0xff};
	const Scintilla::Internal::ColourRGBA menuItemHover{0xe8, 0xf0, 0xf8, 0xff};
	const Scintilla::Internal::ColourRGBA menuSeparator{0xd0, 0xd0, 0xd0, 0xff};
	const Scintilla::Internal::ColourRGBA menuDropdownShadow{0x00, 0x00, 0x00, 0x28};
	const int menuBarHeight = 24;
	const int menuHeadingPadX = 12;
	const int menuFileHeadingWidth = 48;
	const int menuEditHeadingWidth = 48;
	const int menuFontHeadingWidth = 48;
	const int menuRecentHeadingWidth = 68;
	const int menuItemHeight = 24;
	const int menuSeparatorHeight = 9;
	const int menuDropdownPadY = 4;
	const int menuLabelPadLeft = 12;
	/** Left column reserved for the Font radio mark on each Font row. */
	const int menuSelectionIndicatorWidth = 18;
	/** Edge length of the filled square used as the Font radio mark. */
	const int menuSelectionMarkSize = 6;
	const int menuShortcutPadRight = 12;
	const int menuLabelShortcutGap = 24;
	const int menuShortcutColumnWidth = 100;
	const int menuDropdownPreferredWidth = 220;
	const int menuRecentDropdownPreferredWidth = 440;

	// Tab strip.
	const Scintilla::Internal::ColourRGBA tabStripFill{0xe4, 0xe4, 0xe4, 0xff};
	const Scintilla::Internal::ColourRGBA tabDirtyAccent{0xc0, 0x60, 0x20, 0xff};
	const Scintilla::Internal::ColourRGBA tabCloseFill{0xd0, 0xd0, 0xd0, 0xff};
	const Scintilla::Internal::ColourRGBA tabCloseHoverFill{0xc0, 0x70, 0x70, 0xff};
	const Scintilla::Internal::ColourRGBA tabGlyphInk{0x40, 0x40, 0x40, 0xff};
	const int tabStripHeight = 28;
	const int tabPreferredWidth = 140;
	const int tabAddButtonWidth = 28;
	const int tabCloseSize = 14;
	const int tabClosePadRight = 6;
	const int tabLabelPadLeft = 10;
	const int tabLabelPadRight = 4;
	const int tabDefaultScrollStep = 40;

	// Scrollbars.
	const Scintilla::Internal::ColourRGBA scrollTrackFill{0xff, 0xff, 0xff, 0xff};
	const Scintilla::Internal::ColourRGBA scrollThumbFill{0xc8, 0xc8, 0xc8, 0xff};
	const Scintilla::Internal::ColourRGBA scrollThumbHover{0xb0, 0xb0, 0xb0, 0xff};
	const Scintilla::Internal::ColourRGBA scrollThumbPressed{0x98, 0x98, 0x98, 0xff};
	const int scrollBarThickness = 14;
	const int scrollBarMinThumb = 24;

	// Modal cards (shared chrome).
	const Scintilla::Internal::ColourRGBA cardScrim{0x00, 0x00, 0x00, 0x59};
	const Scintilla::Internal::ColourRGBA cardFill{0xf0, 0xf0, 0xf0, 0xff};
	const Scintilla::Internal::ColourRGBA cardBorder{0x90, 0x90, 0x90, 0xff};
	const Scintilla::Internal::ColourRGBA cardButtonFill{0xe0, 0xe0, 0xe0, 0xff};
	const Scintilla::Internal::ColourRGBA cardButtonBorder{0xa0, 0xa0, 0xa0, 0xff};
	const int cardMargin = 16;
	const int cardPad = 16;
	const int cardTitleHeight = 24;
	const int cardButtonHeight = 32;
	const int cardButtonTopGap = 16;

	// Unsaved-changes card.
	const int unsavedCardMaxWidth = 360;
	const int unsavedCardMinWidth = 200;
	const int unsavedSubtitleHeight = 20;
	const int unsavedTitleSubtitleGap = 6;
	const int unsavedButtonGap = 8;

	// File-error card.
	const int fileErrorCardMaxWidth = 480;
	const int fileErrorCardMinWidth = 220;
	const int fileErrorPathHeight = 24;
	const int fileErrorTitlePathGap = 8;
	const int fileErrorButtonWidth = 96;

private:
	UiStyle() = default;
	friend const UiStyle &DefaultUiStyle() noexcept;
};

/** Production fixed style. The only style until runtime themes exist. */
[[nodiscard]] const UiStyle &DefaultUiStyle() noexcept;

/** FontParameters.size is device pixels (see FontPlatform FC_PIXEL_SIZE). */
[[nodiscard]] constexpr float UiPixelSizeFromPoints(float points) noexcept {
	return points * 96.0f / 72.0f;
}

/** Axis-aligned border drawn inside rc (integer-friendly, no poly-line miters). */
void DrawInsideFrame(Scintilla::Internal::Surface &surface,
	const Scintilla::Internal::PRectangle &rc,
	Scintilla::Internal::ColourRGBA colour,
	Scintilla::Internal::XYPOSITION thickness);

/**
 * Centre label ink with DrawTextTransparent. DrawTextNoClip fills an opaque
 * background that would erase borders drawn underneath. Pen is pixel-aligned
 * so GL_NEAREST glyph samples stay stable.
 */
void DrawCenteredLabel(Scintilla::Internal::Surface &surface,
	const Scintilla::Internal::PRectangle &rc,
	const Scintilla::Internal::Font *font,
	std::string_view text,
	Scintilla::Internal::ColourRGBA fore);

void DrawLeftAlignedLabel(Scintilla::Internal::Surface &surface,
	const Scintilla::Internal::PRectangle &rc,
	const Scintilla::Internal::Font *font,
	std::string_view text,
	Scintilla::Internal::ColourRGBA fore);

void DrawRightAlignedLabel(Scintilla::Internal::Surface &surface,
	const Scintilla::Internal::PRectangle &rc,
	const Scintilla::Internal::Font *font,
	std::string_view text,
	Scintilla::Internal::ColourRGBA fore);

}

#endif
