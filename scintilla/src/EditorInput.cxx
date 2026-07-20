// Scintilla source code edit control
/** @file EditorInput.cxx
 ** Focus, IME interaction mode, mouse dwell and capture, cursor mode, text drag enablement,
 ** and insert-check text replacement.
 **
 ** The Wayland shell reports compositor keyboard focus through SetFocus; HasFocus reflects that
 ** state. Focus transitions redraw, emit FocusIn/FocusOut, cancel modal input modes when focus is
 ** lost, and start or stop caret blinking.
 **
 ** IMEInteraction chooses windowed versus inline composition presentation for the platform IME
 ** path. Mouse dwell time is how long the pointer must stay still before DwellStart; capture flags
 ** control whether press and wheel events are claimed by the editor. Cursor mode overrides the
 ** automatic text/arrow cursor. Drag-drop enablement gates starting an internal text drag from a
 ** selection. ChangeInsertion may be called only from an InsertCheck notification to replace the
 ** text about to be inserted.
 **
 ** Context-menu policy (UsePopUp) lives on ScintillaBase because the popup is owned there; its
 ** definition is in this file with the other input surface.
 **/
// Copyright 1998-2011 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <string_view>

#include "Document.h"
#include "Editor.h"
#include "EditorInputTypes.h"
#include "Platform.h"
#include "Position.h"
#include "ScintillaBase.h"

using namespace Scintilla;
using namespace Scintilla::Internal;

/// Report compositor keyboard focus to the editor. Changing focus redraws, notifies FocusIn or
/// FocusOut, cancels modal modes when focus is lost, and updates caret blink activity.
void Editor::SetFocus(bool focusState) {
	const bool changing = hasFocus != focusState;
	hasFocus = focusState;
	if (changing) {
		Redraw();
	}
	NotifyFocus(hasFocus);
	if (!hasFocus) {
		CancelModes();
	}
	ShowCaretAtCurrentPosition();
}

/// True when the editor believes it has keyboard focus.
bool Editor::HasFocus() const noexcept {
	return hasFocus;
}

/// How the platform IME should present composition: windowed (default) or inline.
void Editor::SetIMEInteraction(IMEInteraction imeInteraction_) {
	imeInteraction = imeInteraction_;
}

/// Current IME presentation mode.
IMEInteraction Editor::GetIMEInteraction() const noexcept {
	return imeInteraction;
}

/// Milliseconds the pointer must stay still before a DwellStart notification.
/// Use a very large value to disable dwell (TimeForever).
void Editor::SetMouseDwellTime(int milliseconds) {
	dwellDelay = milliseconds;
	ticksToDwell = dwellDelay;
}

/// Current dwell delay in milliseconds.
int Editor::GetMouseDwellTime() const noexcept {
	return dwellDelay;
}

/// When true (default), a press inside the editor captures the mouse until release.
void Editor::SetMouseDownCaptures(bool captures) {
	mouseDownCaptures = captures;
}

/// True when mouse-down capture is enabled.
bool Editor::GetMouseDownCaptures() const noexcept {
	return mouseDownCaptures;
}

/// When true (default), the editor may claim mouse-wheel events while focused.
void Editor::SetMouseWheelCaptures(bool captures) {
	mouseWheelCaptures = captures;
}

/// True when mouse-wheel capture is enabled.
bool Editor::GetMouseWheelCaptures() const noexcept {
	return mouseWheelCaptures;
}

/// Force a cursor shape (for example Wait). Normal restores automatic text/arrow choice.
void Editor::SetCursor(CursorShape cursor) {
	cursorMode = cursor;
	DisplayCursor(Window::Cursor::text);
}

/// Last cursor shape set with SetCursor, or Normal if never overridden.
CursorShape Editor::GetCursor() const noexcept {
	return cursorMode;
}

/// When true (default), dragging from a selection can start an internal text drag.
void Editor::SetDragDropEnabled(bool enabled) {
	dragDropEnabled = enabled;
}

/// True when internal text drag is enabled.
bool Editor::GetDragDropEnabled() const noexcept {
	return dragDropEnabled;
}

/// Replace the text about to be inserted. Call only from an InsertCheck modification notification;
/// outside that window the change is ignored by the next insert. Length is the byte count of text.
void Editor::ChangeInsertion(std::string_view text) {
	pdoc->ChangeInsertion(text.data(), static_cast<Sci::Position>(text.length()));
}

/// Context-menu policy: Never, All, or Text (not in the selection margin).
void ScintillaBase::UsePopUp(PopUp popUpMode) {
	displayPopupMenu = popUpMode;
}

/// Current context-menu policy.
PopUp ScintillaBase::GetUsePopUp() const noexcept {
	return displayPopupMenu;
}
