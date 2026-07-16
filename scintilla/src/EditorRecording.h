// Scintilla source code edit control
/** @file EditorRecording.h
 ** Typed macro recording: owned actions and the host callback contract.
 **
 ** Recording captures user-level edits so a host can store and replay them.
 ** The numeric SCN_MACRORECORD form (message number plus raw parameters) is
 ** replaced by RecordedAction: a variant that owns any text and carries only
 ** typed fields. Production recording still uses the temporary numeric path
 ** until phase 4 steps 15 and 16 move lifecycle, command capture, and
 ** parameterized capture onto this type.
 **
 ** # Recordable operations
 **
 ** - Zero-argument bindable actions as RecordedCommand (EditorCommand values
 **   that NotifyMacroRecord currently allowlists: movement and selection
 **   extension, editing, clipboard cut/copy/paste/clear, scroll-to-start/end,
 **   vertical centre caret, and similar). Undo and redo are not recorded.
 ** - ReplaceSelection with owned text (typed characters and ReplaceSel; also
 **   how newline insertion is stored today — one ReplaceSelection per EOL
 **   character after insertion, not a separate NewLine command).
 ** - AddText, InsertText (position + text), AppendText with owned text.
 ** - ClearAll.
 ** - GotoLine and GotoPos.
 ** - SearchAnchor, and Search with direction, FindOption flags, and owned needle.
 ** - SetSelectionMode with a typed SelectionMode.
 **
 ** # Ignored operations
 **
 ** - Display-only and style/layout settings that never appear in the
 **   NotifyMacroRecord allowlist (scroll bars, wrap mode, colours, zoom as a
 **   view setting, and other non-editing messages).
 ** - NewLine as a command: filtered out because character insert already
 **   records the EOL bytes as ReplaceSelection.
 ** - Tentative IME input (CharacterSource::TentativeInput): not recorded;
 **   only committed or direct input is.
 **
 ** # Text ownership
 **
 ** Every text-bearing alternative stores a std::string copy of the UTF-8
 ** bytes as they stand in the document path, including invalid sequences
 ** (phase 3 policy: store bytes as given). The recording callback receives a
 ** const RecordedAction &; the editor does not borrow host memory and does
 ** not keep a pointer into the action after the callback returns. The host
 ** must copy any text it needs beyond the callback (the test host appends a
 ** full copy of the action to its observation list).
 **
 ** # Replay and no recursive recording
 **
 ** Replay applies the same named editor operations that produced the actions
 ** (ExecuteCommand, ReplaceSel, InsertText, and so on). While replaying, the
 ** editor must not emit new recorded actions: either recording is stopped
 ** before replay, or an internal replaying guard suppresses capture. Nested
 ** StartRecording during an active session remains "already recording"; the
 ** host owns when to start and stop around capture versus playback.
 **/
// Copyright 1998-2011 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef EDITOR_RECORDING_H
#define EDITOR_RECORDING_H

#include <functional>
#include <string>
#include <variant>

#include "ScintillaTypes.h"
#include "Position.h"
#include "EditorCommands.h"

namespace Scintilla::Internal {

// Direction for a recorded parameterized search (SearchNext vs SearchPrev).
enum class SearchDirection {
	Next,
	Prev,
};

// Zero-argument bindable action (key map / ExecuteCommand).
struct RecordedCommand {
	EditorCommand command = EditorCommand::None;
};

// Replace the selection (or insert at the caret) with owned text.
// Used for ReplaceSel and for recorded character / EOL insertion.
struct RecordedReplaceSelection {
	std::string text;
};

// Insert text at the current position without clearing the selection first
// (AddText message shape).
struct RecordedAddText {
	std::string text;
};

// Insert text at an absolute byte position.
struct RecordedInsertText {
	Sci::Position position = 0;
	std::string text;
};

// Append text at the end of the document.
struct RecordedAppendText {
	std::string text;
};

// Clear the whole document (ClearAll).
struct RecordedClearAll {
};

// Move the caret to the start of a document line.
struct RecordedGotoLine {
	Sci::Line line = 0;
};

// Move the caret to an absolute byte position.
struct RecordedGotoPos {
	Sci::Position position = 0;
};

// Remember the current selection start as the search anchor.
struct RecordedSearchAnchor {
};

// Parameterized search from the search anchor.
struct RecordedSearch {
	SearchDirection direction = SearchDirection::Next;
	Scintilla::FindOption flags = Scintilla::FindOption::None;
	std::string text;
};

// Set how subsequent selection extension behaves.
struct RecordedSetSelectionMode {
	Scintilla::SelectionMode mode = Scintilla::SelectionMode::Stream;
};

/**
 * One recorded user-level edit. Owns any text. No message number or raw
 * wParam/lParam. Alternatives match the shapes NotifyMacroRecord allows today.
 */
using RecordedAction = std::variant<
	RecordedCommand,
	RecordedReplaceSelection,
	RecordedAddText,
	RecordedInsertText,
	RecordedAppendText,
	RecordedClearAll,
	RecordedGotoLine,
	RecordedGotoPos,
	RecordedSearchAnchor,
	RecordedSearch,
	RecordedSetSelectionMode
>;

/**
 * Host callback for one recorded action while recording is on.
 * The action owns its text; copy anything needed after return.
 */
using RecordingCallback = std::function<void(const RecordedAction &)>;

}

#endif
