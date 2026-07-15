// scalpel-editor test code
/** @file EditorInputTest.cxx
 ** Focused behavior tests for focus, IME, dwell, capture, cursor, drag-drop,
 ** ChangeInsertion, overtype, context-menu policy, and key-map configuration.
 **/

#include <array>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <forward_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
#include "Document.h"
#include "UniConversion.h"
#include "Selection.h"
#include "PositionCache.h"
#include "EditModel.h"
#include "MarginView.h"
#include "EditView.h"
#include "Editor.h"
#include "AutoComplete.h"
#include "CallTip.h"
#include "ScintillaBase.h"

#include "TestPlatform.h"
#include "TestEditor.h"

#include "catch.hpp"

using namespace Scintilla;
using namespace Scintilla::Internal;

namespace {

bool HasNotification(const TestEditor &editor, Notification code) {
	return std::any_of(editor.observations.notifications.begin(),
		editor.observations.notifications.end(),
		[code](const TestNotification &n) { return n.code == code; });
}

bool HasCaretTickerStarted(const TestEditor &editor) {
	// TickReason::caret is 0 (protected enum); compare by numeric id.
	constexpr int caretReason = 0;
	return std::any_of(editor.observations.tickerRequests.begin(),
		editor.observations.tickerRequests.end(),
		[](const TestTickerRequest &t) {
			return t.reason == caretReason && t.started;
		});
}

struct InputSnapshot {
	bool focus = false;
	IMEInteraction ime = IMEInteraction::Windowed;
	int dwell = 0;
	bool mouseDownCaptures = true;
	bool mouseWheelCaptures = true;
	CursorShape cursor = CursorShape::Normal;
	bool dragDrop = true;
	bool overtype = false;
	PopUp popUp = PopUp::All;
	size_t invalidatedRectangles = 0;
	bool focusIn = false;
	bool focusOut = false;

	bool operator==(const InputSnapshot &other) const noexcept {
		return focus == other.focus
			&& ime == other.ime
			&& dwell == other.dwell
			&& mouseDownCaptures == other.mouseDownCaptures
			&& mouseWheelCaptures == other.mouseWheelCaptures
			&& cursor == other.cursor
			&& dragDrop == other.dragDrop
			&& overtype == other.overtype
			&& popUp == other.popUp
			&& invalidatedRectangles == other.invalidatedRectangles
			&& focusIn == other.focusIn
			&& focusOut == other.focusOut;
	}
};

InputSnapshot CaptureNamed(TestEditor &editor) {
	editor.ClearObservations();
	editor.SetFocus(true);
	editor.SetIMEInteraction(IMEInteraction::Inline);
	editor.SetMouseDwellTime(400);
	editor.SetMouseDownCaptures(false);
	editor.SetMouseWheelCaptures(false);
	editor.SetCursor(CursorShape::Wait);
	editor.SetDragDropEnabled(false);
	editor.SetOvertype(true);
	editor.UsePopUp(PopUp::Never);

	InputSnapshot s;
	s.focus = editor.HasFocus();
	s.ime = editor.GetIMEInteraction();
	s.dwell = editor.GetMouseDwellTime();
	s.mouseDownCaptures = editor.GetMouseDownCaptures();
	s.mouseWheelCaptures = editor.GetMouseWheelCaptures();
	s.cursor = editor.GetCursor();
	s.dragDrop = editor.GetDragDropEnabled();
	s.overtype = editor.GetOvertype();
	s.popUp = editor.GetUsePopUp();
	s.invalidatedRectangles = editor.Snapshot().invalidatedRectangles;
	s.focusIn = HasNotification(editor, Notification::FocusIn);
	s.focusOut = HasNotification(editor, Notification::FocusOut);
	return s;
}

InputSnapshot CaptureMessage(TestEditor &editor) {
	editor.ClearObservations();
	editor.WndProc(Message::SetFocus, 1, 0);
	editor.WndProc(Message::SetIMEInteraction, static_cast<uptr_t>(IMEInteraction::Inline), 0);
	editor.WndProc(Message::SetMouseDwellTime, 400, 0);
	editor.WndProc(Message::SetMouseDownCaptures, 0, 0);
	editor.WndProc(Message::SetMouseWheelCaptures, 0, 0);
	editor.WndProc(Message::SetCursor, static_cast<uptr_t>(CursorShape::Wait), 0);
	editor.WndProc(Message::SetDragDropEnabled, 0, 0);
	editor.WndProc(Message::SetOvertype, 1, 0);
	editor.WndProc(Message::UsePopUp, static_cast<uptr_t>(PopUp::Never), 0);

	InputSnapshot s;
	s.focus = editor.WndProc(Message::GetFocus, 0, 0) != 0;
	s.ime = static_cast<IMEInteraction>(editor.WndProc(Message::GetIMEInteraction, 0, 0));
	s.dwell = static_cast<int>(editor.WndProc(Message::GetMouseDwellTime, 0, 0));
	s.mouseDownCaptures = editor.WndProc(Message::GetMouseDownCaptures, 0, 0) != 0;
	s.mouseWheelCaptures = editor.WndProc(Message::GetMouseWheelCaptures, 0, 0) != 0;
	s.cursor = static_cast<CursorShape>(editor.WndProc(Message::GetCursor, 0, 0));
	s.dragDrop = editor.WndProc(Message::GetDragDropEnabled, 0, 0) != 0;
	s.overtype = editor.WndProc(Message::GetOvertype, 0, 0) != 0;
	s.popUp = editor.GetUsePopUp();
	s.invalidatedRectangles = editor.Snapshot().invalidatedRectangles;
	s.focusIn = HasNotification(editor, Notification::FocusIn);
	s.focusOut = HasNotification(editor, Notification::FocusOut);
	return s;
}

}

