// Scintilla source code edit control
/** @file EditorStyling.cxx
 ** Document styles, style definitions, elements, whitespace view, selection colours,
 ** layout caches, zoom, long-line edges, and idle styling.
 **
 ** Each document byte has a style byte (0..STYLE_MAX). Lexer or container styling walks the
 ** document with StartStyling / SetStyling / SetStylingEx; GetEndStyled reports how far styles
 ** are believed correct. Style definition messages configure how those style numbers look.
 ** Element colours override colours for selection, whitespace, caret, and other chrome.
 ** Layout caches, draw phases, font locale, extra ascent/descent, and zoom affect measurement
 ** and paint. Long-line edge mode/colour complements the edge column APIs in EditorLines.cxx.
 **/
// Copyright 1998-2011 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cmath>

#include <stdexcept>
#include <utility>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <set>
#include <forward_list>
#include <optional>
#include <algorithm>
#include <iterator>
#include <memory>
#include <chrono>
#include <atomic>
#include <mutex>
#include <thread>
#include <future>

#include "ScintillaTypes.h"
#include "ScintillaStructures.h"
#include "ILoader.h"
#include "ILexer.h"

#include "Debugging.h"
#include "Geometry.h"
#include "Platform.h"

#include "CharacterType.h"
#include "CharacterCategoryMap.h"
#include "Position.h"
#include "UniqueString.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "ContractionState.h"
#include "CellBuffer.h"
#include "PerLine.h"
#include "KeyMap.h"
#include "Indicator.h"
#include "LineMarker.h"
#include "Style.h"
#include "ViewStyle.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "CaseConvert.h"
#include "Document.h"
#include "UniConversion.h"
#include "Selection.h"
#include "PositionCache.h"
#include "EditModel.h"
#include "MarginView.h"
#include "EditView.h"
#include "Editor.h"
#include "ElapsedPeriod.h"

using namespace Scintilla;
using namespace Scintilla::Internal;


// --- Document style bytes ----------------------------------------------------

// Clear all style bytes to 0, drop lexer decorations, expand folds, clear fold levels, and refresh annotation heights. Does not change the text.
void Editor::ClearDocumentStyle() {
	pdoc->decorations->DeleteLexerDecorations();
	pdoc->StartStyling(0);
	pdoc->SetStyleFor(pdoc->Length(), 0);
	pcs->ShowAll();
	SetAnnotationHeights(0, pdoc->LinesTotal());
	pdoc->ClearLevels();
}

// Style byte at position, or 0 past the end of the document. Values are 0..255.
int Editor::GetStyleAt(Sci::Position pos) const noexcept {
	if (pos >= pdoc->Length())
		return 0;
	return pdoc->StyleAt(pos);
}

// Same as GetStyleAt but always returns the style index as an integer in 0..255.
int Editor::GetStyleIndexAt(Sci::Position pos) const noexcept {
	if (pos >= pdoc->Length())
		return 0;
	return pdoc->StyleIndexAt(pos);
}

// Last position believed styled correctly. Moves forward when styles are applied and backward when text before it changes. Drawing may request more styling from here.
Sci::Position Editor::GetEndStyled() const noexcept {
	return pdoc->GetEndStyled();
}

// Prepare to apply styles starting at start. Call SetStyling or SetStylingEx for each run.
void Editor::StartStyling(Sci::Position start) {
	pdoc->StartStyling(start);
}

// Apply style to the next length bytes from the styling position, then advance that position by length.
// Negative length sets errorStatus to Failure and does nothing.
void Editor::SetStyling(Sci::Position length, int style) {
	if (length < 0)
		errorStatus = Status::Failure;
	else
		pdoc->SetStyleFor(length, static_cast<char>(style));
}

// Apply one style byte per document byte for length bytes from the styling position, then advance.
// styles must point at length bytes; a null pointer is ignored.
void Editor::SetStylingEx(Sci::Position length, const char *styles) {
	if (!styles)
		return;
	pdoc->SetStyles(length, styles);
}

// When and how much to style in idle time. None (default) styles the visible area before paint.
// ToVisible styles a little before display then continues in idle; AfterVisible styles past the view in idle; All does both.
// Has no effect while wrapping is on because wrapping already uses idle time and requires styles.
void Editor::SetIdleStyling(IdleStyling idleStyling_) {
	idleStyling = idleStyling_;
}

