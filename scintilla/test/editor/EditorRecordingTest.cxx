// scalpel-editor test code
/** @file EditorRecordingTest.cxx
 ** Typed recording contract, lifecycle, command capture, and replay tests.
 **/

#include <algorithm>
#include <array>
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
#include "EditorRecording.h"
#include "AutoComplete.h"
#include "CallTip.h"
#include "ScintillaBase.h"

#include "TestPlatform.h"
#include "TestEditor.h"

#include "catch.hpp"

using namespace Scintilla;
using namespace Scintilla::Internal;

namespace {

// Deliver action through a callback, then destroy the original so the host
// vector must hold an independent copy (especially for owned text).
void DeliverAndDropOriginal(const RecordingCallback &callback, RecordedAction action) {
	callback(action);
}

template <typename T>
const T &As(const RecordedAction &action) {
	const T *value = std::get_if<T>(&action);
	REQUIRE(value != nullptr);
	return *value;
}

}

TEST_CASE("Recording host retains every RecordedAction alternative after callback return") {
	TestHost host;
	TestEditor editor(host);
	const RecordingCallback callback = editor.MakeRecordingCallback();

	DeliverAndDropOriginal(callback, RecordedCommand{EditorCommand::CharRight});
	DeliverAndDropOriginal(callback, RecordedReplaceSelection{"hello"});
	DeliverAndDropOriginal(callback, RecordedAddText{"add"});
	DeliverAndDropOriginal(callback, RecordedInsertText{4, "ins"});
	DeliverAndDropOriginal(callback, RecordedAppendText{"app"});
	DeliverAndDropOriginal(callback, RecordedClearAll{});
	DeliverAndDropOriginal(callback, RecordedGotoLine{3});
	DeliverAndDropOriginal(callback, RecordedGotoPos{12});
	DeliverAndDropOriginal(callback, RecordedSearchAnchor{});
	DeliverAndDropOriginal(callback, RecordedSearch{
		SearchDirection::Prev, FindOption::MatchCase | FindOption::WholeWord, "needle"});
	DeliverAndDropOriginal(callback, RecordedSetSelectionMode{SelectionMode::Rectangle});

	REQUIRE(editor.observations.recordedActions.size() == 11);

	CHECK(As<RecordedCommand>(editor.observations.recordedActions[0]).Command() ==
		EditorCommand::CharRight);
	CHECK(As<RecordedReplaceSelection>(editor.observations.recordedActions[1]).text == "hello");
	CHECK(As<RecordedAddText>(editor.observations.recordedActions[2]).text == "add");
	CHECK(As<RecordedInsertText>(editor.observations.recordedActions[3]).position == 4);
	CHECK(As<RecordedInsertText>(editor.observations.recordedActions[3]).text == "ins");
	CHECK(As<RecordedAppendText>(editor.observations.recordedActions[4]).text == "app");
	CHECK(std::holds_alternative<RecordedClearAll>(editor.observations.recordedActions[5]));
	CHECK(As<RecordedGotoLine>(editor.observations.recordedActions[6]).line == 3);
	CHECK(As<RecordedGotoPos>(editor.observations.recordedActions[7]).position == 12);
	CHECK(std::holds_alternative<RecordedSearchAnchor>(editor.observations.recordedActions[8]));
	const RecordedSearch &search = As<RecordedSearch>(editor.observations.recordedActions[9]);
	CHECK(search.direction == SearchDirection::Prev);
	CHECK(search.flags == (FindOption::MatchCase | FindOption::WholeWord));
	CHECK(search.text == "needle");
	CHECK(As<RecordedSetSelectionMode>(editor.observations.recordedActions[10]).mode ==
		SelectionMode::Rectangle);
}

