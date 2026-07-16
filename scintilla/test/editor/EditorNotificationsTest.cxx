// scalpel-editor test code
/** @file EditorNotificationsTest.cxx
 ** Focused coverage for every retained host notification kind.
 **/

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

const TestNotification *FindNotification(const TestEditor &editor, Notification code) {
	for (const TestNotification &notification : editor.observations.notifications) {
		if (notification.code == code)
			return &notification;
	}
	return nullptr;
}

const TestNotification *FindModified(const TestEditor &editor, ModificationFlags flags) {
	const auto it = std::find_if(editor.observations.notifications.begin(), editor.observations.notifications.end(),
		[flags](const TestNotification &notification) {
			return notification.code == Notification::Modified &&
				FlagSet(notification.modificationType, flags);
		});
	return it == editor.observations.notifications.end() ? nullptr : &*it;
}

bool HasNotification(const TestEditor &editor, Notification code) {
	return FindNotification(editor, code) != nullptr;
}

// Default fixedColumnWidth is about left gap + symbol margin (16). Place clicks
// past that so PositionFromLocation lands on text, and inside it for margins.
constexpr XYPOSITION textX = 40;
constexpr XYPOSITION textY = 5;
constexpr XYPOSITION marginX = 8;

void PaintClean(TestEditor &editor, std::string_view text) {
	LoadClean(editor, text);
	editor.PaintAll();
	editor.ClearObservations();
}

void InstallSimpleFold(TestEditor &editor) {
	editor.SetText("header\nchild1\nchild2\n");
	const int header = static_cast<int>(FoldLevel::Base) | static_cast<int>(FoldLevel::HeaderFlag);
	const int child = static_cast<int>(FoldLevel::Base) + 1;
	editor.SetFoldLevel(0, static_cast<FoldLevel>(header));
	editor.SetFoldLevel(1, static_cast<FoldLevel>(child));
	editor.SetFoldLevel(2, static_cast<FoldLevel>(child));
	editor.SetAutomaticFold(AutomaticFold::None);
	editor.PaintAll();
}

}

TEST_CASE("StyleNeeded notifies under container lexing") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("abcd");
	// No ILexer: Colourise asks the host to style the range.
	editor.ClearObservations();
	editor.Colourise(0, -1);
	const TestNotification *n = FindNotification(editor, Notification::StyleNeeded);
	REQUIRE(n != nullptr);
	CHECK(n->position == 4);
}

TEST_CASE("CharAdded reports code point and source") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("");
	editor.ClearObservations();
	editor.InsertInput("Z");
	const TestNotification *n = FindNotification(editor, Notification::CharAdded);
	REQUIRE(n != nullptr);
	CHECK(n->ch == 'Z');
	CHECK(n->characterSource == CharacterSource::DirectInput);
}

TEST_CASE("SavePoint and ModifyAttemptRO notifications") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "x");
	editor.ClearObservations();
	editor.InsertInput("y");
	CHECK(HasNotification(editor, Notification::SavePointLeft));
	CHECK(HasNotification(editor, Notification::Modified));

	// Undo restores the load save point.
	editor.ClearObservations();
	editor.RunCommand(EditorCommand::Undo);
	CHECK(HasNotification(editor, Notification::SavePointReached));
	CHECK(editor.Text() == "x");

	editor.SetReadOnly(true);
	editor.ClearObservations();
	editor.InsertInput("z");
	CHECK(HasNotification(editor, Notification::ModifyAttemptRO));
}

TEST_CASE("UpdateUI and Painted notifications") {
	TestHost host;
	TestEditor editor(host);
	PaintClean(editor, "hello");
	editor.InsertInput("!");
	editor.ClearObservations();
	editor.FlushUpdateNotifications();
	const TestNotification *update = FindNotification(editor, Notification::UpdateUI);
	REQUIRE(update != nullptr);
	CHECK(update->updated != Update::None);

	editor.ClearObservations();
	editor.PaintAll();
	CHECK(HasNotification(editor, Notification::Painted));
}

TEST_CASE("FocusIn and FocusOut notifications") {
	TestHost host;
	TestEditor editor(host);
	editor.ClearObservations();
	editor.SetFocus(true);
	CHECK(HasNotification(editor, Notification::FocusIn));
	editor.ClearObservations();
	editor.SetFocus(false);
	CHECK(HasNotification(editor, Notification::FocusOut));
}

TEST_CASE("Zoom notification has no payload fields") {
	TestHost host;
	TestEditor editor(host);
	editor.PaintAll();
	editor.ClearObservations();
	editor.SetZoom(4);
	CHECK(HasNotification(editor, Notification::Zoom));
}

