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

TEST_CASE("Non-recordable commands do not produce RecordedCommand values") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("ab");
	editor.SetSel(2, 2);
	editor.StartRecording();
	editor.ClearObservations();

	// Undo, redo, zoom, and parameterless SearchNext/Prev emit nothing.
	editor.RunCommand(EditorCommand::Undo);
	editor.RunCommand(EditorCommand::Redo);
	editor.RunCommand(EditorCommand::ZoomIn);
	editor.RunCommand(EditorCommand::SearchNext);
	editor.RunCommand(EditorCommand::SearchPrev);
	CHECK(editor.observations.recordedActions.empty());

	// SearchAnchor is not a RecordedCommand; it has a dedicated alternative.
	editor.ClearObservations();
	editor.RunCommand(EditorCommand::SearchAnchor);
	REQUIRE(editor.observations.recordedActions.size() == 1);
	CHECK(std::holds_alternative<RecordedSearchAnchor>(editor.observations.recordedActions[0]));

	// NewLine is not a RecordedCommand; EOL bytes are stored as ReplaceSelection.
	editor.ClearObservations();
	editor.RunCommand(EditorCommand::NewLine);
	REQUIRE_FALSE(editor.observations.recordedActions.empty());
	for (const RecordedAction &action : editor.observations.recordedActions) {
		CHECK(std::holds_alternative<RecordedReplaceSelection>(action));
		CHECK_FALSE(std::holds_alternative<RecordedCommand>(action));
	}
}

TEST_CASE("Commands are not captured when recording is off") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("x");
	editor.ClearObservations();

	editor.RunCommand(EditorCommand::CharRight);

	CHECK(editor.observations.recordedActions.empty());
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

TEST_CASE("Replay suppresses character capture during FormFeed command") {
	TestHost host;
	TestEditor editor(host);
	editor.StartRecording();
	editor.ClearObservations();

	editor.ReplayRecordedAction(RecordedCommand{EditorCommand::FormFeed});

	CHECK(editor.Text() == "\f");
	CHECK(editor.observations.recordedActions.empty());
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

TEST_CASE("Goto and selection mode are captured as typed actions while recording") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("a\nb\nc\n");
	editor.StartRecording();
	editor.ClearObservations();

	editor.GotoLine(2);
	editor.GotoPos(1);
	editor.SetSelectionMode(SelectionMode::Rectangle, true);

	REQUIRE(editor.observations.recordedActions.size() == 3);
	CHECK(As<RecordedGotoLine>(editor.observations.recordedActions[0]).line == 2);
	CHECK(As<RecordedGotoPos>(editor.observations.recordedActions[1]).position == 1);
	CHECK(As<RecordedSetSelectionMode>(editor.observations.recordedActions[2]).mode ==
		SelectionMode::Rectangle);
}

TEST_CASE("Search anchor and parameterized search are captured while recording") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("one two one");
	editor.SetSel(0, 0);
	editor.StartRecording();
	editor.ClearObservations();

	editor.SearchAnchor();
	const char needle[] = "one";
	const Sci::Position found = editor.SearchText(EditorCommand::SearchNext, FindOption::None, needle);
	REQUIRE(found == 0);
	const Sci::Position prev = editor.SearchText(EditorCommand::SearchPrev, FindOption::MatchCase, needle);
	(void)prev;

	REQUIRE(editor.observations.recordedActions.size() == 3);
	CHECK(std::holds_alternative<RecordedSearchAnchor>(editor.observations.recordedActions[0]));
	const RecordedSearch &next = As<RecordedSearch>(editor.observations.recordedActions[1]);
	CHECK(next.direction == SearchDirection::Next);
	CHECK(next.flags == FindOption::None);
	CHECK(next.text == "one");
	const RecordedSearch &searchPrev = As<RecordedSearch>(editor.observations.recordedActions[2]);
	CHECK(searchPrev.direction == SearchDirection::Prev);
	CHECK(searchPrev.flags == FindOption::MatchCase);
	CHECK(searchPrev.text == "one");
}

