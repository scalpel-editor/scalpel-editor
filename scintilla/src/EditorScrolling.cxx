// Scintilla source code edit control
/** @file EditorScrolling.cxx
 ** View scrolling, hit testing, and scrollbar configuration.
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

using namespace Scintilla;
using namespace Scintilla::Internal;

// First document line shown at the top of the view.
Sci::Line Editor::GetFirstVisibleLine() const noexcept {
	return topLine;
}

// Scroll so the given document line is the first visible line.
void Editor::SetFirstVisibleLine(Sci::Line line) {
	ScrollTo(line);
}

// Horizontal pixel offset of the view.
void Editor::SetXOffset(int offset) {
	xOffset = offset;
	ContainerNeedsUpdate(Update::HScroll);
	SetHorizontalScrollPos();
	Redraw();
}

// Horizontal pixel offset of the view.
int Editor::GetXOffset() const noexcept {
	return xOffset;
}

// Scroll by columns (wParam) and lines (lParam) relative to the current origin.
void Editor::LineScroll(Sci::Position columns, Sci::Line lines) {
	ScrollTo(topLine + lines);
	HorizontalScrollTo(xOffset + static_cast<int>(columns * vs.spaceWidth));
}

// Scroll so that docLine's display sub-line is at the top (or record wrap intent).
void Editor::ScrollVertical(Sci::Line docLine, Sci::Line displayLine) {
	if (Wrapping()) {
		scrollToAfterWrap = { docLine, displayLine };
	} else {
		scrollToAfterWrap.reset();
	}
	ScrollTo(pcs->DisplayFromDocSub(docLine, displayLine));
}

// Show or hide the horizontal scrollbar and reconfigure scroll ranges.
void Editor::SetHScrollBar(bool visible) {
	if (horizontalScrollBarVisible != visible) {
		horizontalScrollBarVisible = visible;
		SetScrollBars();
		ReconfigureScrollBars();
	}
}

// True when the horizontal scrollbar is enabled.
bool Editor::GetHScrollBar() const noexcept {
	return horizontalScrollBarVisible;
}

// Show or hide the vertical scrollbar and reconfigure scroll ranges.
void Editor::SetVScrollBar(bool visible) {
	if (verticalScrollBarVisible != visible) {
		verticalScrollBarVisible = visible;
		SetScrollBars();
		ReconfigureScrollBars();
		if (verticalScrollBarVisible)
			SetVerticalScrollPos();
	}
}

// True when the vertical scrollbar is enabled.
bool Editor::GetVScrollBar() const noexcept {
	return verticalScrollBarVisible;
}

// Assumed document width in pixels for horizontal scrolling when not tracking line width.
void Editor::SetScrollWidth(int width) {
	PLATFORM_ASSERT(width > 0);
	if ((width > 0) && (width != scrollWidth)) {
		view.lineWidthMaxSeen = 0;
		scrollWidth = width;
		SetScrollBars();
	}
}

// Current assumed scroll width in pixels.
int Editor::GetScrollWidth() const noexcept {
	return scrollWidth;
}

// When true, scroll width grows with the widest measured line instead of a fixed width.
void Editor::SetScrollWidthTracking(bool tracking) {
	trackLineWidth = tracking;
}

// True when scroll width tracks measured line widths.
bool Editor::GetScrollWidthTracking() const noexcept {
	return trackLineWidth;
}

// When true (default), the last line can scroll only to the bottom of the view.
// When false, the last line may scroll to the top.
void Editor::SetEndAtLastLine(bool endAtLast) {
	if (endAtLastLine != endAtLast) {
		endAtLastLine = endAtLast;
		SetScrollBars();
	}
}

// Whether scrolling stops with the last line at the bottom of the view.
bool Editor::GetEndAtLastLine() const noexcept {
	return endAtLastLine;
}

// Pixel height of one text line after refreshing style data.
int Editor::TextHeightPixels() {
	RefreshStyleData();
	return vs.lineHeight;
}

// Policy used by EnsureVisibleEnforcePolicy: VisiblePolicy::Slop / Strict and a line slop.
void Editor::SetVisiblePolicy(uptr_t policy, sptr_t slop) {
	visiblePolicy = VisiblePolicySlop(policy, slop);
}

// View-relative x pixel of pos, or 0 when pos is negative.
int Editor::PointXFromPosition(Sci::Position pos) {
	if (pos < 0) return 0;
	const Point pt = LocationFromPosition(pos);
	// Convert to view-relative
	return static_cast<int>(pt.x) - vs.textStart + vs.fixedColumnWidth;
}

// View-relative y pixel of pos, or 0 when pos is negative.
int Editor::PointYFromPosition(Sci::Position pos) {
	if (pos < 0) return 0;
	return static_cast<int>(LocationFromPosition(pos).y);
}

