// Scintilla source code edit control
/** @file EditorClipboard.cxx
 ** Clipboard cut, copy, paste options, and range-to-clipboard helpers.
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

// True when the document is not read-only and the selection is not protected.
// Used for menu enablement; the host may still refuse Paste if the clipboard
// is empty.
bool Editor::CanPaste() {
	return !pdoc->IsReadOnly() && !SelectionContainsProtected();
}

// Copies the selection (or current line when empty and allowLineCopy is true)
// to the host clipboard, then deletes the selection when cut is allowed.
void Editor::Cut() {
	pdoc->CheckReadOnly();
	if (!pdoc->IsReadOnly() && !SelectionContainsProtected()) {
		Copy();
		ClearSelection();
	}
}

// Like Copy, but when the selection is empty copies the whole line that
// contains the caret.
void Editor::CopyAllowLine() {
	SelectionText selectedText;
	CopySelectionRange(&selectedText, true);
	CopyToClipboard(selectedText);
}

// Like Cut, but when the selection is empty cuts the whole line that contains
// the caret.
void Editor::CutAllowLine() {
	if (sel.Empty()) {
		pdoc->CheckReadOnly();
		if (!pdoc->IsReadOnly()) {
			SelectionText selectedText;
			if (CopyLineRange(&selectedText, false)) {
				CopyToClipboard(selectedText);
				LineDelete();
			}
		}
	} else {
		Cut();
	}
}

// Builds clipboard text from the current selection into ss. For a rectangular
// selection, ranges are ordered and separated by the document EOL. For multiple
// stream selections, ranges are joined with the copy separator. When the
// selection is empty and allowLineCopy is true, copies the caret's line.
void Editor::CopySelectionRange(SelectionText *ss, bool allowLineCopy) {
	if (sel.Empty()) {
		if (allowLineCopy) {
			CopyLineRange(ss);
		}
	} else {
		std::string text;
		std::vector<SelectionRange> rangesInOrder = sel.RangesCopy();
		if (sel.selType == Selection::SelTypes::rectangle)
			std::sort(rangesInOrder.begin(), rangesInOrder.end());
		const std::string_view separator = (sel.selType == Selection::SelTypes::rectangle) ? pdoc->EOLString() : copySeparator;
		for (size_t part = 0; part < rangesInOrder.size(); part++) {
			text.append(RangeText(rangesInOrder[part].Start().Position(), rangesInOrder[part].End().Position()));
			if ((sel.selType == Selection::SelTypes::rectangle) || (part < rangesInOrder.size() - 1)) {
				// Append unless simple selection or last part of multiple selection
				text.append(separator);
			}
		}
		ss->Copy(text, sel.IsRectangular(), sel.selType == Selection::SelTypes::lines);
	}
}

// Copies [start, end) to the host clipboard without changing the selection.
void Editor::CopyRangeToClipboard(Sci::Position start, Sci::Position end) {
	start = pdoc->ClampPositionIntoDocument(start);
	end = pdoc->ClampPositionIntoDocument(end);
	SelectionText selectedText;
	std::string text = RangeText(start, end);
	selectedText.Copy(text, false, false);
	CopyToClipboard(selectedText);
}

// Places an arbitrary string on the host clipboard.
void Editor::CopyText(std::string_view text) {
	SelectionText selectedText;
	selectedText.Copy(std::string(text), false, false);
	CopyToClipboard(selectedText);
}

// How paste inserts into multiple selections: Once pastes only into the main
// selection, Each pastes into every selection.
void Editor::SetMultiPaste(MultiPaste multiPaste) {
	multiPasteMode = multiPaste;
}

// Current multi-paste mode (Once or Each).
MultiPaste Editor::GetMultiPaste() const noexcept {
	return multiPasteMode;
}

// When true, paste converts line endings in the clipboard text to the document
// EOL mode before inserting.
void Editor::SetPasteConvertEndings(bool convert) {
	convertPastes = convert;
}

// True when paste rewrites line endings to the document EOL mode.
bool Editor::GetPasteConvertEndings() const noexcept {
	return convertPastes;
}

// Separator string inserted between parts when copying a multiple stream
// selection (not used for rectangular selections, which use the document EOL).
void Editor::SetCopySeparator(std::string_view separator) {
	copySeparator = std::string(separator);
}

// Separator string used between parts of a multi-stream copy.
std::string Editor::GetCopySeparator() const {
	return copySeparator;
}