TEST_CASE("RecordedCommand rejects actions with dedicated or ignored recording forms") {
	CHECK_FALSE(IsRecordableCommand(EditorCommand::SearchAnchor));
	CHECK_FALSE(IsRecordableCommand(EditorCommand::SearchNext));
	CHECK_FALSE(IsRecordableCommand(EditorCommand::SearchPrev));
	CHECK_FALSE(IsRecordableCommand(EditorCommand::Undo));
	CHECK_FALSE(IsRecordableCommand(EditorCommand::Redo));
	CHECK_FALSE(IsRecordableCommand(EditorCommand::NewLine));
	CHECK_FALSE(IsRecordableCommand(EditorCommand::ZoomIn));
	CHECK_FALSE(IsRecordableCommand(EditorCommand::None));

	CHECK_THROWS_AS(RecordedCommand{EditorCommand::SearchNext}, std::invalid_argument);
	CHECK_THROWS_AS(RecordedCommand{EditorCommand::Undo}, std::invalid_argument);
	CHECK_NOTHROW(RecordedCommand{EditorCommand::CharRight});
}

TEST_CASE("Recording host keeps multi-byte and invalid UTF-8 text after the original is gone") {
	TestHost host;
	TestEditor editor(host);
	const RecordingCallback callback = editor.MakeRecordingCallback();

	// U+00E9 in UTF-8, then a lone continuation byte (invalid under phase 3 policy).
	const std::string multiByte = "\xC3\xA9";
	const std::string invalidUtf8 = "a\x80z";

	{
		RecordedReplaceSelection replace{multiByte};
		callback(replace);
		// Mutate the stack value after delivery; host must not alias it.
		replace.text = "mutated";
	}
	{
		RecordedInsertText insert{0, invalidUtf8};
		callback(insert);
		insert.text.clear();
		insert.position = -1;
	}
	{
		RecordedSearch search{SearchDirection::Next, FindOption::None, invalidUtf8};
		callback(search);
		search.text = "other";
	}

	REQUIRE(editor.observations.recordedActions.size() == 3);
	CHECK(As<RecordedReplaceSelection>(editor.observations.recordedActions[0]).text == multiByte);
	CHECK(As<RecordedInsertText>(editor.observations.recordedActions[1]).position == 0);
	CHECK(As<RecordedInsertText>(editor.observations.recordedActions[1]).text == invalidUtf8);
	CHECK(As<RecordedSearch>(editor.observations.recordedActions[2]).text == invalidUtf8);
}

TEST_CASE("ClearObservations drops retained recorded actions") {
	TestHost host;
	TestEditor editor(host);

	editor.OnRecordedAction(RecordedReplaceSelection{"kept only until clear"});
	REQUIRE(editor.observations.recordedActions.size() == 1);

	editor.ClearObservations();
	CHECK(editor.observations.recordedActions.empty());
}

TEST_CASE("Without a recording sink the observation list stays empty") {
	TestHost host;
	TestEditor editor(host);
	// Drop the constructor-installed sink so emit has nowhere to go.
	editor.SetRecordingCallback({});
	editor.StartRecording();
	editor.ClearObservations();

	editor.RunCommand(EditorCommand::LineDown);

	CHECK(editor.observations.recordedActions.empty());
}

TEST_CASE("Recording lifecycle defaults, start, stop, and nested start") {
	TestHost host;
	TestEditor editor(host);

	CHECK_FALSE(editor.IsRecording());

	editor.StartRecording();
	CHECK(editor.IsRecording());

	// Nested start stays recording.
	editor.StartRecording();
	CHECK(editor.IsRecording());

	editor.StopRecording();
	CHECK_FALSE(editor.IsRecording());
}

TEST_CASE("Message start/stop forwarders match named lifecycle methods") {
	TestHost host;
	TestEditor editor(host);

	editor.WndProc(Message::StartRecord, 0, 0);
	CHECK(editor.IsRecording());

	editor.WndProc(Message::StopRecord, 0, 0);
	CHECK_FALSE(editor.IsRecording());
}