// Current idle-styling mode.
IdleStyling Editor::GetIdleStyling() const noexcept {
	return idleStyling;
}

// --- Style definition --------------------------------------------------------

// Copy STYLE_DEFAULT into all styles and redraw. Use after setting the default style when many styles should match it.
void Editor::StyleClearAll() {
	vs.ClearStyles();
	InvalidateStyleRedraw();
}

// Reset STYLE_DEFAULT to Scintilla's built-in default face, size, and colours, then redraw.
void Editor::StyleResetDefault() {
	vs.ResetDefaultStyle();
	InvalidateStyleRedraw();
}

// Foreground colour of style (opaque RGB).
void Editor::StyleSetFore(int style, int rgb) {
	vs.EnsureStyle(static_cast<size_t>(style));
	vs.styles[style].fore = ColourRGBA::FromIpRGB(rgb);
	InvalidateStyleRedraw();
}

int Editor::StyleGetFore(int style) {
	vs.EnsureStyle(static_cast<size_t>(style));
	return vs.styles[style].fore.OpaqueRGB();
}

// Background colour of style (opaque RGB).
void Editor::StyleSetBack(int style, int rgb) {
	vs.EnsureStyle(static_cast<size_t>(style));
	vs.styles[style].back = ColourRGBA::FromIpRGB(rgb);
	InvalidateStyleRedraw();
}

int Editor::StyleGetBack(int style) {
	vs.EnsureStyle(static_cast<size_t>(style));
	return vs.styles[style].back.OpaqueRGB();
}

// Bold weight (true → FontWeight::Bold, false → Normal). Prefer StyleSetWeight for finer control.
void Editor::StyleSetBold(int style, bool bold) {
	vs.EnsureStyle(static_cast<size_t>(style));
	vs.styles[style].weight = bold ? FontWeight::Bold : FontWeight::Normal;
	InvalidateStyleRedraw();
}

bool Editor::StyleGetBold(int style) {
	vs.EnsureStyle(static_cast<size_t>(style));
	return vs.styles[style].weight > FontWeight::Normal;
}

// Font weight (100..1000 scale used by FontWeight).
void Editor::StyleSetWeight(int style, FontWeight weight) {
	vs.EnsureStyle(static_cast<size_t>(style));
	vs.styles[style].weight = weight;
	InvalidateStyleRedraw();
}

FontWeight Editor::StyleGetWeight(int style) {
	vs.EnsureStyle(static_cast<size_t>(style));
	return vs.styles[style].weight;
}

// Font stretch (ultra-condensed … ultra-expanded).
void Editor::StyleSetStretch(int style, FontStretch stretch) {
	vs.EnsureStyle(static_cast<size_t>(style));
	vs.styles[style].stretch = stretch;
	InvalidateStyleRedraw();
}

FontStretch Editor::StyleGetStretch(int style) {
	vs.EnsureStyle(static_cast<size_t>(style));
	return vs.styles[style].stretch;
}

// Italic face.
void Editor::StyleSetItalic(int style, bool italic) {
	vs.EnsureStyle(static_cast<size_t>(style));
	vs.styles[style].italic = italic;
	InvalidateStyleRedraw();
}

bool Editor::StyleGetItalic(int style) {
	vs.EnsureStyle(static_cast<size_t>(style));
	return vs.styles[style].italic;
}

// When true, the style's background fills to the right margin on that line.
void Editor::StyleSetEOLFilled(int style, bool eolFilled) {
	vs.EnsureStyle(static_cast<size_t>(style));
	vs.styles[style].eolFilled = eolFilled;
	InvalidateStyleRedraw();
}

bool Editor::StyleGetEOLFilled(int style) {
	vs.EnsureStyle(static_cast<size_t>(style));
	return vs.styles[style].eolFilled;
}

// Point size as a whole number (stored as size * FontSizeMultiplier).
void Editor::StyleSetSize(int style, int sizePoints) {
	vs.EnsureStyle(static_cast<size_t>(style));
	vs.styles[style].size = sizePoints * FontSizeMultiplier;
	InvalidateStyleRedraw();
}

int Editor::StyleGetSize(int style) {
	vs.EnsureStyle(static_cast<size_t>(style));
	return vs.styles[style].size / FontSizeMultiplier;
}

