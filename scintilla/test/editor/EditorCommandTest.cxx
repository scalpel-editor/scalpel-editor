// scalpel-editor test code
/** @file EditorCommandTest.cxx
 ** Focused tests for the EditorCommand path: ExecuteCommand, default key
 ** bindings, movement, editing, clipboard, undo/redo, and key-map changes.
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
#include <variant>
#include <vector>

#include "EditorBasicTypes.h"
#include "EditorDocumentTypes.h"
#include "EditorStyleTypes.h"
#include "EditorInputTypes.h"
#include "EditorLayoutTypes.h"
#include "EditorRecording.h"
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

Sci::Position Anchor(TestEditor &editor) {
	return editor.GetAnchor();
}

void Goto(TestEditor &editor, Sci::Position pos) {
	editor.GotoPos(pos);
}

void SelectRange(TestEditor &editor, Sci::Position anchor, Sci::Position caret) {
	editor.SetSel(anchor, caret);
}

}

TEST_CASE("CharRight command and default Right key move the caret") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("abcd");
	Goto(editor, 0);

	editor.RunCommand(EditorCommand::CharRight);
	CHECK(editor.CurrentPos() == 1);

	bool consumed = false;
	editor.KeyDown(Keys::Right, KeyMod::Norm, &consumed);
	CHECK(consumed);
	CHECK(editor.CurrentPos() == 2);
}

TEST_CASE("CharRightExtend grows a stream selection") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("abcd");
	Goto(editor, 1);

	editor.RunCommand(EditorCommand::CharRightExtend);
	CHECK(Anchor(editor) == 1);
	CHECK(editor.CurrentPos() == 2);

	bool consumed = false;
	editor.KeyDown(Keys::Right, KeyMod::Shift, &consumed);
	CHECK(consumed);
	CHECK(Anchor(editor) == 1);
	CHECK(editor.CurrentPos() == 3);
}

TEST_CASE("DeleteBack removes the previous character") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("ab");
	Goto(editor, 2);

	editor.RunCommand(EditorCommand::DeleteBack);
	CHECK(editor.Text() == "a");
	CHECK(editor.CurrentPos() == 1);
}

TEST_CASE("Tab inserts indentation into an empty selection") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("x");
	Goto(editor, 0);
	editor.SetUseTabs(true);
	editor.SetTabWidth(4);

	editor.RunCommand(EditorCommand::Tab);
	CHECK(editor.Text() == "\tx");
}

TEST_CASE("NewLine inserts a line end") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("ab");
	Goto(editor, 1);

	editor.RunCommand(EditorCommand::NewLine);
	// Default EOL is LF in this tree.
	CHECK(editor.Text() == "a\nb");
}

TEST_CASE("UpperCase and LowerCase convert the selection") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("Ab");
	SelectRange(editor, 0, 2);

	editor.RunCommand(EditorCommand::UpperCase);
	CHECK(editor.Text() == "AB");

	SelectRange(editor, 0, 2);
	editor.RunCommand(EditorCommand::LowerCase);
	CHECK(editor.Text() == "ab");
}

TEST_CASE("Copy Cut and Paste use the host clipboard") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("hello");
	SelectRange(editor, 0, 5);

	editor.RunCommand(EditorCommand::Copy);
	CHECK(editor.observations.clipboard == "hello");
	CHECK(editor.Text() == "hello");

	editor.RunCommand(EditorCommand::Cut);
	CHECK(editor.observations.clipboard == "hello");
	CHECK(editor.Text() == "");

	editor.RunCommand(EditorCommand::Paste);
	CHECK(editor.Text() == "hello");
}

TEST_CASE("Cut is refused when the document is read-only") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("keep");
	SelectRange(editor, 0, 4);
	editor.SetReadOnly(true);

	editor.RunCommand(EditorCommand::Cut);
	CHECK(editor.Text() == "keep");
}

TEST_CASE("Undo and Redo restore text after an edit") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("xy");
	Goto(editor, 2);

	editor.RunCommand(EditorCommand::DeleteBack);
	CHECK(editor.Text() == "x");

	editor.RunCommand(EditorCommand::Undo);
	CHECK(editor.Text() == "xy");

	editor.RunCommand(EditorCommand::Redo);
	CHECK(editor.Text() == "x");
}

TEST_CASE("Ctrl+Z key binding undoes like ExecuteCommand") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("z");
	Goto(editor, 1);
	editor.RunCommand(EditorCommand::DeleteBack);
	CHECK(editor.Text() == "");

	bool consumed = false;
	editor.KeyDown(static_cast<Keys>('Z'), KeyMod::Ctrl, &consumed);
	CHECK(consumed);
	CHECK(editor.Text() == "z");
}

TEST_CASE("Unbound key is not consumed") {
	TestHost host;
	TestEditor editor(host);
	bool consumed = true;
	// 'Q' has no default binding in KeyMap::MapDefault
	editor.KeyDown(static_cast<Keys>('Q'), KeyMod::Norm, &consumed);
	CHECK_FALSE(consumed);
}

TEST_CASE("AssignCmdKey rebinds a key to another command") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("ab");
	Goto(editor, 0);

	// Rebind Right to LineEnd instead of CharRight.
	editor.AssignCmdKey(Keys::Right, KeyMod::Norm, EditorCommand::LineEnd);

	bool consumed = false;
	editor.KeyDown(Keys::Right, KeyMod::Norm, &consumed);
	CHECK(consumed);
	CHECK(editor.CurrentPos() == 2);
}

TEST_CASE("ClearCmdKey removes a binding") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("ab");
	Goto(editor, 0);

	editor.ClearCmdKey(Keys::Right, KeyMod::Norm);

	bool consumed = true;
	editor.KeyDown(Keys::Right, KeyMod::Norm, &consumed);
	CHECK_FALSE(consumed);
	CHECK(editor.CurrentPos() == 0);
}

TEST_CASE("LineDown command moves the caret to the next line") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("one\ntwo\nthree");
	Goto(editor, 0);
	editor.RunCommand(EditorCommand::LineDown);
	CHECK(editor.CurrentPos() > 0);
	// "one\n" is 4 bytes; caret should land on the second line.
	CHECK(editor.CurrentPos() == 4);
}

TEST_CASE("Editor commands use a non-zero client top as the view origin") {
	TestHost host;
	TestEditor editor(host, PRectangle(0, 28, 640, 480));
	editor.SetText("one\ntwo\nthree\n");
	Goto(editor, 0);

	editor.RunCommand(EditorCommand::MoveSelectedLinesDown);

	CHECK(editor.Text() == "two\none\nthree\n");
}

TEST_CASE("Bound keyboard commands remain observable to macro recording") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("ab");
	Goto(editor, 0);
	editor.StartRecording();
	editor.ClearObservations();

	bool consumed = false;
	editor.KeyDown(Keys::Right, KeyMod::Norm, &consumed);

	REQUIRE(consumed);
	REQUIRE(editor.observations.recordedActions.size() == 1);
	const RecordedCommand *command =
		std::get_if<RecordedCommand>(&editor.observations.recordedActions[0]);
	REQUIRE(command != nullptr);
	CHECK(command->Command() == EditorCommand::CharRight);
	// Recording delivers only typed RecordedAction values, not notifications.
}

TEST_CASE("Unbound keys are not recorded as macro commands") {
	TestHost host;
	TestEditor editor(host);
	editor.StartRecording();
	editor.ClearObservations();

	bool consumed = true;
	editor.KeyDown(static_cast<Keys>('Q'), KeyMod::Norm, &consumed);

	CHECK_FALSE(consumed);
	CHECK(editor.observations.recordedActions.empty());
	// Unbound keys leave both recorded actions and notifications untouched for recording.
}

TEST_CASE("SelectAll selects the whole document") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("all of it");
	Goto(editor, 3);

	editor.RunCommand(EditorCommand::SelectAll);
	const Sci::Position start = std::min(Anchor(editor), editor.CurrentPos());
	const Sci::Position end = std::max(Anchor(editor), editor.CurrentPos());
	CHECK(start == 0);
	CHECK(end == static_cast<Sci::Position>(editor.Text().size()));
}

TEST_CASE("SetZoom command resets zoom to zero") {
	TestHost host;
	TestEditor editor(host);
	editor.SetZoom(4);
	CHECK(editor.GetZoom() == 4);

	editor.RunCommand(EditorCommand::SetZoom);
	CHECK(editor.GetZoom() == 0);
}

TEST_CASE("Standard zoom hotkeys increase decrease and reset zoom") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("keep");
	CHECK(editor.GetZoom() == 0);

	// Ctrl++ arrives as '+' with Ctrl|Shift on a US layout.
	bool consumed = false;
	editor.KeyDown(static_cast<Keys>('+'), KeyMod::Ctrl | KeyMod::Shift, &consumed);
	CHECK(consumed);
	CHECK(editor.GetZoom() == 1);

	consumed = false;
	editor.KeyDown(static_cast<Keys>('-'), KeyMod::Ctrl, &consumed);
	CHECK(consumed);
	CHECK(editor.GetZoom() == 0);

	consumed = false;
	editor.KeyDown(static_cast<Keys>('-'), KeyMod::Ctrl, &consumed);
	CHECK(consumed);
	CHECK(editor.GetZoom() == -1);

	consumed = false;
	editor.KeyDown(static_cast<Keys>('0'), KeyMod::Ctrl, &consumed);
	CHECK(consumed);
	CHECK(editor.GetZoom() == 0);
	// Zoom shortcuts must not insert text.
	CHECK(editor.Text() == "keep");
}
