// Scintilla source code edit control
/** @file EditorDocument.cxx
 ** Document text concern for the editor: whole-text load and save, read-only
 ** and dirty state, insertion and deletion, and range reads.
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

// Replaces the whole document with text. One undo action. The selection becomes
// empty at position 0. Unlike ClearAll, this does not clear annotations, margins,
// or fold state. Bytes are stored as given, including invalid UTF-8 sequences.
// When the document is read-only, the attempt notifies ModifyAttemptRO and leaves
// the text unchanged.
void Editor::SetText(std::string_view text) {
	UndoGroup ug(pdoc);
	pdoc->DeleteChars(0, pdoc->Length());
	SetEmptySelection(0);
	if (!text.empty()) {
		pdoc->InsertString(0, text);
	}
}

// Returns the whole document as bytes. No validation or rewriting of invalid UTF-8.
std::string Editor::GetText() const {
	return RangeText(0, pdoc->Length());
}

// Byte length of the document. Same as GetLength.
Sci::Position Editor::GetTextLength() const noexcept {
	return pdoc->Length();
}

// True when the document is not at its save point (has unsaved changes).
bool Editor::GetModify() const noexcept {
	return !pdoc->IsSavePoint();
}

// When read-only is set, edits that would change text notify ModifyAttemptRO
// and leave the document unchanged. The flag is on the owned document.
void Editor::SetReadOnly(bool readOnly) {
	pdoc->SetReadOnly(readOnly);
}

// True when the owned document rejects edits.
bool Editor::GetReadOnly() const noexcept {
	return pdoc->IsReadOnly();
}

// Inserts text at the main caret and places an empty selection after the insert.
// length is the byte count of text; embedded NULs are allowed.
// When macro recording is on, emits RecordedAddText (including empty text).
void Editor::AddText(std::string_view text) {
	EmitRecordedAction(RecordedAddText{std::string(text)});
	if (text.empty()) {
		return;
	}
	const Sci::Position lengthInserted = pdoc->InsertString(CurrentPosition(), text);
	SetEmptySelection(sel.MainCaret() + lengthInserted);
}

// Inserts text at pos, or at the main caret when pos is negative. If the caret
// was after the insertion point it is moved with the surrounding text.
// When macro recording is on, emits RecordedInsertText with the position as
// passed (negative means caret at replay time via InsertText).
void Editor::InsertText(Sci::Position pos, std::string_view text) {
	EmitRecordedAction(RecordedInsertText{pos, std::string(text)});
	if (text.empty()) {
		return;
	}
	Sci::Position insertPos = pos;
	if (insertPos < 0) {
		insertPos = CurrentPosition();
	}
	pdoc->CheckPosition(insertPos);
	Sci::Position newCurrent = CurrentPosition();
	const Sci::Position lengthInserted = pdoc->InsertString(insertPos, text);
	if (newCurrent > insertPos) {
		newCurrent += lengthInserted;
	}
	SetEmptySelection(newCurrent);
}

// Appends text at the end of the document. Selection and scroll position are unchanged.
// When macro recording is on, emits RecordedAppendText (including empty text).
void Editor::AppendText(std::string_view text) {
	EmitRecordedAction(RecordedAppendText{std::string(text)});
	if (text.empty()) {
		return;
	}
	pdoc->InsertString(pdoc->Length(), text);
}

// Deletes every character and, when writable, clears fold, annotation, EOL
// annotation, and margin text state. Resets selection and vertical scroll, then
// redraws. Tabstops in the view are also cleared.
// When macro recording is on, emits RecordedClearAll.
void Editor::ClearAll() {
	EmitRecordedAction(RecordedClearAll{});
	{
		UndoGroup ug(pdoc);
		if (0 != pdoc->Length()) {
			pdoc->DeleteChars(0, pdoc->Length());
		}
		if (!pdoc->IsReadOnly()) {
			pcs->Clear();
			pdoc->AnnotationClearAll();
			pdoc->EOLAnnotationClearAll();
			pdoc->MarginClearAll();
		}
	}

	view.ClearAllTabstops();

	sel.Clear();
	SetTopLine(0);
	SetVerticalScrollPos();
	InvalidateStyleRedraw();
}

// Deletes lengthDelete bytes starting at start.
void Editor::DeleteRange(Sci::Position start, Sci::Position lengthDelete) {
	pdoc->DeleteChars(start, lengthDelete);
}

// Inserts length/2 character+style cells at the main caret: each cell is one
// character byte followed by one style byte. Styles are applied after the insert.
void Editor::AddStyledText(const char *buffer, Sci::Position appendLength) {
	// The buffer consists of alternating character bytes and style bytes
	const Sci::Position textLength = appendLength / 2;
	std::string text(static_cast<size_t>(textLength), '\0');
	for (Sci::Position i = 0; i < textLength; i++) {
		text[static_cast<size_t>(i)] = buffer[i * 2];
	}
	const Sci::Position lengthInserted = pdoc->InsertString(CurrentPosition(), text);
	for (Sci::Position i = 0; i < textLength; i++) {
		text[static_cast<size_t>(i)] = buffer[i * 2 + 1];
	}
	pdoc->StartStyling(CurrentPosition());
	pdoc->SetStyles(textLength, text.c_str());
	SetEmptySelection(sel.MainCaret() + lengthInserted);
}

// Byte length of the document. Same as GetTextLength.
Sci::Position Editor::GetLength() const noexcept {
	return pdoc->Length();
}

// Byte at pos, or 0 when pos is outside the document.
char Editor::GetCharAt(Sci::Position pos) const noexcept {
	return pdoc->CharAt(pos);
}

// Copies [cpMin, cpMax) as alternating character and style bytes into buffer,
// then two trailing NULs. buffer must hold at least 2*(cpMax-cpMin)+2 bytes.
// Returns the number of data bytes written, not counting the trailing NULs.
Sci::Position Editor::GetStyledText(char *buffer, Sci::Position cpMin, Sci::Position cpMax) const noexcept {
	Sci::Position iPlace = 0;
	for (Sci::Position iChar = cpMin; iChar < cpMax; iChar++) {
		buffer[iPlace++] = pdoc->CharAt(iChar);
		buffer[iPlace++] = pdoc->StyleAtNoExcept(iChar);
	}
	buffer[iPlace] = '\0';
	buffer[iPlace + 1] = '\0';
	return iPlace;
}

// Copies [cpMin, cpMax) into buffer and NUL-terminates it. cpMax of -1 means the
// end of the document. Returns the number of bytes copied, not counting the NUL.
Sci::Position Editor::GetTextRange(char *buffer, Sci::Position cpMin, Sci::Position cpMax) const {
	const Sci::Position cpEnd = (cpMax == -1) ? pdoc->Length() : cpMax;
	PLATFORM_ASSERT(cpEnd <= pdoc->Length());
	const Sci::Position len = cpEnd - cpMin; 	// No -1 as cpMin and cpMax are referring to inter character positions
	pdoc->GetCharRange(buffer, cpMin, len);
	// Spec says copied text is terminated with a NUL
	buffer[len] = '\0';
	return len; 	// Not including NUL
}

// Grows the document buffer capacity to at least bytes. Does not shrink.
void Editor::Allocate(Sci::Position bytes) {
	pdoc->Allocate(bytes);
}

// Pointer into the document buffer for [start, start+rangeLength). Valid until
// the next document mutation. Prefer ranges that do not cross the gap.
const char *Editor::GetRangePointer(Sci::Position start, Sci::Position rangeLength) {
	return pdoc->RangePointer(start, rangeLength);
}

// Position of the movable gap in the document buffer.
Sci::Position Editor::GetGapPosition() const noexcept {
	return pdoc->GapPosition();
}

// Keyboard Clear: delete the selection, or the character after each caret when
// empty. Multiple selections do not delete line ends at the caret.
void Editor::Clear() {
	// If multiple selections, don't delete EOLS
	if (sel.Empty()) {
		bool singleVirtual = false;
		if ((sel.Count() == 1) &&
			!RangeContainsProtected(sel.MainCaret(), sel.MainCaret() + 1) &&
			sel.RangeMain().Start().VirtualSpace()) {
			singleVirtual = true;
		}
		UndoGroup ug(pdoc, (sel.Count() > 1) || singleVirtual);
		for (size_t r=0; r<sel.Count(); r++) {
			if (!RangeContainsProtected(sel.Range(r).caret.Position(), sel.Range(r).caret.Position() + 1)) {
				if (sel.Range(r).Start().VirtualSpace()) {
					if (sel.Range(r).anchor < sel.Range(r).caret)
						sel.Range(r) = SelectionRange(RealizeVirtualSpace(sel.Range(r).anchor.Position(), sel.Range(r).anchor.VirtualSpace()));
					else
						sel.Range(r) = SelectionRange(RealizeVirtualSpace(sel.Range(r).caret.Position(), sel.Range(r).caret.VirtualSpace()));
				}
				if ((sel.Count() == 1) || !pdoc->IsPositionInLineEnd(sel.Range(r).caret.Position())) {
					pdoc->DelChar(sel.Range(r).caret.Position());
					sel.Range(r).ClearVirtualSpace();
				}  // else multiple selection so don't eat line ends
			} else {
				sel.Range(r).ClearVirtualSpace();
			}
		}
	} else {
		ClearSelection();
	}
	sel.RemoveDuplicates();
	ShowCaretAtCurrentPosition();		// Avoid blinking
}