TEST_CASE("Search recording owns the needle after the original buffer is overwritten") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("needle here");
	editor.SetSel(0, 0);
	editor.StartRecording();
	editor.ClearObservations();

	char needle[] = "needle";
	editor.SearchAnchor();
	editor.SearchText(EditorCommand::SearchNext, FindOption::None, needle);
	needle[0] = 'X';

	REQUIRE(editor.observations.recordedActions.size() == 2);
	CHECK(As<RecordedSearch>(editor.observations.recordedActions[1]).text == "needle");
}

TEST_CASE("Search and recording preserve embedded NUL bytes") {
	TestHost host;
	TestEditor editor(host);
	const std::string document("a\0bc", 4);
	const std::string needle("\0b", 2);
	editor.SetText(document);
	editor.SetSel(0, 0);
	editor.StartRecording();
	editor.ClearObservations();

	editor.SearchAnchor();
	const Sci::Position found = editor.SearchText(
		EditorCommand::SearchNext, FindOption::MatchCase, needle);

	CHECK(found == 1);
	REQUIRE(editor.observations.recordedActions.size() == 2);
	CHECK(As<RecordedSearch>(editor.observations.recordedActions[1]).text == needle);
	const std::vector<RecordedAction> recorded = editor.observations.recordedActions;

	editor.SetSel(0, 0);
	editor.ClearObservations();
	editor.ReplayRecordedActions(recorded);

	CHECK(editor.CurrentPos() == 1);
	CHECK(editor.observations.recordedActions.empty());
}

TEST_CASE("Failed search still records the request") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("abc");
	editor.SetSel(0, 0);
	editor.StartRecording();
	editor.ClearObservations();

	const char missing[] = "zzz";
	editor.SearchAnchor();
	const Sci::Position pos = editor.SearchText(EditorCommand::SearchNext, FindOption::None, missing);
	CHECK(pos == Sci::invalidPosition);
	REQUIRE(editor.observations.recordedActions.size() == 2);
	CHECK(As<RecordedSearch>(editor.observations.recordedActions[1]).text == "zzz");
}

TEST_CASE("Character insert is captured as RecordedReplaceSelection") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("");
	editor.StartRecording();
	editor.ClearObservations();

	editor.InsertInput("Hi");
	editor.InsertInput("\xC3\xA9");
	{
		std::string invalid = "\x80";
		editor.InsertInput(invalid);
		invalid[0] = 'Z';
	}

	REQUIRE(editor.observations.recordedActions.size() == 3);
	CHECK(As<RecordedReplaceSelection>(editor.observations.recordedActions[0]).text == "Hi");
	CHECK(As<RecordedReplaceSelection>(editor.observations.recordedActions[1]).text == "\xC3\xA9");
	CHECK(As<RecordedReplaceSelection>(editor.observations.recordedActions[2]).text == "\x80");
	CHECK(editor.Text() == "Hi\xC3\xA9\x80");
}

TEST_CASE("Tentative IME input is not recorded") {
	TestHost host;
	TestEditor editor(host);
	editor.StartRecording();
	editor.ClearObservations();

	editor.InsertCharacter("ab", CharacterSource::TentativeInput);
	CHECK(editor.observations.recordedActions.empty());
	CHECK(editor.Text() == "ab");

	editor.InsertCharacter("c", CharacterSource::DirectInput);
	REQUIRE(editor.observations.recordedActions.size() == 1);
	CHECK(As<RecordedReplaceSelection>(editor.observations.recordedActions[0]).text == "c");
}

TEST_CASE("Newline records EOL bytes as replace-selection, not a command") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("x");
	editor.SetSel(1, 1);
	editor.StartRecording();
	editor.ClearObservations();

	editor.RunCommand(EditorCommand::NewLine);

	// NewLine is not a RecordedCommand; each EOL character is one ReplaceSelection.
	REQUIRE_FALSE(editor.observations.recordedActions.empty());
	for (const RecordedAction &action : editor.observations.recordedActions) {
		CHECK(std::holds_alternative<RecordedReplaceSelection>(action));
	}
	std::string recordedEol;
	for (const RecordedAction &action : editor.observations.recordedActions) {
		recordedEol += As<RecordedReplaceSelection>(action).text;
	}
	CHECK(editor.Text() == "x" + recordedEol);
}

