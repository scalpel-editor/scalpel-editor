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
 * Painters and layout helpers take a reference; production uses DefaultUiStyle().
 */
struct UiStyle {
	// Typography (FontParameters.size is device pixels at 96 DPI).
	const char *fontName = "system-ui";
	float chromeLabelPoints = 12.0f;
	float cardTitlePoints = 16.0f;
	float cardBodyPoints = 14.0f;

	// Shared ink and focus.
	Scintilla::Internal::ColourRGBA text{0x20, 0x20, 0x20, 0xff};
	Scintilla::Internal::ColourRGBA mutedText{0x60, 0x60, 0x60, 0xff};
	Scintilla::Internal::ColourRGBA disabledText{0xa0, 0xa0, 0xa0, 0xff};
	Scintilla::Internal::ColourRGBA focusFill{0xd0, 0xe4, 0xf8, 0xff};
	Scintilla::Internal::ColourRGBA focusBorder{0x30, 0x70, 0xb0, 0xff};
	Scintilla::Internal::ColourRGBA chromeBorder{0xb8, 0xb8, 0xb8, 0xff};
	Scintilla::Internal::ColourRGBA hoverFill{0xee, 0xee, 0xee, 0xff};
	Scintilla::Internal::ColourRGBA panelFill{0xfc, 0xfc, 0xfc, 0xff};

	// Menu bar and dropdown.
	Scintilla::Internal::ColourRGBA menuBarFill{0xe8, 0xe8, 0xe8, 0xff};
	Scintilla::Internal::ColourRGBA menuItemHover{0xe8, 0xf0, 0xf8, 0xff};
	Scintilla::Internal::ColourRGBA menuSeparator{0xd0, 0xd0, 0xd0, 0xff};
	Scintilla::Internal::ColourRGBA menuDropdownShadow{0x00, 0x00, 0x00, 0x28};
	int menuBarHeight = 24;
	int menuHeadingPadX = 12;
	int menuFileHeadingWidth = 48;
	int menuEditHeadingWidth = 48;
	int menuRecentHeadingWidth = 68;
	int menuItemHeight = 24;
	int menuSeparatorHeight = 9;
	int menuDropdownPadY = 4;
	int menuLabelPadLeft = 12;
	int menuShortcutPadRight = 12;
	int menuLabelShortcutGap = 24;
	int menuShortcutColumnWidth = 100;
	int menuDropdownPreferredWidth = 220;
	int menuRecentDropdownPreferredWidth = 440;

	// Tab strip.
	Scintilla::Internal::ColourRGBA tabStripFill{0xe4, 0xe4, 0xe4, 0xff};
	Scintilla::Internal::ColourRGBA tabDirtyAccent{0xc0, 0x60, 0x20, 0xff};
	Scintilla::Internal::ColourRGBA tabCloseFill{0xd0, 0xd0, 0xd0, 0xff};
	Scintilla::Internal::ColourRGBA tabCloseHoverFill{0xc0, 0x70, 0x70, 0xff};
	Scintilla::Internal::ColourRGBA tabGlyphInk{0x40, 0x40, 0x40, 0xff};
	int tabStripHeight = 28;
	int tabPreferredWidth = 140;
	int tabAddButtonWidth = 28;
	int tabCloseSize = 14;
	int tabClosePadRight = 6;
	int tabLabelPadLeft = 10;
	int tabLabelPadRight = 4;
	int tabDefaultScrollStep = 40;

	// Scrollbars.
	Scintilla::Internal::ColourRGBA scrollTrackFill{0xff, 0xff, 0xff, 0xff};
	Scintilla::Internal::ColourRGBA scrollThumbFill{0xc8, 0xc8, 0xc8, 0xff};
	Scintilla::Internal::ColourRGBA scrollThumbHover{0xb0, 0xb0, 0xb0, 0xff};
	Scintilla::Internal::ColourRGBA scrollThumbPressed{0x98, 0x98, 0x98, 0xff};
	int scrollBarThickness = 14;
	int scrollBarMinThumb = 24;

	// Modal cards (shared chrome).
	Scintilla::Internal::ColourRGBA cardScrim{0x00, 0x00, 0x00, 0x59};
	Scintilla::Internal::ColourRGBA cardFill{0xf0, 0xf0, 0xf0, 0xff};
	Scintilla::Internal::ColourRGBA cardBorder{0x90, 0x90, 0x90, 0xff};
	Scintilla::Internal::ColourRGBA cardButtonFill{0xe0, 0xe0, 0xe0, 0xff};
	Scintilla::Internal::ColourRGBA cardButtonBorder{0xa0, 0xa0, 0xa0, 0xff};
	int cardMargin = 16;
	int cardPad = 16;
	int cardTitleHeight = 24;
	int cardButtonHeight = 32;
	int cardButtonTopGap = 16;

	// Unsaved-changes card.
	int unsavedCardMaxWidth = 360;
	int unsavedCardMinWidth = 200;
	int unsavedSubtitleHeight = 20;
	int unsavedTitleSubtitleGap = 6;
	int unsavedButtonGap = 8;
	int unsavedButtonCount = 3;

	// File-error card.
	int fileErrorCardMaxWidth = 480;
	int fileErrorCardMinWidth = 220;
	int fileErrorPathHeight = 24;
	int fileErrorTitlePathGap = 8;
	int fileErrorButtonWidth = 96;
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