TEST_CASE("Recordable commands are captured as typed actions while recording") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("hello");
	editor.SetSel(0, 0);
	editor.StartRecording();
	editor.ClearObservations();

	editor.RunCommand(EditorCommand::CharRight);
	editor.RunCommand(EditorCommand::CharRightExtend);
	editor.RunCommand(EditorCommand::DeleteBack);
	editor.RunCommand(EditorCommand::SelectAll);
	editor.RunCommand(EditorCommand::Copy);
	editor.RunCommand(EditorCommand::CharRight);
	editor.RunCommand(EditorCommand::Paste);

	REQUIRE(editor.observations.recordedActions.size() == 7);
	CHECK(As<RecordedCommand>(editor.observations.recordedActions[0]).Command() ==
		EditorCommand::CharRight);
	CHECK(As<RecordedCommand>(editor.observations.recordedActions[1]).Command() ==
		EditorCommand::CharRightExtend);
	CHECK(As<RecordedCommand>(editor.observations.recordedActions[2]).Command() ==
		EditorCommand::DeleteBack);
	CHECK(As<RecordedCommand>(editor.observations.recordedActions[3]).Command() ==
		EditorCommand::SelectAll);
	CHECK(As<RecordedCommand>(editor.observations.recordedActions[4]).Command() ==
		EditorCommand::Copy);
	CHECK(As<RecordedCommand>(editor.observations.recordedActions[5]).Command() ==
		EditorCommand::CharRight);
	CHECK(As<RecordedCommand>(editor.observations.recordedActions[6]).Command() ==
		EditorCommand::Paste);
}

TEST_CASE("Non-recordable commands do not produce typed records") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("ab");
	editor.SetSel(2, 2);
	editor.StartRecording();
	editor.ClearObservations();

	editor.RunCommand(EditorCommand::Undo);
	editor.RunCommand(EditorCommand::Redo);
	editor.RunCommand(EditorCommand::NewLine);
	editor.RunCommand(EditorCommand::ZoomIn);
	editor.RunCommand(EditorCommand::SearchAnchor);
	editor.RunCommand(EditorCommand::SearchNext);
	editor.RunCommand(EditorCommand::SearchPrev);

	CHECK(editor.observations.recordedActions.empty());
}

TEST_CASE("Commands are not captured when recording is off") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("x");
	editor.ClearObservations();

	editor.RunCommand(EditorCommand::CharRight);

	CHECK(editor.observations.recordedActions.empty());
}

TEST_CASE("Message path and ExecuteCommand agree for recorded CharRight") {
	EditorCommand directCommand = EditorCommand::None;
	{
		TestHost host;
		TestEditor direct(host);
		direct.SetText("ab");
		direct.SetSel(0, 0);
		direct.StartRecording();
		direct.ClearObservations();
		direct.RunCommand(EditorCommand::CharRight);
		REQUIRE(direct.observations.recordedActions.size() == 1);
		directCommand = As<RecordedCommand>(direct.observations.recordedActions[0]).Command();
	}
	{
		TestHost host;
		TestEditor viaMessage(host);
		viaMessage.SetText("ab");
		viaMessage.SetSel(0, 0);
		viaMessage.StartRecording();
		viaMessage.ClearObservations();
		viaMessage.WndProc(Message::CharRight, 0, 0);
		REQUIRE(viaMessage.observations.recordedActions.size() == 1);
		CHECK(As<RecordedCommand>(viaMessage.observations.recordedActions[0]).Command() ==
			directCommand);
		// Message path must not also emit numeric macro for the command.
		CHECK(std::none_of(viaMessage.observations.notifications.begin(),
			viaMessage.observations.notifications.end(), [](const TestNotification &n) {
				return n.code == Notification::MacroRecord;
			}));
	}
}