// Fractional point size in hundredths of a point (FontSizeMultiplier units).
void Editor::StyleSetSizeFractional(int style, int sizeHundredthPoints) {
	vs.EnsureStyle(static_cast<size_t>(style));
	vs.styles[style].size = sizeHundredthPoints;
	InvalidateStyleRedraw();
}

int Editor::StyleGetSizeFractional(int style) {
	vs.EnsureStyle(static_cast<size_t>(style));
	return vs.styles[style].size;
}

// Font family name. Null is ignored.
void Editor::StyleSetFont(int style, const char *fontName) {
	if (!fontName)
		return;
	vs.EnsureStyle(static_cast<size_t>(style));
	vs.SetStyleFontName(style, fontName);
	InvalidateStyleRedraw();
}

// Copies the font name into buffer when non-null; returns the length of the name.
int Editor::StyleGetFont(int style, char *buffer) {
	vs.EnsureStyle(static_cast<size_t>(style));
	const char *name = vs.styles[style].fontName;
	const size_t len = name ? strlen(name) : 0;
	if (buffer) {
		if (name)
			memcpy(buffer, name, len + 1);
		else
			buffer[0] = '\0';
	}
	return static_cast<int>(len);
}

// Underline.
void Editor::StyleSetUnderline(int style, bool underline) {
	vs.EnsureStyle(static_cast<size_t>(style));
	vs.styles[style].underline = underline;
	InvalidateStyleRedraw();
}

bool Editor::StyleGetUnderline(int style) {
	vs.EnsureStyle(static_cast<size_t>(style));
	return vs.styles[style].underline;
}

// Case force: mixed, upper, lower, or camel.
void Editor::StyleSetCase(int style, CaseVisible caseVisible) {
	vs.EnsureStyle(static_cast<size_t>(style));
	vs.styles[style].caseForce = static_cast<Style::CaseForce>(caseVisible);
	InvalidateStyleRedraw();
}

CaseVisible Editor::StyleGetCase(int style) {
	vs.EnsureStyle(static_cast<size_t>(style));
	return static_cast<CaseVisible>(vs.styles[style].caseForce);
}

// Character set for the face. Changing it drops the case-fold cache.
void Editor::StyleSetCharacterSet(int style, CharacterSet characterSet) {
	vs.EnsureStyle(static_cast<size_t>(style));
	vs.styles[style].characterSet = characterSet;
	pdoc->SetCaseFolder(nullptr);
	InvalidateStyleRedraw();
}

CharacterSet Editor::StyleGetCharacterSet(int style) {
	vs.EnsureStyle(static_cast<size_t>(style));
	return vs.styles[style].characterSet;
}

// Visible text for this style. Invisible styles still take horizontal space unless combined with other settings.
void Editor::StyleSetVisible(int style, bool visible) {
	vs.EnsureStyle(static_cast<size_t>(style));
	vs.styles[style].visible = visible;
	InvalidateStyleRedraw();
}

bool Editor::StyleGetVisible(int style) {
	vs.EnsureStyle(static_cast<size_t>(style));
	return vs.styles[style].visible;
}

// When false, the style's text is protected from editing (selection and typing skip it where enforced).
void Editor::StyleSetChangeable(int style, bool changeable) {
	vs.EnsureStyle(static_cast<size_t>(style));
	vs.styles[style].changeable = changeable;
	InvalidateStyleRedraw();
}

bool Editor::StyleGetChangeable(int style) {
	vs.EnsureStyle(static_cast<size_t>(style));
	return vs.styles[style].changeable;
}

// Hotspot: clickable style that can raise hotspot notifications and use hotspot colours.
void Editor::StyleSetHotSpot(int style, bool hotspot) {
	vs.EnsureStyle(static_cast<size_t>(style));
	vs.styles[style].hotspot = hotspot;
	InvalidateStyleRedraw();
}

bool Editor::StyleGetHotSpot(int style) {
	vs.EnsureStyle(static_cast<size_t>(style));
	return vs.styles[style].hotspot;
}

// Ask the platform to treat the face as monospaced when measuring if true.
void Editor::StyleSetCheckMonospaced(int style, bool checkMonospaced) {
	vs.EnsureStyle(static_cast<size_t>(style));
	vs.styles[style].checkMonospaced = checkMonospaced;
	InvalidateStyleRedraw();
}

bool Editor::StyleGetCheckMonospaced(int style) {
	vs.EnsureStyle(static_cast<size_t>(style));
	return vs.styles[style].checkMonospaced;
}