TEST_CASE("Focus on and off notifies and affects caret ticker") {
	TestHost host;
	TestEditor editor(host);

	CHECK_FALSE(editor.HasFocus());
	editor.ClearObservations();
	editor.SetFocus(true);
	CHECK(editor.HasFocus());
	CHECK(HasNotification(editor, Notification::FocusIn));
	CHECK_FALSE(HasNotification(editor, Notification::FocusOut));
	// Focus enables caret blink ticker request.
	CHECK(HasCaretTickerStarted(editor));

	editor.ClearObservations();
	editor.SetFocus(false);
	CHECK_FALSE(editor.HasFocus());
	CHECK(HasNotification(editor, Notification::FocusOut));
	CHECK_FALSE(HasNotification(editor, Notification::FocusIn));
}

TEST_CASE("IME dwell capture cursor drag-drop and popup round-trip") {
	TestHost host;
	TestEditor editor(host);

	CHECK(editor.GetIMEInteraction() == IMEInteraction::Windowed);
	editor.SetIMEInteraction(IMEInteraction::Inline);
	CHECK(editor.GetIMEInteraction() == IMEInteraction::Inline);

	editor.SetMouseDwellTime(250);
	CHECK(editor.GetMouseDwellTime() == 250);

	CHECK(editor.GetMouseDownCaptures());
	editor.SetMouseDownCaptures(false);
	CHECK_FALSE(editor.GetMouseDownCaptures());
	editor.SetMouseWheelCaptures(false);
	CHECK_FALSE(editor.GetMouseWheelCaptures());

	CHECK(editor.GetCursor() == CursorShape::Normal);
	editor.SetCursor(CursorShape::Wait);
	CHECK(editor.GetCursor() == CursorShape::Wait);

	CHECK(editor.GetDragDropEnabled());
	editor.SetDragDropEnabled(false);
	CHECK_FALSE(editor.GetDragDropEnabled());

	CHECK(editor.GetUsePopUp() == PopUp::All);
	editor.UsePopUp(PopUp::Text);
	CHECK(editor.GetUsePopUp() == PopUp::Text);
	editor.UsePopUp(PopUp::Never);
	CHECK(editor.GetUsePopUp() == PopUp::Never);
}

TEST_CASE("Overtype set toggle and command") {
	TestHost host;
	TestEditor editor(host);

	CHECK_FALSE(editor.GetOvertype());
	editor.SetOvertype(true);
	CHECK(editor.GetOvertype());
	editor.RunCommand(EditorCommand::EditToggleOvertype);
	CHECK_FALSE(editor.GetOvertype());
	editor.RunCommand(EditorCommand::EditToggleOvertype);
	CHECK(editor.GetOvertype());
}

TEST_CASE("ChangeInsertion replaces text during InsertCheck") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("");
	editor.observations.changeInsertionOnInsertCheck = "XYZ";
	editor.InsertInput("ab");
	CHECK(editor.Text() == "XYZ");
}

TEST_CASE("ChangeInsertion accepts invalid UTF-8 bytes") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("");
	// Lone continuation byte is stored as-is under the phase-3 policy.
	editor.observations.changeInsertionOnInsertCheck = std::string("\x80", 1);
	editor.InsertInput("a");
	CHECK(editor.Text() == "\x80");
	CHECK(editor.GetTextLength() == 1);
}

TEST_CASE("AssignCmdKey ClearCmdKey and ClearAllCmdKeys") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("abcd");
	editor.GotoPos(0);

	// Rebind Right to CharLeft.
	editor.AssignCmdKey(Keys::Right, KeyMod::Norm, EditorCommand::CharLeft);
	bool consumed = false;
	editor.KeyDown(Keys::Right, KeyMod::Norm, &consumed);
	CHECK(consumed);
	// At position 0, CharLeft stays at 0.
	CHECK(editor.CurrentPos() == 0);

	editor.ClearCmdKey(Keys::Right, KeyMod::Norm);
	editor.GotoPos(1);
	consumed = false;
	editor.KeyDown(Keys::Right, KeyMod::Norm, &consumed);
	// Unbound key is not consumed by a command.
	CHECK_FALSE(consumed);

	editor.AssignCmdKey(Keys::Right, KeyMod::Norm, EditorCommand::CharRight);
	editor.ClearAllCmdKeys();
	consumed = false;
	editor.KeyDown(Keys::Right, KeyMod::Norm, &consumed);
	CHECK_FALSE(consumed);
}

TEST_CASE("Input named path matches message path") {
	InputSnapshot named;
	InputSnapshot message;
	{
		TestHost host;
		TestEditor editor(host);
		named = CaptureNamed(editor);
	}
	{
		TestHost host;
		TestEditor editor(host);
		message = CaptureMessage(editor);
	}
	CHECK(named == message);
}

TEST_CASE("Deleted accessibility and GrabFocus messages fall through without changing focus") {
	TestHost host;
	TestEditor editor(host);

	// Accessibility messages are removed from dispatch; they fall through to DefWndProc.
	editor.WndProc(Message::GetAccessibility, 0, 0);
	editor.WndProc(Message::SetAccessibility, 1, 0);
	CHECK(editor.observations.defaultWindowCalls.size() >= 1);

	editor.ClearObservations();
	editor.WndProc(Message::GrabFocus, 0, 0);
	CHECK(std::find(editor.observations.defaultWindowCalls.begin(),
		editor.observations.defaultWindowCalls.end(),
		Message::GrabFocus) != editor.observations.defaultWindowCalls.end());
	CHECK_FALSE(editor.HasFocus());
	// KeysUnicode was already removed from the Message enum in phase 3.
}
