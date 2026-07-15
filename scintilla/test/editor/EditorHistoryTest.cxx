// scalpel-editor test code
/** @file EditorHistoryTest.cxx
 ** Focused behavior tests for undo, redo, save point, and undo groups.
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

// Load text, clear undo history from the load, and mark the save point.
void LoadClean(TestEditor &editor, std::string_view text) {
	editor.SetText(text);
	editor.WndProc(Message::EmptyUndoBuffer, 0, 0);
	editor.SetSavePoint();
}

void TypeAtEnd(TestEditor &editor, std::string_view text) {
	editor.WndProc(Message::GotoPos, static_cast<uptr_t>(editor.GetTextLength()), 0);
	editor.InsertInput(text);
}

}

TEST_CASE("CanUndo and CanRedo follow edits and history") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "ab");

	CHECK_FALSE(editor.CanUndo());
	CHECK_FALSE(editor.CanRedo());

	TypeAtEnd(editor, "x");
	CHECK(editor.CanUndo());
	CHECK_FALSE(editor.CanRedo());
	CHECK(editor.GetModify());

	editor.RunCommand(EditorCommand::Undo);
	CHECK(editor.GetText() == "ab");
	CHECK_FALSE(editor.CanUndo());
	CHECK(editor.CanRedo());
	CHECK_FALSE(editor.GetModify());

	editor.RunCommand(EditorCommand::Redo);
	CHECK(editor.GetText() == "abx");
	CHECK(editor.CanUndo());
	CHECK_FALSE(editor.CanRedo());
}

TEST_CASE("Undo and Redo commands match named methods") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "z");
	TypeAtEnd(editor, "!");
	CHECK(editor.GetText() == "z!");

	editor.RunCommand(EditorCommand::Undo);
	CHECK(editor.GetText() == "z");
	editor.RunCommand(EditorCommand::Redo);
	CHECK(editor.GetText() == "z!");

	CHECK(editor.WndProc(Message::CanUndo, 0, 0) == (editor.CanUndo() ? 1 : 0));
	CHECK(editor.WndProc(Message::CanRedo, 0, 0) == (editor.CanRedo() ? 1 : 0));
}

TEST_CASE("SetSavePoint clears dirty and notifies") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("doc");
	editor.ClearObservations();

	editor.SetSavePoint();
	CHECK_FALSE(editor.GetModify());
	CHECK(HasNotification(editor, Notification::SavePointReached));

	editor.ClearObservations();
	TypeAtEnd(editor, "!");
	CHECK(editor.GetModify());
	CHECK(HasNotification(editor, Notification::SavePointLeft));
}

TEST_CASE("BeginUndoAction groups edits into one undo step") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "");

	editor.BeginUndoAction();
	TypeAtEnd(editor, "a");
	TypeAtEnd(editor, "b");
	editor.EndUndoAction();
	CHECK(editor.GetText() == "ab");
	CHECK(editor.WndProc(Message::GetUndoSequence, 0, 0) == 0);

	editor.RunCommand(EditorCommand::Undo);
	CHECK(editor.GetText().empty());
	CHECK(editor.CanRedo());
}

TEST_CASE("EmptyUndoBuffer drops history without SavePointReached") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "x");
	TypeAtEnd(editor, "y");
	REQUIRE(editor.CanUndo());
	editor.ClearObservations();

	editor.WndProc(Message::EmptyUndoBuffer, 0, 0);
	CHECK_FALSE(editor.CanUndo());
	CHECK_FALSE(editor.CanRedo());
	CHECK_FALSE(HasNotification(editor, Notification::SavePointReached));
}

TEST_CASE("Read-only blocks CanUndo for menu enablement") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "ro");
	TypeAtEnd(editor, "!");
	REQUIRE(editor.CanUndo());
	editor.SetReadOnly(true);
	CHECK_FALSE(editor.CanUndo());
	CHECK_FALSE(editor.CanRedo());
}

TEST_CASE("Undo collection can be turned off") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "base");
	editor.WndProc(Message::SetUndoCollection, 0, 0);
	editor.WndProc(Message::EmptyUndoBuffer, 0, 0);
	TypeAtEnd(editor, "!");
	CHECK(editor.GetText() == "base!");
	CHECK_FALSE(editor.CanUndo());
	CHECK(editor.WndProc(Message::GetUndoCollection, 0, 0) == 0);

	editor.WndProc(Message::SetUndoCollection, 1, 0);
	CHECK(editor.WndProc(Message::GetUndoCollection, 0, 0) != 0);
}

TEST_CASE("Message path matches named history methods") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "m");
	editor.WndProc(Message::SetSavePoint, 0, 0);
	CHECK_FALSE(editor.GetModify());

	editor.WndProc(Message::BeginUndoAction, 0, 0);
	TypeAtEnd(editor, "1");
	TypeAtEnd(editor, "2");
	editor.WndProc(Message::EndUndoAction, 0, 0);
	CHECK(editor.GetText() == "m12");

	editor.WndProc(Message::Undo, 0, 0);
	CHECK(editor.GetText() == "m");
	editor.WndProc(Message::Redo, 0, 0);
	CHECK(editor.GetText() == "m12");
}

TEST_CASE("Change history option round trips") {
	TestHost host;
	TestEditor editor(host);
	CHECK(static_cast<ChangeHistoryOption>(
		editor.WndProc(Message::GetChangeHistory, 0, 0)) == ChangeHistoryOption::Disabled);
	editor.WndProc(Message::SetChangeHistory,
		static_cast<uptr_t>(ChangeHistoryOption::Enabled), 0);
	CHECK(static_cast<ChangeHistoryOption>(
		editor.WndProc(Message::GetChangeHistory, 0, 0)) == ChangeHistoryOption::Enabled);
}