// UTF-8 text drawn instead of the real characters when the style is invisible. Invalid UTF-8 is ignored; null clears.
void Editor::StyleSetInvisibleRepresentation(int style, const char *utf8) {
	vs.EnsureStyle(static_cast<size_t>(style));
	char *rep = vs.styles[style].invisibleRepresentation;
	if (!utf8) {
		*rep = 0;
		InvalidateStyleRedraw();
		return;
	}
	const int classified = UTF8Classify(utf8);
	if (!(classified & UTF8MaskInvalid)) {
		const int len = classified & UTF8MaskWidth;
		for (int i = 0; i < len && i < UTF8MaxBytes; i++)
			*rep++ = *utf8++;
	}
	*rep = 0;
	InvalidateStyleRedraw();
}

// Copies the invisible representation into buffer when non-null; returns its byte length.
int Editor::StyleGetInvisibleRepresentation(int style, char *buffer) {
	vs.EnsureStyle(static_cast<size_t>(style));
	const char *rep = vs.styles[style].invisibleRepresentation;
	const size_t len = strlen(rep);
	if (buffer)
		memcpy(buffer, rep, len + 1);
	return static_cast<int>(len);
}

// --- Element colours ---------------------------------------------------------

// Override the colour of a visual element (selection, whitespace, caret, …). Alpha is used when the element allows translucency.
void Editor::SetElementColour(Element element, int colourAlpha) {
	if (vs.SetElementColour(element, ColourRGBA(colourAlpha))) {
		InvalidateStyleRedraw();
	}
}

// Current override colour as colouralpha integer, or 0 when unset.
int Editor::GetElementColour(Element element) const {
	return vs.ElementColour(element).value_or(ColourRGBA()).AsInteger();
}

// Remove the override so the default or system colour is used again.
void Editor::ResetElementColour(Element element) {
	if (vs.ResetElement(element)) {
		InvalidateStyleRedraw();
	}
}

// True when an override colour is set for the element (not merely a base/default colour).
bool Editor::GetElementIsSet(Element element) const {
	return vs.ElementIsSet(element);
}

// True when the element accepts a translucent alpha channel.
bool Editor::GetElementAllowsTranslucent(Element element) const {
	return vs.ElementAllowsTranslucent(element);
}

// Base/default colour for the element before overrides.
int Editor::GetElementBaseColour(Element element) const {
	const auto it = vs.elementBaseColours.find(element);
	if (it == vs.elementBaseColours.end())
		return ColourRGBA().AsInteger();
	return it->second.value_or(ColourRGBA()).AsInteger();
}

// --- Font locale and measurement ---------------------------------------------

// Locale used when realising fonts (OpenType features, etc.). Null is ignored.
void Editor::SetFontLocale(const char *localeName) {
	if (!localeName)
		return;
	vs.SetFontLocaleName(localeName);
	InvalidateStyleRedraw();
}

// Copies the locale name into buffer when non-null; returns its length.
int Editor::GetFontLocale(char *buffer) const {
	const size_t len = vs.localeName.size();
	if (buffer) {
		memcpy(buffer, vs.localeName.c_str(), len + 1);
	}
	return static_cast<int>(len);
}

// Pixel width of text measured in the given style after refreshing style data.
// Useful for sizing the line-number margin. Returns 1 if no measurement surface is available.
long Editor::TextWidth(int style, std::string_view text) {
	RefreshStyleData();
	AutoSurface surface(this);
	if (surface) {
		return std::lround(surface->WidthText(vs.styles[style].font.get(), text));
	}
	return 1;
}

// How many line layouts to cache: None, Caret (default, one line), Page (visible + caret), or Document.
// Higher levels speed wrapping and scrolling at the cost of memory.
void Editor::SetLayoutCache(LineCache cacheMode) {
	if (cacheMode <= LineCache::Document) {
		view.llc.SetLevel(cacheMode);
	}
}

LineCache Editor::GetLayoutCache() const noexcept {
	return view.llc.GetLevel();
}

// Entries in the short-string width cache used while laying out lines.
void Editor::SetPositionCache(int size) {
	view.posCache->SetSize(static_cast<size_t>(size));
}

int Editor::GetPositionCache() const noexcept {
	return static_cast<int>(view.posCache->GetSize());
}

// Worker threads for layout. Large requests are clamped to a reasonable platform maximum.
void Editor::SetLayoutThreads(unsigned int threads) {
	view.SetLayoutThreads(threads);
}

