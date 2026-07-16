// Scintilla source code edit control
/** @file EditorWrapping.cxx
 ** Line-wrapping concern for the editor: wrap mode and appearance, the
 ** deferred wrap queue, laying out wrapped lines, and the wrap-count query.
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

bool Editor::Wrapping() const noexcept {
	return vs.wrap.state != Wrap::None;
}

// Sets how lines wider than the window wrap: Word breaks on word or style
// boundaries, Char between any characters, Whitespace on whitespace, and None
// disables wrapping. Char wrapping suits languages that do not separate words
// with spaces. When a single word is wider than the window the break falls
// after the last character that fits. The horizontal scroll bar is hidden while
// wrapping is on.
void Editor::SetWrapMode(Wrap wrapMode) {
	if (vs.SetWrapState(wrapMode)) {
		xOffset = 0;
		ContainerNeedsUpdate(Update::HScroll);
		InvalidateStyleRedraw();
		ReconfigureScrollBars();
	}
}

Wrap Editor::GetWrapMode() const noexcept {
	return vs.wrap.state;
}

// Selects the visual flags (small arrows) drawn to indicate a wrapped line.
// The bits combine WrapVisualFlag values: End draws a flag at the end of a
// subline, Start draws one at the start of the next subline (which then keeps
// at least one character of indent to make room for it), and Margin draws one
// in the line-number margin.
void Editor::SetWrapVisualFlags(WrapVisualFlag wrapVisualFlags) {
	if (vs.SetWrapVisualFlags(wrapVisualFlags)) {
		InvalidateStyleRedraw();
		ReconfigureScrollBars();
	}
}

WrapVisualFlag Editor::GetWrapVisualFlags() const noexcept {
	return vs.wrap.visualFlags;
}

// Selects whether the wrap visual flags are drawn near the window border
// (Default) or near the text. Bits set for End or Start place that flag by the
// text.
void Editor::SetWrapVisualFlagsLocation(WrapVisualLocation wrapVisualFlagsLocation) {
	if (vs.SetWrapVisualFlagsLocation(wrapVisualFlagsLocation)) {
		InvalidateStyleRedraw();
	}
}

WrapVisualLocation Editor::GetWrapVisualFlagsLocation() const noexcept {
	return vs.wrap.visualFlagsLocation;
}

// Sets the indent of wrapped sublines, measured in average character widths of
// the default style. Values below 0 or very large values may look wrong. The
// indent is independent of the visual flags, but a Start visual flag forces an
// indent of at least 1.
void Editor::SetWrapStartIndent(int wrapStartIndent) {
	if (vs.SetWrapVisualStartIndent(wrapStartIndent)) {
		InvalidateStyleRedraw();
		ReconfigureScrollBars();
	}
}

int Editor::GetWrapStartIndent() const noexcept {
	return vs.wrap.visualStartIndent;
}

// Selects how wrapped sublines are indented: Fixed aligns them to the left of
// the window plus the amount from SetWrapStartIndent, Same aligns to the first
// subline's indent, Indent adds one more indent level, and DeepIndent adds two.
// The default is Fixed.
void Editor::SetWrapIndentMode(WrapIndentMode wrapIndentMode) {
	if (vs.SetWrapIndentMode(wrapIndentMode)) {
		InvalidateStyleRedraw();
		ReconfigureScrollBars();
	}
}

WrapIndentMode Editor::GetWrapIndentMode() const noexcept {
	return vs.wrap.indentMode;
}

// Wrapping is deferred: a change queues the affected lines here, and the actual
// layout happens later, during idle and paint, in WrapLines. Batching this way
// lets a set of changes be wrapped and displayed once. A consequence is that a
// scroll requested right after loading runs before wrapping and lands on the
// wrong line; wait for the first Painted notification before scrolling.
void Editor::NeedWrapping(Sci::Line docLineStart, Sci::Line docLineEnd) {
//Platform::DebugPrintf("\nNeedWrapping: %0d..%0d\n", docLineStart, docLineEnd);
	if (wrapPending.AddRange(docLineStart, docLineEnd)) {
		view.llc.Invalidate(LineLayout::ValidLevel::positions);
	}
	// Wrap lines during idle.
	if (Wrapping() && wrapPending.NeedsWrap()) {
		SetIdle(true);
	}
}

bool Editor::WrapOneLine(Surface *surface, Sci::Line lineToWrap) {
	std::shared_ptr<LineLayout> ll = view.RetrieveLineLayout(lineToWrap, *this);
	int linesWrapped = 1;
	if (ll) {
		view.LayoutLine(*this, surface, vs, ll.get(), wrapWidth);
		linesWrapped = ll->lines;
	}
	if (vs.annotationVisible != AnnotationVisible::Hidden) {
		linesWrapped += pdoc->AnnotationLines(lineToWrap);
	}
	return pcs->SetHeight(lineToWrap, linesWrapped);
}

namespace {

// Lines less than lengthToMultiThread are laid out in blocks in parallel.
// Longer lines are multi-threaded inside LayoutLine.
// This allows faster processing when lines differ greatly in length and thus time to lay out.
constexpr Sci::Position lengthToMultiThread = 4000;

}

bool Editor::WrapBlock(Surface *surface, Sci::Line lineToWrap, Sci::Line lineToWrapEnd) {

	const size_t linesBeingWrapped = static_cast<size_t>(lineToWrapEnd - lineToWrap);

	std::vector<int> linesAfterWrap(linesBeingWrapped);

	size_t threads = std::min<size_t>(linesBeingWrapped, view.maxLayoutThreads);
	if (!surface->SupportsFeature(Supports::ThreadSafeMeasureWidths)) {
		threads = 1;
	}

	const bool multiThreaded = threads > 1;

	ElapsedPeriod epWrapping;

	// Wrap all the short lines in multiple threads

	// If only 1 thread needed then use the main thread, else spin up multiple
	const std::launch policy = multiThreaded ? std::launch::async : std::launch::deferred;

	std::atomic<size_t> nextIndex = 0;

	// Lines that are less likely to be re-examined should not be read from or written to the cache.
	const SignificantLines significantLines {
		pdoc->SciLineFromPosition(sel.MainCaret()),
		pcs->DocFromDisplay(topLine),
		LinesOnScreen() + 1,
		view.llc.GetLevel(),
	};

	// Protect the line layout cache from being accessed from multiple threads simultaneously
	std::mutex mutexRetrieve;

	std::vector<std::future<void>> futures;
	for (size_t th = 0; th < threads; th++) {
		std::future<void> fut = std::async(policy,
			[=, &surface, &nextIndex, &linesAfterWrap, &mutexRetrieve]() {
			// llTemporary is reused for non-significant lines, avoiding allocation costs.
			std::shared_ptr<LineLayout> llTemporary = std::make_shared<LineLayout>(-1, 200);
			while (true) {
				const size_t i = nextIndex.fetch_add(1, std::memory_order_acq_rel);
				if (i >= linesBeingWrapped) {
					break;
				}
				const Sci::Line lineNumber = lineToWrap + i;
				const Range rangeLine = pdoc->LineRange(lineNumber);
				const Sci::Position lengthLine = rangeLine.Length();
				if (lengthLine < lengthToMultiThread) {
					std::shared_ptr<LineLayout> ll;
					if (significantLines.LineMayCache(lineNumber)) {
						std::lock_guard<std::mutex> guard(mutexRetrieve);
						ll = view.RetrieveLineLayout(lineNumber, *this);
					} else {
						ll = llTemporary;
						ll->ReSet(lineNumber, lengthLine);
					}
					view.LayoutLine(*this, surface, vs, ll.get(), wrapWidth, multiThreaded);
					linesAfterWrap[i] = ll->lines;
				}
			}
		});
		futures.push_back(std::move(fut));
	}
	for (const std::future<void> &f : futures) {
		f.wait();
	}
	// End of multiple threads

	// Multiply duration by number of threads to produce (near) equivalence to duration if single threaded
	const double durationShortLines = epWrapping.Duration(true);
	const double durationShortLinesThreads = durationShortLines * static_cast<double>(threads);

	// Wrap all the long lines in the main thread.
	// LayoutLine may then multi-thread over segments in each line.

	std::shared_ptr<LineLayout> llLarge = std::make_shared<LineLayout>(-1, 200);
	for (size_t indexLarge = 0; indexLarge < linesBeingWrapped; indexLarge++) {
		if (linesAfterWrap[indexLarge] == 0) {
			const Sci::Line lineNumber = lineToWrap + indexLarge;
			const Range rangeLine = pdoc->LineRange(lineNumber);
			const Sci::Position lengthLine = rangeLine.Length();
			std::shared_ptr<LineLayout> ll;
			if (significantLines.LineMayCache(lineNumber)) {
				ll = view.RetrieveLineLayout(lineNumber, *this);
			} else {
				ll = llLarge;
				ll->ReSet(lineNumber, lengthLine);
			}
			view.LayoutLine(*this, surface, vs, ll.get(), wrapWidth);
			linesAfterWrap[indexLarge] = ll->lines;
		}
	}

	const double durationLongLines = epWrapping.Duration();
	const size_t bytesBeingWrapped = pdoc->LineStart(lineToWrap + linesBeingWrapped) - pdoc->LineStart(lineToWrap);

	size_t wrapsDone = 0;

	for (size_t i = 0; i < linesBeingWrapped; i++) {
		const Sci::Line lineNumber = lineToWrap + i;
		int linesWrapped = linesAfterWrap[i];
		if (vs.annotationVisible != AnnotationVisible::Hidden) {
			linesWrapped += pdoc->AnnotationLines(lineNumber);
		}
		if (pcs->SetHeight(lineNumber, linesWrapped)) {
			wrapsDone++;
		}
		wrapPending.Wrapped(lineNumber);
	}

	durationWrapOneByte.AddSample(bytesBeingWrapped, durationShortLinesThreads + durationLongLines);

	return wrapsDone > 0;
}

// Perform  wrapping for a subset of the lines needing wrapping.
// wsAll: wrap all lines which need wrapping in this single call
// wsVisible: wrap currently visible lines
// wsIdle: wrap one page + 100 lines
// Return true if wrapping occurred.
bool Editor::WrapLines(WrapScope ws) {
	Sci::Line goodTopLine = topLine;
	bool wrapOccurred = false;
	if (!Wrapping()) {
		if (wrapWidth != LineLayout::wrapWidthInfinite) {
			wrapWidth = LineLayout::wrapWidthInfinite;
			for (Sci::Line lineDoc = 0; lineDoc < pdoc->LinesTotal(); lineDoc++) {
				int linesWrapped = 1;
				if (vs.annotationVisible != AnnotationVisible::Hidden) {
					linesWrapped += pdoc->AnnotationLines(lineDoc);
				}
				pcs->SetHeight(lineDoc, linesWrapped);
			}
			wrapOccurred = true;
		}
		wrapPending.Reset();

	} else if (wrapPending.NeedsWrap()) {
		wrapPending.start = std::min(wrapPending.start, pdoc->LinesTotal());
		if (!SetIdle(true)) {
			// Idle processing not supported so full wrap required.
			ws = WrapScope::wsAll;
		}
		// Decide where to start wrapping
		Sci::Line lineToWrap = wrapPending.start;
		Sci::Line lineToWrapEnd = std::min(wrapPending.end, pdoc->LinesTotal());

		const Sci::Line lineDocTop = pcs->DocFromDisplay(topLine);
		LineDocSub lineScrollTo;
		if (scrollToAfterWrap) {
			lineScrollTo = scrollToAfterWrap.value();
		} else {
			const Sci::Line subLineTop = topLine - pcs->DisplayFromDoc(lineDocTop);
			lineScrollTo = { lineDocTop, subLineTop };
		}
		if (ws == WrapScope::wsVisible) {
			lineToWrap = std::clamp(lineDocTop-5, wrapPending.start, pdoc->LinesTotal());
			// Priority wrap to just after visible area.
			// Since wrapping could reduce display lines, treat each
			// as taking only one display line.
			lineToWrapEnd = lineDocTop;
			Sci::Line lines = LinesOnScreen() + 1;
			constexpr double secondsAllowed = 0.1;
			const size_t actionsInAllowedTime = std::clamp<Sci::Line>(
				durationWrapOneByte.ActionsInAllowedTime(secondsAllowed),
				0x2000, 0x200000);
			const Sci::Line lineLast = pdoc->LineFromPositionAfter(lineToWrap, actionsInAllowedTime);
			const Sci::Line maxLine = std::min(lineLast, pcs->LinesInDoc());
			while ((lineToWrapEnd < maxLine) && (lines>0)) {
				if (pcs->GetVisible(lineToWrapEnd))
					lines--;
				lineToWrapEnd++;
			}
			// .. and if the paint window is outside pending wraps
			if ((lineToWrap > wrapPending.end) || (lineToWrapEnd < wrapPending.start)) {
				// Currently visible text does not need wrapping
				return false;
			}
		} else if (ws == WrapScope::wsIdle) {
			// Try to keep time taken by wrapping reasonable so interaction remains smooth.
			constexpr double secondsAllowed = 0.01;
			const size_t actionsInAllowedTime = std::clamp<Sci::Line>(
				durationWrapOneByte.ActionsInAllowedTime(secondsAllowed),
				0x200, 0x20000);
			lineToWrapEnd = pdoc->LineFromPositionAfter(lineToWrap, actionsInAllowedTime);
		}
		const Sci::Line lineEndNeedWrap = std::min(wrapPending.end, pdoc->LinesTotal());
		lineToWrapEnd = std::min(lineToWrapEnd, lineEndNeedWrap);

		// Ensure all lines being wrapped are styled.
		pdoc->EnsureStyledTo(pdoc->LineStart(lineToWrapEnd));

		if (lineToWrap < lineToWrapEnd) {

			PRectangle rcTextArea = GetClientRectangle();
			rcTextArea.left = static_cast<XYPOSITION>(vs.textStart);
			rcTextArea.right -= vs.rightMarginWidth;
			wrapWidth = static_cast<int>(rcTextArea.Width());
			RefreshStyleData();
			AutoSurface surface(this);
			if (surface) {
//Platform::DebugPrintf("Wraplines: scope=%0d need=%0d..%0d perform=%0d..%0d\n", ws, wrapPending.start, wrapPending.end, lineToWrap, lineToWrapEnd);

				wrapOccurred = WrapBlock(surface, lineToWrap, lineToWrapEnd);

				goodTopLine = pcs->DisplayFromDocSub(lineScrollTo.lineDoc, lineScrollTo.subLine);
			}
		}

		// If wrapping is done, bring it to resting position
		if (wrapPending.start >= lineEndNeedWrap) {
			wrapPending.Reset();
			scrollToAfterWrap.reset();
		}
	}

	if (wrapOccurred) {
		insideWrapScroll = true;
		SetScrollBars();
		SetTopLine(std::clamp<Sci::Line>(goodTopLine, 0, MaxScrollPos()));
		SetVerticalScrollPos();
		insideWrapScroll = false;
	}

	return wrapOccurred;
}

void Editor::CheckModificationForWrap(DocModification mh) {
	if (FlagSet(mh.modificationType, ModificationFlags::InsertText | ModificationFlags::DeleteText)) {
		view.llc.Invalidate(LineLayout::ValidLevel::checkTextAndStyle);
		const Sci::Line lineDoc = pdoc->SciLineFromPosition(mh.position);
		const Sci::Line lines = std::max(static_cast<Sci::Line>(0), mh.linesAdded);
		if (Wrapping()) {
			// Check if this modification crosses any of the wrap points
			if (wrapPending.NeedsWrap()) {
				if (lineDoc < wrapPending.end) { // Inserted/deleted before or inside wrap range
					wrapPending.end += mh.linesAdded;
				}
			}
			NeedWrapping(lineDoc, lineDoc + lines + 1);
		}
		RefreshStyleData();
		// Fix up annotation heights
		SetAnnotationHeights(lineDoc, lineDoc + lines + 2);
	}
}

// A document line can occupy more than one display line when it wraps; returns
// the number of display lines the given document line needs.
Sci::Line Editor::WrapCount(Sci::Line line) {
	AutoSurface surface(this);
	std::shared_ptr<LineLayout> ll = view.RetrieveLineLayout(line, *this);

	if (surface && ll) {
		view.LayoutLine(*this, surface, vs, ll.get(), wrapWidth);
		return ll->lines;
	}
	return 1;
}
