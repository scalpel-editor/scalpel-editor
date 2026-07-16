// Scintilla source code edit control
/** @file EditorMargins.cxx
 ** Margin widths, types, masks, sensitivity, text, styles, and fold-margin colours.
 **
 ** There may be multiple numbered margins to the left of the text, plus blank gaps on each side of the text. Initially MaxMargin+1 slots exist (indices 0..MaxMargin). Out-of-range margin numbers have no effect on set and return zero-ish values on get.
 **
 ** Defaults: margin 0 is line numbers at width 0 (hidden); margin 1 is non-folding symbols at 16 pixels; margin 2 is a symbol margin at width 0. Blank left and right text gaps default to one pixel each. All margins start insensitive to clicks. Fold-margin colours start unset so the platform can supply its own face and highlight colours.
 **
 ** Markers that do not appear in any visible margin's mask are drawn as text-line background changes instead. MaskFolders and MaskHistory select the fold and change-history marker groups. When a mask does not include MaskFolders, that margin's background follows the line-number style.
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
#include "ScintillaMessages.h"
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

// True when margin indexes a margin slot in the view style.
bool Editor::ValidMargin(size_t margin) const noexcept {
	return margin < vs.ms.size();
}

// Allocate margin slots, or shrink the slot list. Growing appends default-width-zero margins. Shrinking drops trailing slots; if a removed slot was visible, layout must refresh (this invalidates on any size change).
void Editor::SetMargins(size_t margins) {
	if (margins >= 1000)
		return;
	if (margins == vs.ms.size())
		return;
	vs.ms.resize(margins);
	InvalidateStyleRedraw();
}

// Number of margin slots.
size_t Editor::GetMargins() const noexcept {
	return vs.ms.size();
}

// Margin type: Symbol, Number, Back/Fore (background from default style colours), Text / RText (application text, right-justified for RText), or Colour (background from SetMarginBackN). Conventionally 0 is line numbers and the next slots are symbols.
void Editor::SetMarginTypeN(size_t margin, MarginType marginType) {
	if (ValidMargin(margin)) {
		vs.ms[margin].style = marginType;
		InvalidateStyleRedraw();
	}
}

// Type of margin (Symbol, Number, Text, …).
MarginType Editor::GetMarginTypeN(size_t margin) const noexcept {
	if (ValidMargin(margin))
		return vs.ms[margin].style;
	// Invalid margin: message path historically returned 0 (Symbol).
	return static_cast<MarginType>(0);
}

// Pixel width of a numbered margin. Zero hides the margin completely. Unchanged width skips redraw. Line-number widths should fit the document; TextWidth on the line-number style is a common measure.
void Editor::SetMarginWidthN(size_t margin, int pixelWidth) {
	if (ValidMargin(margin)) {
		if (vs.ms[margin].width != pixelWidth) {
			lastXChosen += pixelWidth - vs.ms[margin].width;
			vs.ms[margin].width = pixelWidth;
			InvalidateStyleRedraw();
		}
	}
}

// Pixel width of margin; 0 means hidden.
int Editor::GetMarginWidthN(size_t margin) const noexcept {
	if (ValidMargin(margin))
		return vs.ms[margin].width;
	return 0;
}

// Which of the 32 marker bits this margin may show. Markers outside every visible mask draw as line background. Default for margin 1 is ~MaskFolders; a fold margin uses MaskFolders.
void Editor::SetMarginMaskN(size_t margin, int mask) {
	if (ValidMargin(margin)) {
		vs.ms[margin].mask = mask;
		InvalidateStyleRedraw();
	}
}

// Marker mask drawn in this margin.
int Editor::GetMarginMaskN(size_t margin) const noexcept {
	if (ValidMargin(margin))
		return vs.ms[margin].mask;
	return 0;
}

// Sensitive margins send MarginClick / MarginRightClick; insensitive ones select lines (or a wrap sub-line when MarginOption::SubLineSelect is set). Default is insensitive.
void Editor::SetMarginSensitiveN(size_t margin, bool sensitive) {
	if (ValidMargin(margin)) {
		vs.ms[margin].sensitive = sensitive;
		InvalidateStyleRedraw();
	}
}

// True when the margin receives mouse clicks.
bool Editor::GetMarginSensitiveN(size_t margin) const noexcept {
	if (ValidMargin(margin))
		return vs.ms[margin].sensitive;
	return false;
}

// Mouse cursor over this margin. Default is reverse arrow; CursorShape::Arrow is the usual alternative.
void Editor::SetMarginCursorN(size_t margin, CursorShape cursor) {
	if (ValidMargin(margin))
		vs.ms[margin].cursor = cursor;
}

// Cursor shape shown over the margin.
CursorShape Editor::GetMarginCursorN(size_t margin) const noexcept {
	if (ValidMargin(margin))
		return vs.ms[margin].cursor;
	// Invalid margin: message path historically returned 0.
	return static_cast<CursorShape>(0);
}

// Background for MarginType::Colour margins.
void Editor::SetMarginBackN(size_t margin, int rgb) {
	if (ValidMargin(margin)) {
		vs.ms[margin].back = ColourRGBA::FromIpRGB(rgb);
		InvalidateStyleRedraw();
	}
}

// Background colour for Colour margins.
int Editor::GetMarginBackN(size_t margin) const noexcept {
	if (ValidMargin(margin))
		return vs.ms[margin].back.OpaqueRGB();
	return 0;
}

// Blank gap between the numbered margins and the text (left) or after the text (right). Default is one pixel each.
void Editor::SetMarginLeft(int pixelWidth) {
	lastXChosen += pixelWidth - vs.leftMarginWidth;
	vs.leftMarginWidth = pixelWidth;
	InvalidateStyleRedraw();
}

// Blank pixel gap left of the text.
int Editor::GetMarginLeft() const noexcept {
	return vs.leftMarginWidth;
}

// Blank pixel gap right of the text.
void Editor::SetMarginRight(int pixelWidth) {
	vs.rightMarginWidth = pixelWidth;
	InvalidateStyleRedraw();
}

// Blank pixel gap right of the text.
int Editor::GetMarginRight() const noexcept {
	return vs.rightMarginWidth;
}

// Fold-margin fill and highlight. useSetting false clears the override so the platform default applies again.
void Editor::SetFoldMarginColour(bool useSetting, int rgb) {
	vs.foldmarginColour = OptionalColour(useSetting, rgb);
	InvalidateStyleRedraw();
}

// Highlight colour of the fold margin, or platform default when useSetting is false.
void Editor::SetFoldMarginHiColour(bool useSetting, int rgb) {
	vs.foldmarginHighlightColour = OptionalColour(useSetting, rgb);
	InvalidateStyleRedraw();
}

// Per-line text for MarginType::Text / RText. Setting text notifies ChangeMargin. Active style attributes in text margins are font, size/sizeFractional, bold/weight, italics, fore, back, and characterSet only.
void Editor::MarginSetText(Sci::Line line, const char *text) {
	pdoc->MarginSetText(line, text);
}

// Application text for a Text/RText margin line.
std::string Editor::MarginGetText(Sci::Line line) const {
	return std::string(pdoc->MarginStyledText(line).AsView());
}

// One style for the whole margin line (before style offset is applied at look-up).
void Editor::MarginSetStyle(Sci::Line line, int style) {
	pdoc->MarginSetStyle(line, style);
}

// Style index for margin text on line, or 0.
int Editor::MarginGetStyle(Sci::Line line) const noexcept {
	return static_cast<int>(pdoc->MarginStyledText(line).style);
}

// Per-byte style indices for margin text, same idea as SetStylingEx.
void Editor::MarginSetStyles(Sci::Line line, const unsigned char *styles) {
	pdoc->MarginSetStyles(line, styles);
}

// Copy per-character style bytes into buffer. When the line uses one shared style, there is no style array: returns 0 and, if buffer is non-null and the line has text, writes a single 0 byte so the message path matches the old BytesResult(null pointer, length) behavior. When per-character styles exist, returns their length and copies that many bytes (no NUL). buffer may be null to measure only.
Sci::Position Editor::MarginGetStyles(Sci::Line line, char *buffer) const {
	const StyledText st = pdoc->MarginStyledText(line);
	if (buffer && st.length > 0) {
		if (st.styles)
			std::memcpy(buffer, st.styles, st.length);
		else
			*buffer = 0;
	}
	return st.styles ? static_cast<Sci::Position>(st.length) : 0;
}

// Clear all per-line margin text and styles.
void Editor::MarginTextClearAll() {
	pdoc->MarginClearAll();
}

// Added to style numbers before looking up margin styles so they stay off lexer styles. Call AllocateExtendedStyles first and pass its result as the offset (for example 256 yields margin styles 256..511).
void Editor::MarginSetStyleOffset(int style) {
	vs.marginStyleOffset = style;
	InvalidateStyleRedraw();
}

// Style index base for margin text styles.
int Editor::MarginGetStyleOffset() const noexcept {
	return vs.marginStyleOffset;
}

// MarginOption::SubLineSelect selects only the wrap sub-line under the click; default MarginOption::None selects the whole wrapped line.
void Editor::SetMarginOptions(MarginOption options) {
	marginOptions = options;
}

// MarginOptions for sub-line selection behaviour.
MarginOption Editor::GetMarginOptions() const noexcept {
	return marginOptions;
}
