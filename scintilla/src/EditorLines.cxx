// Scintilla source code edit control
/** @file EditorLines.cxx
 ** Line layout helpers, EOL policy, indentation, and line queries for the editor.
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

// Characters inserted when the user presses Enter. One of CrLf, Cr, or Lf.
// Existing line endings in the document are unchanged until ConvertEOLs.
void Editor::SetEOLMode(EndOfLine eolMode) {
	pdoc->eolMode = eolMode;
}

// Current EOL mode used for new line inserts (CrLf, Cr, or Lf).
EndOfLine Editor::GetEOLMode() const noexcept {
	return pdoc->eolMode;
}

// Rewrites every line ending in the document to eolMode. One undo action.
// The selection is reapplied so carets stay inside the document after lengths
// change. Marks the document modified when endings actually change.
void Editor::ConvertEOLs(EndOfLine eolMode) {
	pdoc->ConvertLineEnds(eolMode);
	SetSelection(sel.MainCaret(), sel.MainAnchor());
}

// Joins lines that cover the current search target by deleting line ends and
// inserting a space when adjacent lines would otherwise touch. Does nothing
// when the target includes protected text.
void Editor::LinesJoin() {
	if (!RangeContainsProtected(targetRange.start.Position(), targetRange.end.Position())) {
		UndoGroup ug(pdoc);
		const Sci::Line line = pdoc->SciLineFromPosition(targetRange.start.Position());
		for (Sci::Position pos = pdoc->LineEnd(line); pos < targetRange.end.Position(); pos = pdoc->LineEnd(line)) {
			const char chPrev = pdoc->CharAt(pos - 1);
			const Sci::Position widthChar = pdoc->LenChar(pos);
			targetRange.end.Add(-widthChar);
			pdoc->DeleteChars(pos, widthChar);
			if (chPrev != ' ') {
				// Ensure at least one space separating previous lines
				const Sci::Position lengthInserted = pdoc->InsertString(pos, " ", 1);
				targetRange.end.Add(lengthInserted);
			}
		}
	}
}

// Inserts the document EOL at each soft wrap break inside the search target so
// display rows become real document lines. When pixelWidth is 0, uses the text
// area width. Does nothing when the target includes protected text.
void Editor::LinesSplit(int pixelWidth) {
	if (!RangeContainsProtected(targetRange.start.Position(), targetRange.end.Position())) {
		if (pixelWidth == 0) {
			const PRectangle rcText = GetTextRectangle();
			pixelWidth = static_cast<int>(rcText.Width());
		}
		const Sci::Line lineStart = pdoc->SciLineFromPosition(targetRange.start.Position());
		Sci::Line lineEnd = pdoc->SciLineFromPosition(targetRange.end.Position());
		const std::string_view eol = pdoc->EOLString();
		UndoGroup ug(pdoc);
		for (Sci::Line line = lineStart; line <= lineEnd; line++) {
			AutoSurface surface(this);
			std::shared_ptr<LineLayout> ll = view.RetrieveLineLayout(line, *this);
			if (surface && ll) {
				const Sci::Position posLineStart = pdoc->LineStart(line);
				view.LayoutLine(*this, surface, vs, ll.get(), pixelWidth);
				Sci::Position lengthInsertedTotal = 0;
				for (int subLine = 1; subLine < ll->lines; subLine++) {
					const Sci::Position lengthInserted = pdoc->InsertString(
						posLineStart + lengthInsertedTotal + ll->LineStart(subLine), eol);
					targetRange.end.Add(lengthInserted);
					lengthInsertedTotal += lengthInserted;
				}
			}
			lineEnd = pdoc->SciLineFromPosition(targetRange.end.Position());
		}
	}
}

// How tab characters are drawn: as an arrow, a stripe, or a control-character
// blob. A change invalidates the display.
void Editor::SetTabDrawMode(TabDrawMode tabDrawMode) {
	SetAppearance(vs.tabDrawMode, tabDrawMode);
	Redraw();
}

// How tab arrows are drawn when white space is visible.
TabDrawMode Editor::GetTabDrawMode() const noexcept {
	return vs.tabDrawMode;
}

// Tab width in columns (spaces). Values of 0 or less are ignored. When indent
// size is 0 (follow tabs), the effective indent width tracks this value.
// Invalidates styles and redraws.
void Editor::SetTabWidth(int tabWidth) {
	if (tabWidth > 0) {
		pdoc->tabInChars = tabWidth;
		if (pdoc->indentInChars == 0)
			pdoc->actualIndentInChars = pdoc->tabInChars;
	}
	InvalidateStyleRedraw();
}

// Tab size in columns.
int Editor::GetTabWidth() const noexcept {
	return pdoc->tabInChars;
}

// Minimum pixel width of a tab when measuring. A change invalidates the display.
void Editor::SetTabMinimumWidth(int pixels) {
	SetAppearance(view.tabWidthMinimumPixels, pixels);
}

// Minimum pixel width of a tab when drawing.
int Editor::GetTabMinimumWidth() const noexcept {
	return view.tabWidthMinimumPixels;
}

// Clears all explicit tab stops on line. Notifies ChangeTabStops when any stop
// was removed.
void Editor::ClearTabStops(Sci::Line line) {
	if (view.ClearTabstops(line)) {
		const DocModification mh(ModificationFlags::ChangeTabStops, 0, 0, 0, nullptr, line);
		NotifyModified(pdoc, mh, nullptr);
	}
}

// Adds an explicit tab stop at x on line (pixels from the left of the text).
// Notifies ChangeTabStops when the stop is new.
void Editor::AddTabStop(Sci::Line line, int x) {
	if (view.AddTabstop(line, x)) {
		const DocModification mh(ModificationFlags::ChangeTabStops, 0, 0, 0, nullptr, line);
		NotifyModified(pdoc, mh, nullptr);
	}
}

// Next explicit tab stop at or after x on line, or 0 when none.
int Editor::GetNextTabStop(Sci::Line line, int x) const {
	return view.GetNextTabstop(line, x);
}

// When true, the selection background extends through the end-of-line area.
void Editor::SetSelEOLFilled(bool filled) {
	vs.selection.eolFilled = filled;
	InvalidateStyleRedraw();
}

// True when selection background continues past the last character to the edge.
bool Editor::GetSelEOLFilled() const noexcept {
	return vs.selection.eolFilled;
}

namespace {

// Marks characters as belonging to characterClass. Leaves classes unchanged when characters is empty.
void SetCharsOfClass(Document *pdoc, std::string_view characters, CharacterClass characterClass) {
	if (characters.empty()) {
		return;
	}
	const std::string copy(characters);
	pdoc->SetCharClasses(reinterpret_cast<const unsigned char *>(copy.c_str()), characterClass);
}

}

// Replaces the word character set used for word motion and search word options.
// Resets all character classes to defaults first, then marks characters as word.
// An empty characters string leaves only the default reset (no extra word chars).
void Editor::SetWordChars(std::string_view characters) {
	pdoc->SetDefaultCharClasses(false);
	SetCharsOfClass(pdoc, characters, CharacterClass::word);
}

// Writes the current word characters into buffer when non-null. Returns how
// many characters would be written.
int Editor::GetWordChars(unsigned char *buffer) const {
	return pdoc->GetCharsOfClass(CharacterClass::word, buffer);
}

// Indent width in columns. Zero means "same as tab width". Invalidates styles.
void Editor::SetIndent(int indentSize) {
	pdoc->indentInChars = indentSize;
	if (pdoc->indentInChars != 0)
		pdoc->actualIndentInChars = pdoc->indentInChars;
	else
		pdoc->actualIndentInChars = pdoc->tabInChars;
	InvalidateStyleRedraw();
}

// Indent size in columns for indent/dedent commands.
int Editor::GetIndent() const noexcept {
	return pdoc->indentInChars;
}

// When true, indentation uses tab characters; otherwise only spaces.
void Editor::SetUseTabs(bool useTabs) {
	pdoc->useTabs = useTabs;
}

// True when indentation inserts tab characters; false uses spaces.
bool Editor::GetUseTabs() const noexcept {
	return pdoc->useTabs;
}

// Sets the indentation of line to indentSize columns, rewriting leading
// whitespace according to use-tabs and tab/indent widths.
void Editor::SetLineIndentation(Sci::Line line, Sci::Position indentSize) {
	pdoc->SetLineIndentation(line, indentSize);
}

// Indentation column count of line.
int Editor::GetLineIndentation(Sci::Line line) const {
	return pdoc->GetLineIndentation(line);
}

// Position after the leading whitespace on line.
Sci::Position Editor::GetLineIndentPosition(Sci::Line line) const noexcept {
	return pdoc->GetLineIndentPosition(line);
}

// Display column of pos counting tabs as tabWidth columns.
Sci::Position Editor::GetColumn(Sci::Position pos) const noexcept {
	return pdoc->GetColumn(pos);
}

// Number of whole Unicode characters (code points) in [startPos, endPos).
Sci::Position Editor::CountCharacters(Sci::Position startPos, Sci::Position endPos) const noexcept {
	return pdoc->CountCharacters(startPos, endPos);
}

// Number of UTF-16 code units in [startPos, endPos).
Sci::Position Editor::CountCodeUnits(Sci::Position startPos, Sci::Position endPos) const noexcept {
	return pdoc->CountUTF16(startPos, endPos);
}

// Whether indentation guides are drawn and how far they look for nearby text.
void Editor::SetIndentationGuides(IndentView indentView) {
	vs.viewIndentationGuides = indentView;
	Redraw();
}

// Indentation guide look: None, Real, LookForward, or LookBoth.
IndentView Editor::GetIndentationGuides() const noexcept {
	return vs.viewIndentationGuides;
}

// Position of the last character on line before the line ending.
Sci::Position Editor::GetLineEndPosition(Sci::Line line) const noexcept {
	return pdoc->LineEnd(line);
}

// Copies the bytes of line (excluding a trailing NUL) into buffer when non-null.
// Returns the number of bytes in the line including the line ending if present.
// An empty document still has one empty line.
Sci::Position Editor::GetLine(Sci::Line line, char *buffer) const {
	const Sci::Position lineStart = pdoc->LineStart(line);
	const Sci::Position lineEnd = pdoc->LineStart(line + 1);
	const Sci::Position len = lineEnd - lineStart;
	if (buffer) {
		pdoc->GetCharRange(buffer, lineStart, len);
	}
	return len;
}

// Number of lines in the document. An empty document reports 1.
Sci::Line Editor::GetLineCount() const noexcept {
	if (pdoc->LinesTotal() == 0)
		return 1;
	return pdoc->LinesTotal();
}

// Grows line-storage capacity without changing text.
void Editor::AllocateLines(Sci::Line lines) {
	pdoc->AllocateLines(lines);
}

// Document line containing pos, or 0 when pos is negative.
Sci::Line Editor::LineFromPosition(Sci::Position pos) const noexcept {
	if (pos < 0)
		return 0;
	return pdoc->LineFromPosition(pos);
}

// Byte position of the start of line. Negative line uses the line of the
// selection start. Line 0 is always 0. Lines past the end return -1.
Sci::Position Editor::PositionFromLine(Sci::Line line) {
	if (line < 0)
		line = pdoc->LineFromPosition(SelectionStart().Position());
	if (line == 0)
		return 0;
	if (line > pdoc->LinesTotal())
		return -1;
	return pdoc->LineStart(line);
}

// True when line is expanded in the contraction (fold) map.
bool Editor::GetLineVisible(Sci::Line line) const noexcept {
	return pcs->GetVisible(line);
}

// When true, Tab and Shift+Tab change indentation of selected lines.
void Editor::SetTabIndents(bool tabIndents) {
	pdoc->tabIndents = tabIndents;
}

// True when Tab indents selected lines instead of inserting a tab.
bool Editor::GetTabIndents() const noexcept {
	return pdoc->tabIndents;
}

// When true, BackSpace at the indent removes one indent step rather than one
// character.
void Editor::SetBackSpaceUnIndents(bool bsUnIndents) {
	pdoc->backspaceUnindents = bsUnIndents;
}

// True when BackSpace unindents at indentation instead of deleting one character.
bool Editor::GetBackSpaceUnIndents() const noexcept {
	return pdoc->backspaceUnindents;
}

// Byte length of line including its line ending, or 0 when line is out of range.
Sci::Position Editor::LineLength(Sci::Line line) const noexcept {
	if ((line < 0) || (line > pdoc->LineFromPosition(pdoc->Length())))
		return 0;
	return pdoc->LineStart(line + 1) - pdoc->LineStart(line);
}

// When true, line-ending characters are drawn as glyphs instead of hidden.
void Editor::SetViewEOL(bool visible) {
	vs.viewEOL = visible;
	InvalidateStyleRedraw();
}

// True when end-of-line glyphs are drawn.
bool Editor::GetViewEOL() const noexcept {
	return vs.viewEOL;
}

// Column for the single long-line edge guide (used with edge mode).
void Editor::SetEdgeColumn(int column) {
	vs.theEdge.column = column;
	InvalidateStyleRedraw();
}

// Column of the single long-line edge marker.
int Editor::GetEdgeColumn() const noexcept {
	return vs.theEdge.column;
}

// Column of multi-edge which, or -1 when which is out of range.
int Editor::GetMultiEdgeColumn(size_t which) const noexcept {
	if (which >= vs.theMultiEdge.size()) {
		return -1;
	}
	return vs.theMultiEdge[which].column;
}

// Start of the selection intersection with line, or invalidPosition when the
// selection does not meet that line.
Sci::Position Editor::GetLineSelStartPosition(Sci::Line line) const noexcept {
	const SelectionSegment segmentLine(pdoc->LineStart(line), pdoc->LineEnd(line));
	for (size_t r = 0; r < sel.Count(); r++) {
		const SelectionSegment portion = sel.Range(r).Intersect(segmentLine);
		if (portion.start.IsValid()) {
			return portion.start.Position();
		}
	}
	return Sci::invalidPosition;
}

// End of the selection intersection with line, or invalidPosition when none.
Sci::Position Editor::GetLineSelEndPosition(Sci::Line line) const noexcept {
	const SelectionSegment segmentLine(pdoc->LineStart(line), pdoc->LineEnd(line));
	for (size_t r = 0; r < sel.Count(); r++) {
		const SelectionSegment portion = sel.Range(r).Intersect(segmentLine);
		if (portion.start.IsValid()) {
			return portion.end.Position();
		}
	}
	return Sci::invalidPosition;
}

// Characters treated as whitespace for word motion and drawing options.
void Editor::SetWhitespaceChars(std::string_view characters) {
	SetCharsOfClass(pdoc, characters, CharacterClass::space);
}

// Characters currently classified as white space for word motion.
int Editor::GetWhitespaceChars(unsigned char *buffer) const {
	return pdoc->GetCharsOfClass(CharacterClass::space, buffer);
}

// Characters treated as punctuation for word motion.
void Editor::SetPunctuationChars(std::string_view characters) {
	SetCharsOfClass(pdoc, characters, CharacterClass::punctuation);
}

// Characters currently classified as punctuation for word motion.
int Editor::GetPunctuationChars(unsigned char *buffer) const {
	return pdoc->GetCharsOfClass(CharacterClass::punctuation, buffer);
}

// Byte position on line closest to the given display column.
Sci::Position Editor::FindColumn(Sci::Line line, Sci::Position column) const noexcept {
	return pdoc->FindColumn(line, column);
}

// Extra Unicode line-ending forms the document may recognize (in addition to
// CR, LF, and CRLF). When the active set changes, fold and annotation heights
// are rebuilt and styles redrawn.
void Editor::SetLineEndTypesAllowed(LineEndType lineEndBitSet) {
	if (pdoc->SetLineEndTypesAllowed(lineEndBitSet)) {
		pcs->Clear();
		pcs->InsertLines(0, pdoc->LinesTotal() - 1);
		SetAnnotationHeights(0, pdoc->LinesTotal());
		InvalidateStyleRedraw();
	}
}

// Extra Unicode line-ending forms allowed in this document.
LineEndType Editor::GetLineEndTypesAllowed() const noexcept {
	return pdoc->GetLineEndTypesAllowed();
}

// Line-ending forms currently in force (allowed intersected with supported).
LineEndType Editor::GetLineEndTypesActive() const noexcept {
	return pdoc->GetLineEndTypesActive();
}

// Which optional per-line character indexes are allocated (UTF-16/UTF-32).
LineCharacterIndexType Editor::GetLineCharacterIndex() const noexcept {
	return pdoc->LineCharacterIndex();
}

// Build a line index for UTF-16 or UTF-32 unit lookups.
void Editor::AllocateLineCharacterIndex(LineCharacterIndexType lineCharacterIndex) {
	pdoc->AllocateLineCharacterIndex(lineCharacterIndex);
}

// Drop a previously allocated line character index.
void Editor::ReleaseLineCharacterIndex(LineCharacterIndexType lineCharacterIndex) {
	pdoc->ReleaseLineCharacterIndex(lineCharacterIndex);
}
