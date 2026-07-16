// scalpel-editor test code
/** @file EditorDocumentTest.cxx
 ** Focused behavior tests for whole-document text operations.
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

}

TEST_CASE("SetText replaces content and reports dirty state") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("initial");
	editor.SetSavePoint();
	editor.ClearObservations();

	editor.SetText("hello");
	editor.FlushUpdateNotifications();

	CHECK(editor.GetText() == "hello");
	CHECK(editor.Text() == "hello");
	CHECK(editor.GetTextLength() == 5);
	CHECK(editor.GetLength() == 5);
	CHECK(editor.GetModify());
	CHECK(editor.CurrentPos() == 0);
	CHECK(HasNotification(editor, Notification::SavePointLeft));
}

TEST_CASE("GetText returns the document bytes") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("abc");

	CHECK(editor.GetText() == "abc");
	CHECK(editor.GetTextLength() == 3);
}

TEST_CASE("Read-only rejects SetText and notifies ModifyAttemptRO") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("keep");
	editor.SetReadOnly(true);
	editor.ClearObservations();

	editor.SetText("gone");

	CHECK(editor.GetText() == "keep");
	CHECK(editor.GetReadOnly());
	CHECK(HasNotification(editor, Notification::ModifyAttemptRO));
}

TEST_CASE("Invalid UTF-8 round-trips through SetText and GetText") {
	TestHost host;
	TestEditor editor(host);
	const char raw[] = {'a', static_cast<char>(0xff), static_cast<char>(0x80), 'b'};
	const std::string bytes(raw, sizeof(raw));
	editor.SetText(bytes);

	CHECK(editor.GetText() == bytes);
	CHECK(editor.GetCharAt(1) == static_cast<char>(0xff));
	CHECK(editor.GetTextLength() == 4);
}

TEST_CASE("AddText InsertText AppendText and DeleteRange change the document") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("xy");
	// caret at 0 after SetText
	editor.AddText("A");
	CHECK(editor.GetText() == "Axy");
	CHECK(editor.CurrentPos() == 1);

	editor.InsertText(2, "B");
	CHECK(editor.GetText() == "AxBy");

	editor.AppendText("Z");
	CHECK(editor.GetText() == "AxByZ");

	editor.DeleteRange(1, 2);
	CHECK(editor.GetText() == "AyZ");
}

TEST_CASE("ClearAll empties the document and Clear deletes the selection") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("abcd");
	editor.SetSel(1, 3);
	editor.RunCommand(EditorCommand::Clear);
	CHECK(editor.GetText() == "ad");

	editor.ClearAll();
	CHECK(editor.GetText().empty());
	CHECK(editor.GetTextLength() == 0);
}

TEST_CASE("GetTextRange and GetStyledText copy ranges") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("wxyz");

	char rangeBuf[8] = {};
	CHECK(editor.GetTextRange(rangeBuf, 1, 3) == 2);
	CHECK(std::string(rangeBuf) == "xy");

	char styled[16] = {};
	CHECK(editor.GetStyledText(styled, 0, 2) == 4);
	CHECK(styled[0] == 'w');
	CHECK(styled[2] == 'x');
}

TEST_CASE("Named document mutators and queries are consistent") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("base");
	editor.AppendText("!!");
	CHECK(editor.GetText() == "base!!");

	editor.DeleteRange(4, 2);
	CHECK(editor.GetText() == "base");

	CHECK(editor.GetCharAt(1) == 'a');
	CHECK(editor.GetLength() == 4);
	CHECK(editor.GetModify());
	CHECK_FALSE(editor.GetReadOnly());
}

TEST_CASE("Allocate GetRangePointer and GetGapPosition are usable") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("ptr");
	editor.Allocate(64);
	const char *p = editor.GetRangePointer(0, 3);
	REQUIRE(p != nullptr);
	CHECK(std::string_view(p, 3) == "ptr");
	CHECK(editor.GetGapPosition() >= 0);
}
