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
 ** - Zero-argument bindable actions as RecordedCommand (the EditorCommand
 **   values accepted by IsRecordableCommand: movement and selection extension,
 **   editing, clipboard cut/copy/paste/clear, scroll-to-start/end, vertical
 **   centre caret, and similar). Undo, redo, newline, zoom, and search commands
 **   are not RecordedCommand values. Search anchor and parameterized search use
 **   their dedicated alternatives below.
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
#include <stdexcept>
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

/**
 * True for zero-argument commands that macro recording captures and replay can
 * apply through ExecuteCommand. Search actions use their dedicated alternatives.
 */
constexpr bool IsRecordableCommand(EditorCommand command) noexcept {
	switch (command) {
	case EditorCommand::LineDown:
	case EditorCommand::LineDownExtend:
	case EditorCommand::LineDownRectExtend:
	case EditorCommand::LineUp:
	case EditorCommand::LineUpExtend:
	case EditorCommand::LineUpRectExtend:
	case EditorCommand::LineScrollDown:
	case EditorCommand::LineScrollUp:
	case EditorCommand::ParaDown:
	case EditorCommand::ParaDownExtend:
	case EditorCommand::ParaUp:
	case EditorCommand::ParaUpExtend:
	case EditorCommand::CharLeft:
	case EditorCommand::CharLeftExtend:
	case EditorCommand::CharLeftRectExtend:
	case EditorCommand::CharRight:
	case EditorCommand::CharRightExtend:
	case EditorCommand::CharRightRectExtend:
	case EditorCommand::WordLeft:
	case EditorCommand::WordLeftExtend:
	case EditorCommand::WordRight:
	case EditorCommand::WordRightExtend:
	case EditorCommand::WordLeftEnd:
	case EditorCommand::WordLeftEndExtend:
	case EditorCommand::WordRightEnd:
	case EditorCommand::WordRightEndExtend:
	case EditorCommand::WordPartLeft:
	case EditorCommand::WordPartLeftExtend:
	case EditorCommand::WordPartRight:
	case EditorCommand::WordPartRightExtend:
	case EditorCommand::Home:
	case EditorCommand::HomeExtend:
	case EditorCommand::HomeRectExtend:
	case EditorCommand::HomeDisplay:
	case EditorCommand::HomeDisplayExtend:
	case EditorCommand::HomeWrap:
	case EditorCommand::HomeWrapExtend:
	case EditorCommand::VCHome:
	case EditorCommand::VCHomeExtend:
	case EditorCommand::VCHomeRectExtend:
	case EditorCommand::VCHomeDisplay:
	case EditorCommand::VCHomeDisplayExtend:
	case EditorCommand::VCHomeWrap:
	case EditorCommand::VCHomeWrapExtend:
	case EditorCommand::LineEnd:
	case EditorCommand::LineEndExtend:
	case EditorCommand::LineEndRectExtend:
	case EditorCommand::LineEndDisplay:
	case EditorCommand::LineEndDisplayExtend:
	case EditorCommand::LineEndWrap:
	case EditorCommand::LineEndWrapExtend:
	case EditorCommand::DocumentStart:
	case EditorCommand::DocumentStartExtend:
	case EditorCommand::DocumentEnd:
	case EditorCommand::DocumentEndExtend:
	case EditorCommand::PageUp:
	case EditorCommand::PageUpExtend:
	case EditorCommand::PageUpRectExtend:
	case EditorCommand::PageDown:
	case EditorCommand::PageDownExtend:
	case EditorCommand::PageDownRectExtend:
	case EditorCommand::StutteredPageUp:
	case EditorCommand::StutteredPageUpExtend:
	case EditorCommand::StutteredPageDown:
	case EditorCommand::StutteredPageDownExtend:
	case EditorCommand::ScrollToStart:
	case EditorCommand::ScrollToEnd:
	case EditorCommand::EditToggleOvertype:
	case EditorCommand::Cancel:
	case EditorCommand::DeleteBack:
	case EditorCommand::DeleteBackNotLine:
	case EditorCommand::Tab:
	case EditorCommand::LineIndent:
	case EditorCommand::BackTab:
	case EditorCommand::LineDedent:
	case EditorCommand::FormFeed:
	case EditorCommand::DelWordLeft:
	case EditorCommand::DelWordRight:
	case EditorCommand::DelWordRightEnd:
	case EditorCommand::DelLineLeft:
	case EditorCommand::DelLineRight:
	case EditorCommand::LineCopy:
	case EditorCommand::LineCut:
	case EditorCommand::LineDelete:
	case EditorCommand::LineTranspose:
	case EditorCommand::LineReverse:
	case EditorCommand::LineDuplicate:
	case EditorCommand::SelectionDuplicate:
	case EditorCommand::LowerCase:
	case EditorCommand::UpperCase:
	case EditorCommand::Cut:
	case EditorCommand::Copy:
	case EditorCommand::Paste:
	case EditorCommand::Clear:
	case EditorCommand::CopyAllowLine:
	case EditorCommand::CutAllowLine:
	case EditorCommand::SelectAll:
	case EditorCommand::VerticalCentreCaret:
	case EditorCommand::MoveSelectedLinesUp:
	case EditorCommand::MoveSelectedLinesDown:
		return true;
	default:
		return false;
	}
}

// Zero-argument bindable action (key map / ExecuteCommand).
class RecordedCommand {
public:
	explicit RecordedCommand(EditorCommand command_) : command(command_) {
		if (!IsRecordableCommand(command)) {
			throw std::invalid_argument("command is not recordable");
		}
	}

	EditorCommand Command() const noexcept {
		return command;
	}

private:
	EditorCommand command;
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
