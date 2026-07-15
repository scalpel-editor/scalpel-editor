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

Sci::Position Anchor(TestEditor &editor) {
	return static_cast<Sci::Position>(editor.WndProc(Message::GetAnchor, 0, 0));
}

void Goto(TestEditor &editor, Sci::Position pos) {
	editor.WndProc(Message::GotoPos, static_cast<uptr_t>(pos), 0);
}

void SelectRange(TestEditor &editor, Sci::Position anchor, Sci::Position caret) {
	editor.WndProc(Message::SetSel, static_cast<uptr_t>(anchor), caret);
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
	editor.WndProc(Message::SetUseTabs, 1, 0);
	editor.WndProc(Message::SetTabWidth, 4, 0);

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
	editor.WndProc(Message::SetReadOnly, 1, 0);

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
	const uptr_t keyWithMods = static_cast<uptr_t>(Keys::Right);
	editor.WndProc(Message::AssignCmdKey, keyWithMods,
		static_cast<sptr_t>(Message::LineEnd));

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

	const uptr_t keyWithMods = static_cast<uptr_t>(Keys::Right);
	editor.WndProc(Message::ClearCmdKey, keyWithMods, 0);

	bool consumed = true;
	editor.KeyDown(Keys::Right, KeyMod::Norm, &consumed);
	CHECK_FALSE(consumed);
	CHECK(editor.CurrentPos() == 0);
}

TEST_CASE("Message path and ExecuteCommand match for LineDown") {
	Sci::Position commandPos = 0;
	{
		TestHost host;
		TestEditor viaCommand(host);
		viaCommand.SetText("one\ntwo\nthree");
		Goto(viaCommand, 0);
		viaCommand.RunCommand(EditorCommand::LineDown);
		commandPos = viaCommand.CurrentPos();
	}
	{
		TestHost host;
		TestEditor viaMessage(host);
		viaMessage.SetText("one\ntwo\nthree");
		Goto(viaMessage, 0);
		viaMessage.WndProc(Message::LineDown, 0, 0);
		CHECK(viaMessage.CurrentPos() == commandPos);
		CHECK(commandPos > 0);
	}
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
	editor.WndProc(Message::SetZoom, 4, 0);
	CHECK(editor.WndProc(Message::GetZoom, 0, 0) == 4);

	editor.RunCommand(EditorCommand::SetZoom);
	CHECK(editor.WndProc(Message::GetZoom, 0, 0) == 0);
}