TEST_CASE("DoubleClick reports position line and modifiers") {
	TestHost host;
	TestEditor editor(host);
	PaintClean(editor, "hello");
	const Point pt(textX, textY);
	editor.MouseDown(pt, KeyMod::Shift);
	editor.MouseUp(pt, KeyMod::Shift);
	editor.AdvanceTime(50);
	editor.ClearObservations();
	editor.MouseDown(pt, KeyMod::Shift);
	const TestNotification *n = FindNotification(editor, Notification::DoubleClick);
	REQUIRE(n != nullptr);
	CHECK(n->position >= 0);
	CHECK(n->line == 0);
	CHECK(FlagSet(n->modifiers, KeyMod::Shift));
}

TEST_CASE("HotSpot click and release") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("hot");
	editor.StyleSetHotSpot(0, true);
	editor.PaintAll();
	const Point pt(textX, textY);

	editor.ClearObservations();
	editor.MouseDown(pt, KeyMod::Ctrl);
	const TestNotification *click = FindNotification(editor, Notification::HotSpotClick);
	REQUIRE(click != nullptr);
	CHECK(click->position >= 0);
	CHECK(FlagSet(click->modifiers, KeyMod::Ctrl));

	editor.MouseUp(pt, KeyMod::Ctrl);
	const TestNotification *release = FindNotification(editor, Notification::HotSpotReleaseClick);
	REQUIRE(release != nullptr);
	CHECK(release->position >= 0);
}

TEST_CASE("HotSpotDoubleClick on multi-click in hotspot text") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("hot");
	editor.StyleSetHotSpot(0, true);
	editor.PaintAll();
	const Point pt(textX, textY);

	editor.MouseDown(pt, KeyMod::Norm);
	editor.MouseUp(pt, KeyMod::Norm);
	editor.AdvanceTime(50);
	editor.ClearObservations();
	editor.MouseDown(pt, KeyMod::Norm);
	CHECK(HasNotification(editor, Notification::DoubleClick));
	CHECK(HasNotification(editor, Notification::HotSpotDoubleClick));
}

TEST_CASE("Indicator click and release") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("abc");
	editor.SetIndicatorCurrent(0);
	editor.IndicatorFillRange(0, 3);
	editor.PaintAll();
	const Point pt(textX, textY);

	editor.ClearObservations();
	editor.MouseDown(pt, KeyMod::Alt);
	const TestNotification *click = FindNotification(editor, Notification::IndicatorClick);
	REQUIRE(click != nullptr);
	CHECK(click->position >= 0);
	CHECK(FlagSet(click->modifiers, KeyMod::Alt));

	editor.MouseUp(pt, KeyMod::Alt);
	const TestNotification *release = FindNotification(editor, Notification::IndicatorRelease);
	REQUIRE(release != nullptr);
	CHECK(release->position >= 0);
}

TEST_CASE("MarginClick and MarginRightClick report margin index") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("line\n");
	// Margin 0 is number type with zero default width; give it space and sensitivity.
	editor.SetMarginWidthN(0, 24);
	editor.SetMarginSensitiveN(0, true);
	// Keep folder automatic click off so the host receives the click.
	editor.SetAutomaticFold(AutomaticFold::None);
	editor.PaintAll();
	const Point pt(marginX, textY);

	editor.ClearObservations();
	editor.MouseDown(pt, KeyMod::Norm);
	const TestNotification *left = FindNotification(editor, Notification::MarginClick);
	REQUIRE(left != nullptr);
	CHECK(left->margin == 0);
	CHECK(left->position == 0);

	editor.ClearObservations();
	editor.MouseRightDown(pt, KeyMod::Shift);
	const TestNotification *right = FindNotification(editor, Notification::MarginRightClick);
	REQUIRE(right != nullptr);
	CHECK(right->margin == 0);
	CHECK(FlagSet(right->modifiers, KeyMod::Shift));
}

TEST_CASE("NeedShown when editing into a fold-hidden range") {
	TestHost host;
	TestEditor editor(host);
	InstallSimpleFold(editor);
	editor.ToggleFold(0);
	REQUIRE(editor.GetLineVisible(1) == 0);

	editor.ClearObservations();
	// Insert at start of header so BeforeInsert sees hidden descendants.
	editor.GotoPos(0);
	editor.InsertInput("X");
	const TestNotification *n = FindNotification(editor, Notification::NeedShown);
	REQUIRE(n != nullptr);
	CHECK(n->position >= 0);
	CHECK(n->length >= 0);
}

TEST_CASE("DwellStart and DwellEnd from fine ticker") {
	TestHost host;
	TestEditor editor(host);
	PaintClean(editor, "dwell");
	editor.SetMouseDwellTime(100);
	editor.MouseMove(Point(textX, textY), KeyMod::Norm);

	editor.ClearObservations();
	editor.FireDwellTick();
	const TestNotification *start = FindNotification(editor, Notification::DwellStart);
	REQUIRE(start != nullptr);
	CHECK(start->x > 0);
	CHECK(start->y >= 0);

	editor.ClearObservations();
	editor.MouseMove(Point(textX + 20, textY), KeyMod::Norm);
	const TestNotification *end = FindNotification(editor, Notification::DwellEnd);
	REQUIRE(end != nullptr);
}

