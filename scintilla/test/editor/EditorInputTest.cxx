// scalpel-editor test code
/** @file EditorInputTest.cxx
 ** Focused behavior tests for focus, IME, dwell, capture, cursor, drag-drop,
 ** ChangeInsertion, overtype, context-menu policy, and key-map configuration.
 **/

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

TEST_CASE("Focus set invalidates and notifies FocusIn") {
	TestHost host;
	TestEditor editor(host);
	editor.ClearObservations();
	editor.SetFocus(true);
	CHECK(editor.HasFocus());
	CHECK(HasNotification(editor, Notification::FocusIn));
	CHECK(editor.Snapshot().invalidatedRectangles >= 1);
}
