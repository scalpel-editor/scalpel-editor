// Scintilla source code edit control
/** @file EditorSearch.cxx
 ** Target search, replace, and selection-relative find for the editor.
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


// --- Target range and search (application + private) ---

// Start of the search/replace target range (byte position).
void Editor::SetTargetStart(Sci::Position pos) {
	targetRange.start.SetPosition(pos);
}

// Start position of the target range.
Sci::Position Editor::GetTargetStart() const noexcept {
	return targetRange.start.Position();
}

// Virtual space on the target start (for rectangular targets).
void Editor::SetTargetStartVirtualSpace(Sci::Position space) {
	targetRange.start.SetVirtualSpace(space);
}

// Virtual space of the target start.
Sci::Position Editor::GetTargetStartVirtualSpace() const noexcept {
	return targetRange.start.VirtualSpace();
}

// End of the search/replace target range (byte position).
void Editor::SetTargetEnd(Sci::Position pos) {
	targetRange.end.SetPosition(pos);
}

// End position of the target range.
Sci::Position Editor::GetTargetEnd() const noexcept {
	return targetRange.end.Position();
}

// Virtual space on the target end.
void Editor::SetTargetEndVirtualSpace(Sci::Position space) {
	targetRange.end.SetVirtualSpace(space);
}

// Virtual space of the target end.
Sci::Position Editor::GetTargetEndVirtualSpace() const noexcept {
	return targetRange.end.VirtualSpace();
}

// Set both ends of the target range in one call.
void Editor::SetTargetRange(Sci::Position start, Sci::Position end) {
	targetRange.start.SetPosition(start);
	targetRange.end.SetPosition(end);
}

// Set the target to cover the entire document.
void Editor::TargetWholeDocument() {
	targetRange.start.SetPosition(0);
	targetRange.end.SetPosition(pdoc->Length());
}

// Bytes currently covered by the target range.
std::string Editor::GetTargetText() const {
	return RangeText(targetRange.start.Position(), targetRange.end.Position());
}

// FindOption flags for SearchInTarget: match case, whole word, word start, regexp, posix, etc.
void Editor::SetSearchFlags(FindOption flags) {
	searchFlags = flags;
}

// Current FindOption flags used by SearchInTarget.
FindOption Editor::GetSearchFlags() const noexcept {
	return searchFlags;
}

// Replace the main selection with text. One undo action. Moves the caret after
// the insert and ensures it is visible.
// When macro recording is on, emits RecordedReplaceSelection with owned text.
void Editor::ReplaceSel(std::string_view text) {
	EmitRecordedAction(RecordedReplaceSelection{std::string(text)});
	UndoGroup ug(pdoc);
	ClearSelection();
	const Sci::Position lengthInserted = pdoc->InsertString(sel.MainCaret(), text);
	SetEmptySelection(sel.MainCaret() + lengthInserted);
	SetLastXChosen();
	EnsureCaretVisible();
}

// Paste shape replacement of the selection as a rectangular paste of text.
void Editor::ReplaceRectangular(std::string_view text) {
	UndoGroup ug(pdoc);
	if (!sel.Empty()) {
		ClearSelection();
	}
	InsertPasteShape(text, PasteShape::rectangular);
}

// Search for text inside the target using searchFlags.
// On success moves the target to the match and returns the start position; on failure -1.
// Invalid regular expressions set Status::RegEx and return -1.
Sci::Position Editor::SearchInTarget(std::string_view text) {
	return SearchInTarget(text.data(), static_cast<Sci::Position>(text.size()));
}

// Remember SelectionStart as the anchor for SearchNext / SearchPrev commands.
void Editor::SearchAnchor() noexcept {
	searchAnchor = SelectionStart().Position();
}

// SearchNext or SearchPrev from the search anchor with flags in wParam and C string in lParam.
// On success selects the match and returns its start; otherwise invalidPosition.
Sci::Position Editor::SearchText(
    EditorCommand command,		///< Accepts both @c EditorCommand::SearchNext and @c EditorCommand::SearchPrev.
    uptr_t wParam,				///< Search modes : @c FindOption::MatchCase, @c FindOption::WholeWord,
    ///< @c FindOption::WordStart, @c FindOption::RegExp or @c FindOption::Posix.
    sptr_t lParam) {			///< The text to search for.

	const char *txt = ConstCharPtrFromSPtr(lParam);
	Sci::Position pos = Sci::invalidPosition;
	Sci::Position lengthFound = strlen(txt);
	if (!pdoc->HasCaseFolder())
		pdoc->SetCaseFolder(std::make_unique<CaseFolderUnicode>());
	try {
		if (command == EditorCommand::SearchNext) {
			pos = pdoc->FindText(searchAnchor, pdoc->Length(), txt,
					static_cast<FindOption>(wParam),
					&lengthFound);
		} else {
			pos = pdoc->FindText(searchAnchor, 0, txt,
					static_cast<FindOption>(wParam),
					&lengthFound);
		}
	} catch (RegexError &) {
		errorStatus = Status::RegEx;
		return Sci::invalidPosition;
	}
	if (pos != Sci::invalidPosition) {
		SetSelection(pos, pos + lengthFound);
	}

	return pos;
}

// C-string form of SearchInTarget; length is the byte count of text.
Sci::Position Editor::SearchInTarget(const char *text, Sci::Position length) {
	Sci::Position lengthFound = length;

	if (!pdoc->HasCaseFolder())
		pdoc->SetCaseFolder(std::make_unique<CaseFolderUnicode>());
	try {
		const Sci::Position pos = pdoc->FindText(targetRange.start.Position(), targetRange.end.Position(), text,
				searchFlags,
				&lengthFound);
		if (pos != -1) {
			targetRange.start.SetPosition(pos);
			targetRange.end.SetPosition(pos + lengthFound);
		}
		return pos;
	} catch (RegexError &) {
		errorStatus = Status::RegEx;
		return -1;
	}
}

// Replace the target with text. ReplaceType::patterns expands \1-style backreferences;
// minimal trims a common prefix/suffix before replacing. Returns the inserted length.
// Realizes virtual space at the target start. One undo action.
Sci::Position Editor::ReplaceTarget(ReplaceType replaceType, std::string_view text) {
	pdoc->CheckPosition(targetRange.start.Position());

	UndoGroup ug(pdoc);

	std::string substituted;	// Copy in case of re-entrance

	if (replaceType == ReplaceType::patterns) {
		Sci::Position length = text.length();
		const char *p = pdoc->SubstituteByPosition(text.data(), &length);
		if (!p) {
			return 0;
		}
		substituted.assign(p, length);
		text = substituted;
	}

	if (replaceType == ReplaceType::minimal) {
		// Check for prefix and suffix and reduce text and target to match.
		// This is performed with Range which doesn't support virtual space.
		Range range(targetRange.start.Position(), targetRange.end.Position());
		pdoc->TrimReplacement(text, range);
		// Re-apply virtual space to start if start position didn't change.
		// Don't bother with end as its virtual space is not used
		const SelectionPosition start(range.start == targetRange.start.Position() ?
			targetRange.start : SelectionPosition(range.start));
		targetRange = SelectionSegment(start, SelectionPosition(range.end));
	}

	// Make a copy of targetRange in case callbacks use target
	SelectionSegment replaceRange = targetRange;

	// Remove the text inside the range
	if (replaceRange.Length() > 0)
		pdoc->DeleteChars(replaceRange.start.Position(), replaceRange.Length());

	// Realize virtual space of target start
	const Sci::Position startAfterSpaceInsertion = RealizeVirtualSpace(replaceRange.start.Position(), replaceRange.start.VirtualSpace());
	replaceRange.start.SetPosition(startAfterSpaceInsertion);
	replaceRange.end = replaceRange.start;

	// Insert the new text
	const Sci::Position lengthInserted = pdoc->InsertString(replaceRange.start.Position(), text);
	replaceRange.end.SetPosition(replaceRange.start.Position() + lengthInserted);

	// Copy back to targetRange in case application is chaining modifications
	targetRange = replaceRange;

	return text.length();
}

// Copy the text of a numbered search tag (1..9 from the last regular-expression
// search) into tagValue when non-null. Returns the byte length of the tag text
// (0 when the tag is empty or out of range).
Sci::Position Editor::GetTag(char *tagValue, int tagNumber) {
	const char *text = nullptr;
	Sci::Position length = 0;
	if ((tagNumber >= 1) && (tagNumber <= 9)) {
		char name[3] = "\\?";
		name[1] = static_cast<char>(tagNumber + '0');
		length = 2;
		text = pdoc->SubstituteByPosition(name, &length);
	}
	if (tagValue) {
		if (text)
			memcpy(tagValue, text, length + 1);
		else
			*tagValue = '\0';
	}
	return length;
}