TEST_CASE("Replay restores command sequence without recursive recording") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("abcd");
	editor.SetSel(0, 0);
	editor.StartRecording();
	editor.ClearObservations();

	editor.RunCommand(EditorCommand::CharRight);
	editor.RunCommand(EditorCommand::CharRight);
	editor.RunCommand(EditorCommand::CharRightExtend);
	editor.RunCommand(EditorCommand::DeleteBack);

	const std::vector<RecordedAction> recorded = editor.observations.recordedActions;
	REQUIRE(recorded.size() == 4);
	const std::string afterRecord = editor.Text();
	const Sci::Position afterPos = editor.CurrentPos();

	// Fresh document; leave recording on so recursive capture would show up.
	editor.SetText("abcd");
	editor.SetSel(0, 0);
	editor.ClearObservations();
	editor.ReplayRecordedActions(recorded);

	CHECK(editor.Text() == afterRecord);
	CHECK(editor.CurrentPos() == afterPos);
	CHECK(editor.observations.recordedActions.empty());
	CHECK(editor.IsRecording());
}

TEST_CASE("Replay suppresses temporary numeric character capture") {
	TestHost host;
	TestEditor editor(host);
	editor.StartRecording();
	editor.ClearObservations();

	editor.ReplayRecordedAction(RecordedCommand{EditorCommand::FormFeed});

	CHECK(editor.Text() == "\f");
	CHECK(editor.observations.recordedActions.empty());
	CHECK(std::none_of(editor.observations.notifications.begin(),
		editor.observations.notifications.end(), [](const TestNotification &notification) {
			return notification.code == Notification::MacroRecord;
		}));
}

TEST_CASE("Nested replay keeps outer capture suppression active") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("ab");
	editor.SetSel(0, 0);
	editor.StartRecording();
	editor.ClearObservations();
	editor.observations.replayOnModified =
		RecordedCommand{EditorCommand::CharRight};

	const std::vector<RecordedAction> actions = {
		RecordedInsertText{0, "x"},
		RecordedCommand{EditorCommand::CharRight},
	};
	editor.ReplayRecordedActions(actions);

	CHECK(editor.Text() == "xab");
	CHECK(editor.observations.recordedActions.empty());
}

TEST_CASE("Replay of a single RecordedCommand moves the caret") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("xy");
	editor.SetSel(0, 0);

	editor.ReplayRecordedAction(RecordedCommand{EditorCommand::CharRight});

	CHECK(editor.CurrentPos() == 1);
	CHECK(editor.observations.recordedActions.empty());
}

TEST_CASE("Stop recording ends further command capture") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("z");
	editor.StartRecording();
	editor.RunCommand(EditorCommand::CharRight);
	editor.StopRecording();
	editor.ClearObservations();

	editor.RunCommand(EditorCommand::CharLeft);

	CHECK(editor.observations.recordedActions.empty());
}

namespace {

bool HasMacroRecord(const TestEditor &editor) {
	return std::any_of(editor.observations.notifications.begin(),
		editor.observations.notifications.end(),
		[](const TestNotification &n) { return n.code == Notification::MacroRecord; });
}

}

TEST_CASE("Document text mutations are captured as typed actions while recording") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("ab");
	editor.SetSel(1, 1);
	editor.StartRecording();
	editor.ClearObservations();

	editor.AddText("X");
	editor.InsertText(0, "Y");
	editor.AppendText("Z");
	editor.ReplaceSel("Q");
	editor.ClearAll();

	REQUIRE(editor.observations.recordedActions.size() == 5);
	CHECK(As<RecordedAddText>(editor.observations.recordedActions[0]).text == "X");
	CHECK(As<RecordedInsertText>(editor.observations.recordedActions[1]).position == 0);
	CHECK(As<RecordedInsertText>(editor.observations.recordedActions[1]).text == "Y");
	CHECK(As<RecordedAppendText>(editor.observations.recordedActions[2]).text == "Z");
	CHECK(As<RecordedReplaceSelection>(editor.observations.recordedActions[3]).text == "Q");
	CHECK(std::holds_alternative<RecordedClearAll>(editor.observations.recordedActions[4]));
	CHECK_FALSE(HasMacroRecord(editor));
}