TEST_CASE("Replay character and newline inserts on a fresh editor") {
	TestHost host;
	TestEditor editor(host);
	editor.StartRecording();
	editor.ClearObservations();

	editor.InsertInput("ab");
	editor.RunCommand(EditorCommand::NewLine);
	editor.InsertInput("c");
	const std::vector<RecordedAction> recorded = editor.observations.recordedActions;
	const std::string after = editor.Text();
	REQUIRE_FALSE(recorded.empty());

	editor.SetText("");
	editor.ClearObservations();
	editor.ReplayRecordedActions(recorded);

	CHECK(editor.Text() == after);
	CHECK(editor.observations.recordedActions.empty());
	CHECK(editor.IsRecording());
}

TEST_CASE("Mixed parameterized capture replays on a fresh editor") {
	std::vector<RecordedAction> recorded;
	std::string afterText;
	Sci::Position afterPos = 0;
	SelectionMode afterMode = SelectionMode::Stream;

	{
		TestHost host;
		TestEditor editor(host);
		editor.StartRecording();
		editor.ClearObservations();

		editor.ClearAll();
		editor.AddText("one two");
		editor.InsertText(0, "X");
		editor.AppendText("!");
		editor.GotoPos(2);
		editor.ReplaceSel("Y");
		editor.GotoLine(0);
		editor.RunCommand(EditorCommand::CharRight);
		editor.SearchAnchor();
		const char needle[] = "two";
		editor.SearchText(EditorCommand::SearchNext, FindOption::None, needle);
		editor.SetSelectionMode(SelectionMode::Stream, true);
		editor.InsertInput("z");

		recorded = editor.observations.recordedActions;
		afterText = editor.Text();
		afterPos = editor.CurrentPos();
		afterMode = static_cast<SelectionMode>(editor.GetSelectionMode());
	}

	// Every production alternative should appear at least once in this script.
	REQUIRE(std::any_of(recorded.begin(), recorded.end(),
		[](const RecordedAction &a) { return std::holds_alternative<RecordedClearAll>(a); }));
	REQUIRE(std::any_of(recorded.begin(), recorded.end(),
		[](const RecordedAction &a) { return std::holds_alternative<RecordedAddText>(a); }));
	REQUIRE(std::any_of(recorded.begin(), recorded.end(),
		[](const RecordedAction &a) { return std::holds_alternative<RecordedInsertText>(a); }));
	REQUIRE(std::any_of(recorded.begin(), recorded.end(),
		[](const RecordedAction &a) { return std::holds_alternative<RecordedAppendText>(a); }));
	REQUIRE(std::any_of(recorded.begin(), recorded.end(),
		[](const RecordedAction &a) { return std::holds_alternative<RecordedGotoPos>(a); }));
	REQUIRE(std::any_of(recorded.begin(), recorded.end(),
		[](const RecordedAction &a) { return std::holds_alternative<RecordedReplaceSelection>(a); }));
	REQUIRE(std::any_of(recorded.begin(), recorded.end(),
		[](const RecordedAction &a) { return std::holds_alternative<RecordedGotoLine>(a); }));
	REQUIRE(std::any_of(recorded.begin(), recorded.end(),
		[](const RecordedAction &a) { return std::holds_alternative<RecordedCommand>(a); }));
	REQUIRE(std::any_of(recorded.begin(), recorded.end(),
		[](const RecordedAction &a) { return std::holds_alternative<RecordedSearchAnchor>(a); }));
	REQUIRE(std::any_of(recorded.begin(), recorded.end(),
		[](const RecordedAction &a) { return std::holds_alternative<RecordedSearch>(a); }));
	REQUIRE(std::any_of(recorded.begin(), recorded.end(),
		[](const RecordedAction &a) { return std::holds_alternative<RecordedSetSelectionMode>(a); }));

	{
		TestHost host;
		TestEditor fresh(host);
		fresh.StartRecording();
		fresh.ClearObservations();
		fresh.ReplayRecordedActions(recorded);

		CHECK(fresh.Text() == afterText);
		CHECK(fresh.CurrentPos() == afterPos);
		CHECK(static_cast<SelectionMode>(fresh.GetSelectionMode()) == afterMode);
		CHECK(fresh.observations.recordedActions.empty());
		CHECK(fresh.IsRecording());
		}
}

