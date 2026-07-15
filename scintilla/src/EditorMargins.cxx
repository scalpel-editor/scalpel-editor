// Scintilla source code edit control
/** @file EditorMargins.cxx
 ** Margin widths, types, masks, sensitivity, text, styles, and fold-margin colours.
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
bool Editor::ValidMargin(uptr_t margin) const noexcept {
	return margin < vs.ms.size();
}

// Number of margin slots. Initially MaxMargin+1 (0..4); resize with SetMargins.
void Editor::SetMargins(size_t margins) {
	if (margins < 1000)
		vs.ms.resize(margins);
}

size_t Editor::GetMargins() const noexcept {
	return vs.ms.size();
}

// Margin type: symbol, line number, text, right-justified text, or colour background.
void Editor::SetMarginTypeN(size_t margin, MarginType marginType) {
	if (ValidMargin(margin)) {
		vs.ms[margin].style = marginType;
		InvalidateStyleRedraw();
	}
}

MarginType Editor::GetMarginTypeN(size_t margin) const noexcept {
	if (ValidMargin(margin))
		return vs.ms[margin].style;
	// Invalid margin: message path historically returned 0 (Symbol).
	return static_cast<MarginType>(0);
}

// Pixel width of a numbered margin. Zero hides the margin. Unchanged width skips redraw.
void Editor::SetMarginWidthN(size_t margin, int pixelWidth) {
	if (ValidMargin(margin)) {
		if (vs.ms[margin].width != pixelWidth) {
			lastXChosen += pixelWidth - vs.ms[margin].width;
			vs.ms[margin].width = pixelWidth;
			InvalidateStyleRedraw();
		}
	}
}

int Editor::GetMarginWidthN(size_t margin) const noexcept {
	if (ValidMargin(margin))
		return vs.ms[margin].width;
	return 0;
}

// Which marker bits this margin may show. Markers outside every mask draw as line background.
void Editor::SetMarginMaskN(size_t margin, int mask) {
	if (ValidMargin(margin)) {
		vs.ms[margin].mask = mask;
		InvalidateStyleRedraw();
	}
}

int Editor::GetMarginMaskN(size_t margin) const noexcept {
	if (ValidMargin(margin))
		return vs.ms[margin].mask;
	return 0;
}

// Sensitive margins send MarginClick / MarginRightClick; insensitive ones select lines.
void Editor::SetMarginSensitiveN(size_t margin, bool sensitive) {
	if (ValidMargin(margin)) {
		vs.ms[margin].sensitive = sensitive;
		InvalidateStyleRedraw();
	}
}

bool Editor::GetMarginSensitiveN(size_t margin) const noexcept {
	if (ValidMargin(margin))
		return vs.ms[margin].sensitive;
	return false;
}

// Mouse cursor shape over this margin (arrow or reverse arrow by convention).
void Editor::SetMarginCursorN(size_t margin, CursorShape cursor) {
	if (ValidMargin(margin))
		vs.ms[margin].cursor = cursor;
}

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

int Editor::GetMarginBackN(size_t margin) const noexcept {
	if (ValidMargin(margin))
		return vs.ms[margin].back.OpaqueRGB();
	return 0;
}

// Blank gap between the numbered margins and the text (left) or after the text (right).
void Editor::SetMarginLeft(int pixelWidth) {
	lastXChosen += pixelWidth - vs.leftMarginWidth;
	vs.leftMarginWidth = pixelWidth;
	InvalidateStyleRedraw();
}

int Editor::GetMarginLeft() const noexcept {
	return vs.leftMarginWidth;
}

void Editor::SetMarginRight(int pixelWidth) {
	vs.rightMarginWidth = pixelWidth;
	InvalidateStyleRedraw();
}

int Editor::GetMarginRight() const noexcept {
	return vs.rightMarginWidth;
}

// Fold-margin fill and highlight. useSetting false clears the override.
void Editor::SetFoldMarginColour(bool useSetting, int rgb) {
	vs.foldmarginColour = OptionalColour(useSetting ? 1 : 0, rgb);
	InvalidateStyleRedraw();
}

void Editor::SetFoldMarginHiColour(bool useSetting, int rgb) {
	vs.foldmarginHighlightColour = OptionalColour(useSetting ? 1 : 0, rgb);
	InvalidateStyleRedraw();
}

// Per-line text for MarginType::Text / RText margins. ChangeMargin notifies the host.
void Editor::MarginSetText(Sci::Line line, const char *text) {
	pdoc->MarginSetText(line, text);
}

std::string Editor::MarginGetText(Sci::Line line) const {
	return std::string(pdoc->MarginStyledText(line).AsView());
}

void Editor::MarginSetStyle(Sci::Line line, int style) {
	pdoc->MarginSetStyle(line, style);
}

int Editor::MarginGetStyle(Sci::Line line) const noexcept {
	return static_cast<int>(pdoc->MarginStyledText(line).style);
}

void Editor::MarginSetStyles(Sci::Line line, const unsigned char *styles) {
	pdoc->MarginSetStyles(line, styles);
}

// Per-character style bytes for a margin line (empty when the line has one style).
std::string Editor::MarginGetStyles(Sci::Line line) const {
	const StyledText st = pdoc->MarginStyledText(line);
	if (!st.styles)
		return {};
	return std::string(reinterpret_cast<const char *>(st.styles), st.length);
}

void Editor::MarginTextClearAll() {
	pdoc->MarginClearAll();
}

// Added to style numbers before looking up margin styles (keeps them off lexer styles).
void Editor::MarginSetStyleOffset(int style) {
	vs.marginStyleOffset = style;
	InvalidateStyleRedraw();
}

int Editor::MarginGetStyleOffset() const noexcept {
	return vs.marginStyleOffset;
}

// MarginOption::SubLineSelect selects one wrap sub-line when clicking the margin.
void Editor::SetMarginOptions(MarginOption options) {
	marginOptions = options;
}

MarginOption Editor::GetMarginOptions() const noexcept {
	return marginOptions;
}
