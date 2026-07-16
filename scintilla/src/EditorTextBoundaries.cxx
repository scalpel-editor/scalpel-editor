// Scintilla source code edit control
/** @file EditorTextBoundaries.cxx
 ** UTF-8 character and word boundaries, character-class defaults, category-map
 ** optimization, direct buffer pointer, current-line text, and line/index mapping.
 **
 ** PositionBefore / PositionAfter step one character (complete UTF-8 sequence or a
 ** single invalid byte under the document invalid-UTF-8 policy). PositionRelative
 ** moves by a signed character count; PositionRelativeCodeUnits moves by UTF-16
 ** code units for interoperability with APIs that count that way. Results are
 ** clamped to the document.
 **
 ** WordStartPosition / WordEndPosition extend from a position using the document
 ** character classes; IsRangeWord reports whether a range is a whole word.
 ** SetCharsDefault restores the default word/whitespace/punctuation classes.
 ** Character-category optimization controls how many characters are cached in the
 ** category map for classification performance.
 **
 ** GetCharacterPointer returns a contiguous read-only view of the whole buffer
 ** (may move the gap). GetCurLine copies the document line containing the main
 ** caret. LineFromIndexPosition / IndexPositionFromLine convert between byte
 ** positions and allocated line character indexes (UTF-16 or UTF-32 units).
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

#include "EditorBasicTypes.h"
#include "EditorDocumentTypes.h"
#include "EditorStyleTypes.h"
#include "EditorInputTypes.h"
#include "EditorLayoutTypes.h"
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

// Byte position of the start of the character before pos (or 0).
Sci::Position Editor::PositionBefore(Sci::Position pos) const {
	return pdoc->MovePositionOutsideChar(pos - 1, -1, true);
}

// Byte position of the start of the character after pos (or Length).
Sci::Position Editor::PositionAfter(Sci::Position pos) const {
	return pdoc->MovePositionOutsideChar(pos + 1, 1, true);
}

// Move relativeCharacters document characters from pos; clamp to [0, Length].
Sci::Position Editor::PositionRelative(Sci::Position pos, Sci::Position relativeCharacters) const {
	return std::clamp<Sci::Position>(
		pdoc->GetRelativePosition(pos, relativeCharacters),
		0, pdoc->Length());
}

// Move relativeUTF16Units UTF-16 code units from pos; clamp to [0, Length].
Sci::Position Editor::PositionRelativeCodeUnits(Sci::Position pos, Sci::Position relativeUTF16Units) const {
	return std::clamp<Sci::Position>(
		pdoc->GetRelativePositionUTF16(pos, relativeUTF16Units),
		0, pdoc->Length());
}

// Restore default word, whitespace, and punctuation character classes.
void Editor::SetCharsDefault() {
	pdoc->SetDefaultCharClasses(true);
}

// How many characters the category map precomputes for classification. Values
// below 256 use the minimum 256-entry map.
void Editor::SetCharacterCategoryOptimization(int countCharacters) {
	pdoc->SetCharacterCategoryOptimization(countCharacters);
}

// Current category-map precompute count.
int Editor::GetCharacterCategoryOptimization() const noexcept {
	return pdoc->CharacterCategoryOptimization();
}

// Contiguous pointer to the whole document buffer with a trailing NUL. May move
// the gap. Any editor or UI call may invalidate the pointer, so reacquire it after
// every such call.
const char *Editor::GetCharacterPointer() const {
	return pdoc->BufferPointer();
}

// Start of the word containing or before pos. When onlyWordCharacters is true,
// non-word characters do not count as a word.
Sci::Position Editor::WordStartPosition(Sci::Position pos, bool onlyWordCharacters) const {
	return pdoc->ExtendWordSelect(pos, -1, onlyWordCharacters);
}

// End of the word containing or after pos.
Sci::Position Editor::WordEndPosition(Sci::Position pos, bool onlyWordCharacters) const {
	return pdoc->ExtendWordSelect(pos, 1, onlyWordCharacters);
}

// True when start is a word-start transition and end is a word-end transition.
// The range may contain spaces and multiple words.
bool Editor::IsRangeWord(Sci::Position start, Sci::Position end) const {
	return pdoc->IsWordAt(start, end);
}

// Copy the document line that contains the main caret into buffer (including the
// line end). When buffer is null, returns the byte length of that line (including
// the line end, not including a trailing NUL). When buffer is non-null, writes up
// to bufferLength bytes plus a NUL and returns the caret's offset within the line.
Sci::Position Editor::GetCurLine(char *buffer, Sci::Position bufferLength) const {
	const Sci::Line lineCurrentPos = pdoc->SciLineFromPosition(sel.MainCaret());
	const Sci::Position lineStart = pdoc->LineStart(lineCurrentPos);
	const Sci::Position lineEnd = pdoc->LineStart(lineCurrentPos + 1);
	if (!buffer) {
		return lineEnd - lineStart;
	}
	const Sci::Position len = std::min(lineEnd - lineStart, bufferLength);
	pdoc->GetCharRange(buffer, lineStart, len);
	buffer[len] = '\0';
	return sel.MainCaret() - lineStart;
}

// Document line for a position in an allocated line character index.
Sci::Line Editor::LineFromIndexPosition(Sci::Position pos, LineCharacterIndexType lineCharacterIndex) const {
	return pdoc->LineFromPositionIndex(pos, lineCharacterIndex);
}

// Byte position of the start of line in an allocated line character index.
Sci::Position Editor::IndexPositionFromLine(Sci::Line line, LineCharacterIndexType lineCharacterIndex) const {
	return pdoc->IndexLineStart(line, lineCharacterIndex);
}
