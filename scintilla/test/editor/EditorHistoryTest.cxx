// scalpel-editor test code
/** @file EditorHistoryTest.cxx
 ** Focused behavior tests for undo, redo, save point, and undo groups.
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

void TypeAtEnd(TestEditor &editor, std::string_view text) {
	editor.GotoPos(editor.GetTextLength());
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

TEST_CASE("Undo and Redo commands restore text") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "z");
	TypeAtEnd(editor, "!");
	CHECK(editor.GetText() == "z!");

	editor.RunCommand(EditorCommand::Undo);
	CHECK(editor.GetText() == "z");
	editor.RunCommand(EditorCommand::Redo);
	CHECK(editor.GetText() == "z!");

	CHECK(editor.CanUndo());
	CHECK_FALSE(editor.CanRedo());
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
	CHECK(editor.GetUndoSequence() == 0);

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

	editor.EmptyUndoBuffer();
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
	editor.SetUndoCollection(false);
	editor.EmptyUndoBuffer();
	TypeAtEnd(editor, "!");
	CHECK(editor.GetText() == "base!");
	CHECK_FALSE(editor.CanUndo());
	CHECK_FALSE(editor.GetUndoCollection());

	editor.SetUndoCollection(true);
	CHECK(editor.GetUndoCollection());
}

TEST_CASE("Named history methods group and undo edits") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "m");
	editor.SetSavePoint();
	CHECK_FALSE(editor.GetModify());

	editor.BeginUndoAction();
	TypeAtEnd(editor, "1");
	TypeAtEnd(editor, "2");
	editor.EndUndoAction();
	CHECK(editor.GetText() == "m12");

	editor.RunCommand(EditorCommand::Undo);
	CHECK(editor.GetText() == "m");
	editor.RunCommand(EditorCommand::Redo);
	CHECK(editor.GetText() == "m12");
}

TEST_CASE("Change history option round trips") {
	TestHost host;
	TestEditor editor(host);
	CHECK(editor.GetChangeHistory() == ChangeHistoryOption::Disabled);
	editor.SetChangeHistory(ChangeHistoryOption::Enabled);
	CHECK(editor.GetChangeHistory() == ChangeHistoryOption::Enabled);
}