unsigned int Editor::GetLayoutThreads() const noexcept {
	return view.GetLayoutThreads();
}

// Drawing phases: one phase (base only) or multiple (back then text/indicators). Multi-phase reduces overdraw artefacts.
void Editor::SetPhasesDraw(int phases) {
	if (view.SetPhasesDraw(phases))
		InvalidateStyleRedraw();
}

int Editor::GetPhasesDraw() const noexcept {
	return static_cast<int>(view.phasesDraw);
}

// Extra pixels above each line's text, increasing line height without changing the font size.
void Editor::SetExtraAscent(int extraAscent) {
	vs.extraAscent = extraAscent;
	InvalidateStyleRedraw();
}

int Editor::GetExtraAscent() const noexcept {
	return vs.extraAscent;
}

// Extra pixels below each line's text.
void Editor::SetExtraDescent(int extraDescent) {
	vs.extraDescent = extraDescent;
	InvalidateStyleRedraw();
}

int Editor::GetExtraDescent() const noexcept {
	return vs.extraDescent;
}

// Pixel size of the next RGBA image registered for markers or autocomplete.
void Editor::RGBAImageSetWidth(int width) {
	sizeRGBAImage.x = static_cast<XYPOSITION>(width);
}

void Editor::RGBAImageSetHeight(int height) {
	sizeRGBAImage.y = static_cast<XYPOSITION>(height);
}

// Scale percent for the next RGBA image (100 = 1:1).
void Editor::RGBAImageSetScale(int scalePercent) {
	scaleRGBAImage = static_cast<float>(scalePercent);
}

// Bidirectional layout mode. Default is Disabled. Stores the mode and invalidates layout so measurement and paint can react. Platform subclasses that cannot lay out bidi text may override this virtual method to leave the mode disabled (or to accept only modes they implement). Full screen-line reordering is not on the current roadmap; the flag and shaped-run path stay so a later platform can turn it on.
void Editor::SetBidirectional(Bidirectional bidirectional_) {
	if (bidirectional == bidirectional_)
		return;
	bidirectional = bidirectional_;
	// Layout and surface mode depend on the flag; rebuild caches and redraw.
	InvalidateStyleRedraw();
}

Bidirectional Editor::GetBidirectional() const noexcept {
	return bidirectional;
}

// --- Whitespace view ---------------------------------------------------------

// How space and tab are drawn: Invisible (default), VisibleAlways, VisibleAfterIndent, or VisibleOnlyInIndent.
void Editor::SetViewWS(WhiteSpace viewWS) {
	vs.viewWhitespace = viewWS;
	Redraw();
}

WhiteSpace Editor::GetViewWS() const noexcept {
	return vs.viewWhitespace;
}

// Colour of visible whitespace marks when useSetting is true; otherwise clear the override.
void Editor::SetWhitespaceFore(bool useSetting, int rgb) {
	if (vs.SetElementColourOptional(Element::WhiteSpace, useSetting, rgb)) {
		InvalidateStyleRedraw();
	}
}

// Background behind whitespace marks when useSetting is true; otherwise clear the override.
void Editor::SetWhitespaceBack(bool useSetting, int rgb) {
	if (vs.SetElementColourOptional(Element::WhiteSpaceBack, useSetting, rgb)) {
		InvalidateStyleRedraw();
	}
}

// Size of the dots used for visible space characters.
void Editor::SetWhitespaceSize(int size) {
	vs.whitespaceSize = size;
	Redraw();
}

int Editor::GetWhitespaceSize() const noexcept {
	return vs.whitespaceSize;
}

// --- Selection colours (layer and EOL fill live elsewhere) -------------------

// Selection (and additional-selection) text colour when useSetting is true; otherwise clear both overrides.
void Editor::SetSelFore(bool useSetting, int rgb) {
	vs.elementColours[Element::SelectionText] = OptionalColour(useSetting, rgb);
	vs.elementColours[Element::SelectionAdditionalText] = OptionalColour(useSetting, rgb);
	InvalidateStyleRedraw();
}

// Selection (and additional-selection) background when useSetting is true; otherwise clear both overrides.
void Editor::SetSelBack(bool useSetting, int rgb) {
	if (useSetting) {
		vs.SetElementRGB(Element::SelectionBack, rgb);
		vs.SetElementRGB(Element::SelectionAdditionalBack, rgb);
	} else {
		vs.ResetElement(Element::SelectionBack);
		vs.ResetElement(Element::SelectionAdditionalBack);
	}
	InvalidateStyleRedraw();
}