TEST_CASE("Constructed script of every RecordedAction alternative replays cleanly") {
	const std::vector<RecordedAction> script = {
		RecordedClearAll{},
		RecordedAddText{"alpha beta"},
		RecordedInsertText{0, "Z"},
		RecordedAppendText{"!"},
		RecordedGotoPos{1},
		RecordedReplaceSelection{"Q"},
		RecordedGotoLine{0},
		RecordedCommand{EditorCommand::CharRight},
		RecordedSearchAnchor{},
		RecordedSearch{SearchDirection::Next, FindOption::None, "beta"},
		RecordedSetSelectionMode{SelectionMode::Stream},
		RecordedReplaceSelection{"x"},
	};

	TestHost host;
	TestEditor editor(host);
	editor.StartRecording();
	editor.ClearObservations();
	editor.ReplayRecordedActions(script);

	// Replay must not re-record even while recording is on.
	CHECK(editor.observations.recordedActions.empty());
	CHECK(editor.Text().find('!') != std::string::npos);
	// Search selected "beta"; final replace-selection overwrites that match with "x".
	CHECK(editor.Text().find("beta") == std::string::npos);
	CHECK(editor.Text().find('x') != std::string::npos);
	CHECK(editor.Text().find("ZQalpha") != std::string::npos);
}

TEST_CASE("Moving selected lines records one command and replays the selection") {
	constexpr std::string_view initialText = "one\ntwo\nthree\n";
	std::vector<RecordedAction> recorded;
	std::string afterText;
	Sci::Position afterCaret = 0;
	Sci::Position afterAnchor = 0;

	{
		TestHost host;
		TestEditor editor(host);
		editor.SetText(initialText);
		editor.SetSel(4, 7);
		editor.StartRecording();
		editor.ClearObservations();

		editor.RunCommand(EditorCommand::MoveSelectedLinesDown);

		REQUIRE(editor.observations.recordedActions.size() == 1);
		CHECK(As<RecordedCommand>(editor.observations.recordedActions[0]).Command() ==
			EditorCommand::MoveSelectedLinesDown);
		recorded = editor.observations.recordedActions;
		afterText = editor.Text();
		afterCaret = editor.GetCurrentPos();
		afterAnchor = editor.GetAnchor();
	}

	TestHost host;
	TestEditor replay(host);
	replay.SetText(initialText);
	replay.SetSel(4, 7);
	replay.ReplayRecordedActions(recorded);

	CHECK(replay.Text() == afterText);
	CHECK(replay.GetCurrentPos() == afterCaret);
	CHECK(replay.GetAnchor() == afterAnchor);
}

TEST_CASE("Internal fold caret movement is not recorded as GotoLine") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("outer\ninner\nchild\nlast\n");
	const int outer = static_cast<int>(FoldLevel::Base) |
		static_cast<int>(FoldLevel::HeaderFlag);
	const int inner = static_cast<int>(FoldLevel::Base) + 1 |
		static_cast<int>(FoldLevel::HeaderFlag);
	const int child = static_cast<int>(FoldLevel::Base) + 2;
	editor.SetFoldLevel(0, static_cast<FoldLevel>(outer));
	editor.SetFoldLevel(1, static_cast<FoldLevel>(inner));
	editor.SetFoldLevel(2, static_cast<FoldLevel>(child));
	editor.SetFoldLevel(3, static_cast<FoldLevel>(static_cast<int>(FoldLevel::Base)));
	editor.FoldLine(0, FoldAction::Contract);
	REQUIRE(editor.GetLineVisible(1) == 0);

	editor.StartRecording();
	editor.ClearObservations();
	editor.FoldLine(1, FoldAction::Expand);

	CHECK(editor.GetLineVisible(1) != 0);
	CHECK(editor.observations.recordedActions.empty());
}