TEST_CASE("Empty document text mutations still record the request") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("keep");
	editor.StartRecording();
	editor.ClearObservations();

	editor.AddText("");
	editor.InsertText(1, "");
	editor.AppendText("");

	REQUIRE(editor.observations.recordedActions.size() == 3);
	CHECK(As<RecordedAddText>(editor.observations.recordedActions[0]).text.empty());
	CHECK(As<RecordedInsertText>(editor.observations.recordedActions[1]).position == 1);
	CHECK(As<RecordedInsertText>(editor.observations.recordedActions[1]).text.empty());
	CHECK(As<RecordedAppendText>(editor.observations.recordedActions[2]).text.empty());
	CHECK(editor.Text() == "keep");
}

TEST_CASE("Message path and named methods agree for document text recording") {
	std::vector<RecordedAction> viaNamed;
	{
		TestHost host;
		TestEditor editor(host);
		editor.SetText("base");
		editor.SetSel(0, 0);
		editor.StartRecording();
		editor.ClearObservations();
		editor.AddText("A");
		editor.InsertText(2, "B");
		editor.AppendText("C");
		editor.ReplaceSel("D");
		viaNamed = editor.observations.recordedActions;
		REQUIRE(viaNamed.size() == 4);
	}
	{
		TestHost host;
		TestEditor editor(host);
		editor.SetText("base");
		editor.SetSel(0, 0);
		editor.StartRecording();
		editor.ClearObservations();
		const char add[] = "A";
		editor.WndProc(Message::AddText, 1, reinterpret_cast<sptr_t>(add));
		const char ins[] = "B";
		editor.WndProc(Message::InsertText, 2, reinterpret_cast<sptr_t>(ins));
		const char app[] = "C";
		editor.WndProc(Message::AppendText, 1, reinterpret_cast<sptr_t>(app));
		const char rep[] = "D";
		editor.WndProc(Message::ReplaceSel, 0, reinterpret_cast<sptr_t>(rep));
		REQUIRE(editor.observations.recordedActions.size() == 4);
		CHECK(As<RecordedAddText>(editor.observations.recordedActions[0]).text ==
			As<RecordedAddText>(viaNamed[0]).text);
		CHECK(As<RecordedInsertText>(editor.observations.recordedActions[1]).position ==
			As<RecordedInsertText>(viaNamed[1]).position);
		CHECK(As<RecordedInsertText>(editor.observations.recordedActions[1]).text ==
			As<RecordedInsertText>(viaNamed[1]).text);
		CHECK(As<RecordedAppendText>(editor.observations.recordedActions[2]).text ==
			As<RecordedAppendText>(viaNamed[2]).text);
		CHECK(As<RecordedReplaceSelection>(editor.observations.recordedActions[3]).text ==
			As<RecordedReplaceSelection>(viaNamed[3]).text);
		CHECK_FALSE(HasMacroRecord(editor));
	}
}

TEST_CASE("Document text recording owns multi-byte and invalid UTF-8 after source is gone") {
	TestHost host;
	TestEditor editor(host);
	editor.StartRecording();
	editor.ClearObservations();

	{
		std::string multiByte = "\xC3\xA9";
		std::string invalidUtf8 = "a\x80z";
		editor.AddText(multiByte);
		editor.AppendText(invalidUtf8);
		multiByte[0] = 'X';
		invalidUtf8[0] = 'Y';
	}

	REQUIRE(editor.observations.recordedActions.size() == 2);
	CHECK(As<RecordedAddText>(editor.observations.recordedActions[0]).text == "\xC3\xA9");
	CHECK(As<RecordedAppendText>(editor.observations.recordedActions[1]).text == "a\x80z");
}