// Selection translucency. Alpha::NoAlpha places the selection on the base layer; other values use OverText and set alpha on selection backgrounds.
void Editor::SetSelAlpha(int alpha) {
	const Layer layerNew = (static_cast<Alpha>(alpha) == Alpha::NoAlpha) ? Layer::Base : Layer::OverText;
	if (vs.selection.layer != layerNew) {
		vs.selection.layer = layerNew;
		UpdateBaseElements();
	}
	vs.SetElementAlpha(Element::SelectionBack, alpha);
	vs.SetElementAlpha(Element::SelectionAdditionalBack, alpha);
	vs.SetElementAlpha(Element::SelectionSecondaryBack, alpha);
	vs.SetElementAlpha(Element::SelectionInactiveBack, alpha);
	InvalidateStyleRedraw();
}

// Current selection alpha, or Alpha::NoAlpha when the selection is on the base layer.
int Editor::GetSelAlpha() const {
	if (vs.selection.layer == Layer::Base)
		return static_cast<int>(Alpha::NoAlpha);
	return vs.ElementColourForced(Element::SelectionBack).GetAlpha();
}

// Foreground of additional selections (multi-select).
void Editor::SetAdditionalSelFore(int rgb) {
	vs.elementColours[Element::SelectionAdditionalText] = ColourRGBA::FromIpRGB(rgb);
	InvalidateStyleRedraw();
}

// Background of additional selections.
void Editor::SetAdditionalSelBack(int rgb) {
	vs.SetElementRGB(Element::SelectionAdditionalBack, rgb);
	InvalidateStyleRedraw();
}

// Alpha of additional selection background.
void Editor::SetAdditionalSelAlpha(int alpha) {
	vs.SetElementAlpha(Element::SelectionAdditionalBack, alpha);
	InvalidateStyleRedraw();
}

int Editor::GetAdditionalSelAlpha() const {
	if (vs.selection.layer == Layer::Base)
		return static_cast<int>(Alpha::NoAlpha);
	return vs.ElementColourForced(Element::SelectionAdditionalBack).GetAlpha();
}

// --- Highlight guide, edges, zoom, extended styles ---------------------------

// Column of the vertical indentation guide highlight, or 0 to turn it off. Redraws when the column changes or is forced on.
void Editor::SetHighlightGuide(int column) {
	if ((highlightGuideColumn != column) || (column > 0)) {
		highlightGuideColumn = column;
		Redraw();
	}
}

int Editor::GetHighlightGuide() const noexcept {
	return highlightGuideColumn;
}

// Long-line marker mode: None, Line (vertical line at the edge column), Background, or MultiLine.
void Editor::SetEdgeMode(EdgeVisualStyle edgeMode) {
	vs.edgeState = edgeMode;
	InvalidateStyleRedraw();
}

EdgeVisualStyle Editor::GetEdgeMode() const noexcept {
	return vs.edgeState;
}

// Colour of the single long-line edge (Line or Background modes).
void Editor::SetEdgeColour(int rgb) {
	vs.theEdge.colour = ColourRGBA::FromIpRGB(rgb);
	InvalidateStyleRedraw();
}

int Editor::GetEdgeColour() const noexcept {
	return vs.theEdge.colour.OpaqueRGB();
}

// Add a multi-edge vertical line at column with colour. Use with EdgeVisualStyle::MultiLine.
void Editor::MultiEdgeAddLine(int column, int rgb) {
	vs.AddMultiEdge(column, ColourRGBA::FromIpRGB(rgb));
	InvalidateStyleRedraw();
}

// Remove all multi-edge lines.
void Editor::MultiEdgeClearAll() {
	std::vector<EdgeProperties>().swap(vs.theMultiEdge);
	InvalidateStyleRedraw();
}

// Zoom in whole points added to every style size. Displayed size never goes below 2 points.
// ZoomIn/ZoomOut commands clamp to about -10..+60; this setter accepts any int. Change notifies the host via NotifyZoom.
void Editor::SetZoom(int zoomInPoints) {
	if (SetAppearance(vs.zoomLevel, zoomInPoints)) {
		NotifyZoom();
	}
}

