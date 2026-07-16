// scalpel-editor test code
/** @file EditorDocumentTest.cxx
 ** Focused behavior tests for whole-document text operations.
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

struct TextSnapshot {
	std::string text;
	Sci::Position length = 0;
	Sci::Position caret = 0;
	bool modified = false;
	bool readOnly = false;
	int modifyAttemptRO = 0;
	int savePointLeft = 0;
	int savePointReached = 0;

	bool operator==(const TextSnapshot &other) const noexcept {
		return text == other.text
			&& length == other.length
			&& caret == other.caret
			&& modified == other.modified
			&& readOnly == other.readOnly
			&& modifyAttemptRO == other.modifyAttemptRO
			&& savePointLeft == other.savePointLeft
			&& savePointReached == other.savePointReached;
	}
};

TextSnapshot Capture(const TestEditor &editor) {
	TextSnapshot s;
	s.text = editor.GetText();
	s.length = editor.GetTextLength();
	s.caret = editor.CurrentPos();
	s.modified = editor.GetModify();
	s.readOnly = editor.GetReadOnly();
	for (const TestNotification &n : editor.observations.notifications) {
		if (n.code == Notification::ModifyAttemptRO) {
			s.modifyAttemptRO++;
		} else if (n.code == Notification::SavePointLeft) {
			s.savePointLeft++;
		} else if (n.code == Notification::SavePointReached) {
			s.savePointReached++;
		}
	}
	return s;
}

void MessageAddText(TestEditor &editor, std::string_view text) {
	editor.WndProc(Message::AddText, text.size(),
		reinterpret_cast<sptr_t>(text.data()));
}

void MessageInsertText(TestEditor &editor, Sci::Position pos, const char *text) {
	editor.WndProc(Message::InsertText, static_cast<uptr_t>(pos),
		reinterpret_cast<sptr_t>(text));
}

void MessageAppendText(TestEditor &editor, std::string_view text) {
	editor.WndProc(Message::AppendText, text.size(),
		reinterpret_cast<sptr_t>(text.data()));
}

void MessageDeleteRange(TestEditor &editor, Sci::Position start, Sci::Position length) {
	editor.WndProc(Message::DeleteRange, static_cast<uptr_t>(start), length);
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
	CHECK(editor.WndProc(Message::GetLength, 0, 0) == 5);
	CHECK(editor.GetModify());
	CHECK(editor.CurrentPos() == 0);
	CHECK(HasNotification(editor, Notification::SavePointLeft));
}

TEST_CASE("GetText and message GetText return the same bytes") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("abc");

	const std::string named = editor.GetText();
	char buffer[8] = {};
	const Sci::Position n = static_cast<Sci::Position>(
		editor.WndProc(Message::GetText, sizeof(buffer),
			reinterpret_cast<sptr_t>(buffer)));

	CHECK(named == "abc");
	CHECK(n == 3);
	CHECK(std::string(buffer) == "abc");
	CHECK(editor.WndProc(Message::GetText, 0, 0) == 3);
}

TEST_CASE("SetText message path matches named method") {
	TextSnapshot fromNamed;
	{
		TestHost host;
		TestEditor editor(host);
		editor.SetSavePoint();
		editor.ClearObservations();
		editor.SetText("via-name");
		editor.FlushUpdateNotifications();
		fromNamed = Capture(editor);
	}
	TextSnapshot fromMessage;
	{
		TestHost host;
		TestEditor editor(host);
		editor.SetSavePoint();
		editor.ClearObservations();
		const char *text = "via-name";
		editor.WndProc(Message::SetText, 0, reinterpret_cast<sptr_t>(text));
		editor.FlushUpdateNotifications();
		fromMessage = Capture(editor);
	}
	CHECK(fromNamed == fromMessage);
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
	CHECK(editor.WndProc(Message::GetCharAt, 1, 0) == static_cast<sptr_t>(static_cast<char>(0xff)));
	CHECK(editor.GetTextLength() == 4);
}

TEST_CASE("AddText InsertText AppendText and DeleteRange change the document") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("xy");
	// caret at 0 after SetText
	MessageAddText(editor, "A");
	CHECK(editor.GetText() == "Axy");
	CHECK(editor.CurrentPos() == 1);

	MessageInsertText(editor, 2, "B");
	CHECK(editor.GetText() == "AxBy");

	MessageAppendText(editor, "Z");
	CHECK(editor.GetText() == "AxByZ");

	MessageDeleteRange(editor, 1, 2);
	CHECK(editor.GetText() == "AyZ");
}

TEST_CASE("ClearAll empties the document and Clear deletes the selection") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("abcd");
	editor.WndProc(Message::SetSel, 1, 3);
	editor.RunCommand(EditorCommand::Clear);
	CHECK(editor.GetText() == "ad");

	editor.WndProc(Message::ClearAll, 0, 0);
	CHECK(editor.GetText().empty());
	CHECK(editor.GetTextLength() == 0);
}

TEST_CASE("GetTextRange and GetStyledText copy ranges") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("wxyz");

	char rangeBuf[8] = {};
	TextRange tr{};
	tr.lpstrText = rangeBuf;
	tr.chrg.cpMin = 1;
	tr.chrg.cpMax = 3;
	CHECK(editor.WndProc(Message::GetTextRange, 0, reinterpret_cast<sptr_t>(&tr)) == 2);
	CHECK(std::string(rangeBuf) == "xy");

	char styled[16] = {};
	TextRange str{};
	str.lpstrText = styled;
	str.chrg.cpMin = 0;
	str.chrg.cpMax = 2;
	CHECK(editor.WndProc(Message::GetStyledText, 0, reinterpret_cast<sptr_t>(&str)) == 4);
	CHECK(styled[0] == 'w');
	CHECK(styled[2] == 'x');
}

TEST_CASE("Message mutators forward to named methods") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("base");
	const char *more = "!!";
	editor.WndProc(Message::AppendText, 2, reinterpret_cast<sptr_t>(more));
	CHECK(editor.GetText() == "base!!");

	editor.WndProc(Message::DeleteRange, 4, 2);
	CHECK(editor.GetText() == "base");

	CHECK(editor.WndProc(Message::GetCharAt, 1, 0) == static_cast<sptr_t>('a'));
	CHECK(editor.WndProc(Message::GetLength, 0, 0) == 4);
	CHECK(editor.WndProc(Message::GetModify, 0, 0) != 0);
	CHECK(editor.WndProc(Message::GetReadOnly, 0, 0) == 0);
}

TEST_CASE("Deleted document-sharing messages do nothing") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("solo");

	// No multi-document API: cases fall through to DefWndProc.
	CHECK(editor.WndProc(Message::GetDocPointer, 0, 0) == 0);
	CHECK(editor.WndProc(Message::CreateDocument, 0, 0) == 0);
	CHECK(editor.WndProc(Message::CreateLoader, 0, 0) == 0);
	CHECK(editor.WndProc(Message::GetDocumentOptions, 0, 0) == 0);
	CHECK(editor.GetText() == "solo");
}

TEST_CASE("Allocate GetRangePointer and GetGapPosition are usable") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("ptr");
	editor.WndProc(Message::Allocate, 64, 0);
	const sptr_t p = editor.WndProc(Message::GetRangePointer, 0, 3);
	REQUIRE(p != 0);
	CHECK(std::string_view(reinterpret_cast<const char *>(p), 3) == "ptr");
	CHECK(editor.WndProc(Message::GetGapPosition, 0, 0) >= 0);
}