TEST_CASE("CallTipClick reports click place") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("fn(");
	editor.CallTipShow(3, "fn(int x)");
	REQUIRE(editor.CallTipActive());
	editor.ClearObservations();
	// Host click routing for the tip window is not wired yet; call the emitter.
	editor.CallTipClick();
	const TestNotification *n = FindNotification(editor, Notification::CallTipClick);
	REQUIRE(n != nullptr);
	CHECK(n->position == 0);
}

TEST_CASE("Autocomplete notification kinds") {
	TestHost host;
	TestEditor editor(host);

	// AutoCSelectionChange via list selection.
	editor.SetText("");
	editor.AutoCShow(0, "alpha beta gamma");
	REQUIRE(editor.AutoCActive());
	editor.ClearObservations();
	host.NotifyListBoxSelectionChange();
	const TestNotification *change = FindNotification(editor, Notification::AutoCSelectionChange);
	REQUIRE(change != nullptr);
	CHECK_FALSE(change->text.empty());

	// AutoCCharDeleted on backspace while the list is open.
	editor.SetText("al");
	editor.GotoPos(2);
	editor.AutoCShow(2, "alpha beta");
	REQUIRE(editor.AutoCActive());
	editor.ClearObservations();
	editor.RunCommand(EditorCommand::DeleteBack);
	CHECK(HasNotification(editor, Notification::AutoCCharDeleted));

	// AutoCCancelled via stop character.
	editor.SetText("");
	editor.AutoCStops(".");
	editor.AutoCShow(0, "alpha beta");
	editor.ClearObservations();
	editor.InsertInput(".");
	CHECK(HasNotification(editor, Notification::AutoCCancelled));

	// AutoCSelection + AutoCCompleted.
	editor.SetText("");
	editor.AutoCShow(0, "alpha beta");
	editor.AutoCSelect("beta");
	editor.ClearObservations();
	editor.AutoCComplete();
	const TestNotification *selection = FindNotification(editor, Notification::AutoCSelection);
	REQUIRE(selection != nullptr);
	CHECK(selection->text == "beta");
	const TestNotification *completed = FindNotification(editor, Notification::AutoCCompleted);
	REQUIRE(completed != nullptr);
	CHECK(completed->text == "beta");

	// UserListSelection does not insert.
	editor.SetText("keep");
	editor.UserListShow(4, "one two");
	editor.AutoCSelect("two");
	editor.ClearObservations();
	editor.AutoCComplete();
	const TestNotification *user = FindNotification(editor, Notification::UserListSelection);
	REQUIRE(user != nullptr);
	CHECK(user->text == "two");
	CHECK(user->listType == 4);
	CHECK(editor.Text() == "keep");
}

TEST_CASE("Modified carries text and line payload") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "ab");
	editor.ClearObservations();
	editor.InsertInput("X\n");
	const TestNotification *n = FindModified(editor, ModificationFlags::InsertText);
	REQUIRE(n != nullptr);
	CHECK(n->text == "X\n");
	CHECK(n->length == 2);
	CHECK(n->linesAdded == 1);
	CHECK(n->line == 0);
}

TEST_CASE("Modified carries fold levels") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "line\n");
	const FoldLevel previous = editor.GetFoldLevel(0);
	const FoldLevel current = static_cast<FoldLevel>(
		static_cast<int>(FoldLevel::Base) | static_cast<int>(FoldLevel::HeaderFlag));

	editor.ClearObservations();
	editor.SetFoldLevel(0, current);
	const TestNotification *n = FindModified(editor, ModificationFlags::ChangeFold);
	REQUIRE(n != nullptr);
	CHECK(n->line == 0);
	CHECK(n->foldLevelNow == current);
	CHECK(n->foldLevelPrev == previous);
}

TEST_CASE("Modified carries annotation line change") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "line\n");

	editor.ClearObservations();
	editor.AnnotationSetText(0, "first\nsecond");
	const TestNotification *n = FindModified(editor, ModificationFlags::ChangeAnnotation);
	REQUIRE(n != nullptr);
	CHECK(n->line == 0);
	CHECK(n->annotationLinesAdded == 2);
}

TEST_CASE("Modified returns container action token on undo") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "");
	editor.AddUndoAction(73, false);

	editor.ClearObservations();
	editor.RunCommand(EditorCommand::Undo);
	const TestNotification *n = FindModified(editor, ModificationFlags::Container);
	REQUIRE(n != nullptr);
	CHECK(FlagSet(n->modificationType, ModificationFlags::Undo));
	CHECK(n->token == 73);
}