// Current zoom in points (may be negative).
int Editor::GetZoom() const noexcept {
	return vs.zoomLevel;
}

// Drop all extended (beyond 0..255) style numbers so they can be reallocated.
void Editor::ReleaseAllExtendedStyles() {
	vs.ReleaseAllExtendedStyles();
}

// Reserve numberStyles consecutive extended style numbers; returns the first index.
int Editor::AllocateExtendedStyles(int numberStyles) {
	return vs.AllocateExtendedStyles(numberStyles);
}

// --- Private idle / paint styling helpers ------------------------------------

// Style to a position within the view. If the style at the end of the last line changes, style the rest of the window (multiline constructs).
void Editor::StyleToPositionInView(Sci::Position pos) {
	Sci::Position endWindow = PositionAfterArea(GetClientDrawingRectangle());
	if (pos > endWindow)
		pos = endWindow;
	const int styleAtEnd = pdoc->StyleIndexAt(pos - 1);
	pdoc->EnsureStyledTo(pos);
	if ((endWindow > pos) && (styleAtEnd != pdoc->StyleIndexAt(pos - 1))) {
		// Style at end of line changed so is multi-line change like starting a comment
		// so require rest of window to be styled.
		DiscardOverdraw();	// Prepared bitmaps may be invalid
		// DiscardOverdraw may have truncated client drawing area so recalculate endWindow
		endWindow = PositionAfterArea(GetClientDrawingRectangle());
		pdoc->EnsureStyledTo(endWindow);
	}
}

// Cap how far styling runs in one burst so interaction stays smooth. Scrolling allows less time.
Sci::Position Editor::PositionAfterMaxStyling(Sci::Position posMax, bool scrolling) const {
	if (SynchronousStylingToVisible()) {
		// Both states do not limit styling
		return posMax;
	}

	// Try to keep time taken by styling reasonable so interaction remains smooth.
	// When scrolling, allow less time to ensure responsive
	const double secondsAllowed = scrolling ? 0.005 : 0.02;

	const size_t actionsInAllowedTime = std::clamp<Sci::Line>(
		pdoc->durationStyleOneByte.ActionsInAllowedTime(secondsAllowed),
		0x200, 0x20000);
	const Sci::Line lineLast = pdoc->LineFromPositionAfter(pdoc->SciLineFromPosition(pdoc->GetEndStyled()), actionsInAllowedTime);
	const Sci::Line stylingMaxLine = std::min(lineLast, pdoc->LinesTotal());

	return std::min(pdoc->LineStart(stylingMaxLine), posMax);
}

// Request idle work when more of the document still needs styling under the current idle mode.
void Editor::StartIdleStyling(bool truncatedLastStyling) {
	if (AnyOf(idleStyling, IdleStyling::All, IdleStyling::AfterVisible)) {
		if (pdoc->GetEndStyled() < pdoc->Length()) {
			// Style remainder of document in idle time
			needIdleStyling = true;
		}
	} else if (truncatedLastStyling) {
		needIdleStyling = true;
	}

	if (needIdleStyling) {
		SetIdle(true);
	}
}

// Style for a paint area but bound the amount of work; schedule idle for the rest when truncated.
void Editor::StyleAreaBounded(PRectangle rcArea, bool scrolling) {
	const Sci::Position posAfterArea = PositionAfterArea(rcArea);
	const Sci::Position posAfterMax = PositionAfterMaxStyling(posAfterArea, scrolling);
	if (posAfterMax < posAfterArea) {
		// Idle styling may be performed before current visible area
		// Style a bit now then style further in idle time
		pdoc->StyleToAdjustingLineDuration(posAfterMax);
	} else {
		// Can style all wanted now.
		StyleToPositionInView(posAfterArea);
	}
	StartIdleStyling(posAfterMax < posAfterArea);
}

// Idle-time styling step toward the visible area or the whole document depending on idleStyling.
void Editor::IdleStyle() {
	const Sci::Position posAfterArea = PositionAfterArea(GetClientRectangle());
	const Sci::Position endGoal = (idleStyling >= IdleStyling::AfterVisible) ?
		pdoc->Length() : posAfterArea;
	const Sci::Position posAfterMax = PositionAfterMaxStyling(endGoal, false);
	pdoc->StyleToAdjustingLineDuration(posAfterMax);
	if (pdoc->GetEndStyled() >= endGoal) {
		needIdleStyling = false;
	}
}
