// scalpel-editor test code
/** @file EditorAutocompleteTest.cxx
 ** Focused behavior tests for autocomplete and user lists.
 **/

#include <array>
#include <algorithm>
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

bool HasNotification(const TestEditor &editor, Notification code) {
	for (const TestNotification &notification : editor.observations.notifications) {
		if (notification.code == code)
			return true;
	}
	return false;
}

const TestNotification *FindNotification(const TestEditor &editor, Notification code) {
	for (const TestNotification &notification : editor.observations.notifications) {
		if (notification.code == code)
			return &notification;
	}
	return nullptr;
}

}

TEST_CASE("Autocomplete defaults and option round trips") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("");

	CHECK_FALSE(editor.AutoCActive());
	CHECK(editor.AutoCGetSeparator() == ' ');
	CHECK(editor.AutoCGetTypeSeparator() == '?');
	CHECK(editor.AutoCGetCancelAtStart());
	CHECK_FALSE(editor.AutoCGetChooseSingle());
	CHECK_FALSE(editor.AutoCGetIgnoreCase());
	CHECK(editor.AutoCGetAutoHide());
	CHECK_FALSE(editor.AutoCGetDropRestOfWord());
	CHECK(editor.AutoCGetMulti() == MultiAutoComplete::Once);
	CHECK(editor.AutoCGetOrder() == Ordering::PreSorted);
	CHECK(editor.AutoCGetOptions() == AutoCompleteOption::Normal);
	CHECK(editor.AutoCGetMaxHeight() == 5);
	CHECK(editor.AutoCGetMaxWidth() == 0);
	CHECK(editor.AutoCGetStyle() == StyleDefault);
	CHECK(editor.AutoCGetImageScale() == 100);
	CHECK(editor.AutoCGetCaseInsensitiveBehaviour() == CaseInsensitiveBehaviour::RespectCase);

	editor.AutoCSetSeparator('|');
	CHECK(editor.AutoCGetSeparator() == '|');
	editor.AutoCSetTypeSeparator('#');
	CHECK(editor.AutoCGetTypeSeparator() == '#');
	editor.AutoCSetCancelAtStart(false);
	CHECK_FALSE(editor.AutoCGetCancelAtStart());
	editor.AutoCSetChooseSingle(true);
	CHECK(editor.AutoCGetChooseSingle());
	editor.AutoCSetIgnoreCase(true);
	CHECK(editor.AutoCGetIgnoreCase());
	editor.AutoCSetAutoHide(false);
	CHECK_FALSE(editor.AutoCGetAutoHide());
	editor.AutoCSetDropRestOfWord(true);
	CHECK(editor.AutoCGetDropRestOfWord());
	editor.AutoCSetMulti(MultiAutoComplete::Each);
	CHECK(editor.AutoCGetMulti() == MultiAutoComplete::Each);
	editor.AutoCSetOrder(Ordering::PerformSort);
	CHECK(editor.AutoCGetOrder() == Ordering::PerformSort);
	editor.AutoCSetOptions(AutoCompleteOption::SelectFirstItem);
	CHECK(editor.AutoCGetOptions() == AutoCompleteOption::SelectFirstItem);
	editor.AutoCSetMaxHeight(12);
	CHECK(editor.AutoCGetMaxHeight() == 12);
	editor.AutoCSetMaxWidth(40);
	CHECK(editor.AutoCGetMaxWidth() == 40);
	editor.AutoCSetStyle(3);
	CHECK(editor.AutoCGetStyle() == 3);
	editor.AutoCSetImageScale(200);
	CHECK(editor.AutoCGetImageScale() == 200);
	editor.AutoCSetCaseInsensitiveBehaviour(CaseInsensitiveBehaviour::IgnoreCase);
	CHECK(editor.AutoCGetCaseInsensitiveBehaviour() == CaseInsensitiveBehaviour::IgnoreCase);
	editor.AutoCStops(".)");
	editor.AutoCSetFillUps("([");
}

TEST_CASE("AutoCShow shows list and reports current item") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("");
	editor.AutoCShow(0, "alpha beta gamma");

	CHECK(editor.AutoCActive());
	CHECK(host.listBox.created);
	CHECK(host.listBox.visible);
	REQUIRE(host.listBox.items.size() == 3);
	CHECK(host.listBox.items[0].text == "alpha");
	CHECK(host.listBox.selection == 0);
	CHECK(editor.AutoCGetCurrent() == 0);
	CHECK(editor.AutoCPosStart() == 0);

	char current[32] = {};
	CHECK(editor.AutoCGetCurrentText(current) == 5);
	CHECK(std::string(current) == "alpha");
}

TEST_CASE("AutoCShow with lengthEntered selects matching prefix") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("be");
	editor.GotoPos(2);
	editor.AutoCShow(2, "alpha beta gamma");

	REQUIRE(editor.AutoCActive());
	CHECK(host.listBox.selection == 1);
	CHECK(editor.AutoCGetCurrent() == 1);
}

TEST_CASE("AutoCCancel hides without AutoCCancelled; CancelModes notifies") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("x");
	editor.AutoCShow(0, "one two");
	REQUIRE(editor.AutoCActive());
	editor.ClearObservations();

	editor.AutoCCancel();
	CHECK_FALSE(editor.AutoCActive());
	CHECK_FALSE(host.listBox.created);
	CHECK_FALSE(HasNotification(editor, Notification::AutoCCancelled));

	editor.AutoCShow(0, "one two");
	editor.ClearObservations();
	editor.MouseDown(Point(0, 0), KeyMod::Norm);
	CHECK_FALSE(editor.AutoCActive());
	CHECK(HasNotification(editor, Notification::AutoCCancelled));
}

TEST_CASE("AutoCComplete inserts selected text and notifies") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("");
	editor.AutoCShow(0, "alpha beta gamma");
	editor.AutoCSelect("beta");
	REQUIRE(editor.AutoCGetCurrent() == 1);
	editor.ClearObservations();

	editor.AutoCComplete();

	CHECK_FALSE(editor.AutoCActive());
	CHECK(editor.Text() == "beta");
	const TestNotification *selection = FindNotification(editor, Notification::AutoCSelection);
	REQUIRE(selection != nullptr);
	CHECK(selection->text == "beta");
	const TestNotification *completed = FindNotification(editor, Notification::AutoCCompleted);
	REQUIRE(completed != nullptr);
	CHECK(completed->text == "beta");
}

TEST_CASE("Choose single inserts without showing the list") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("");
	editor.AutoCSetChooseSingle(true);
	editor.ClearObservations();

	editor.AutoCShow(0, "only");

	CHECK_FALSE(editor.AutoCActive());
	CHECK_FALSE(host.listBox.created);
	CHECK(editor.Text() == "only");
	CHECK(HasNotification(editor, Notification::AutoCCompleted));
}

TEST_CASE("User list selection notifies without inserting text") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("keep");
	editor.UserListShow(3, "alpha beta");
	REQUIRE(editor.AutoCActive());
	editor.AutoCSelect("beta");
	editor.ClearObservations();

	editor.AutoCComplete();

	CHECK_FALSE(editor.AutoCActive());
	CHECK(editor.Text() == "keep");
	const TestNotification *user = FindNotification(editor, Notification::UserListSelection);
	REQUIRE(user != nullptr);
	CHECK(user->text == "beta");
	CHECK_FALSE(HasNotification(editor, Notification::AutoCSelection));
	CHECK_FALSE(HasNotification(editor, Notification::AutoCCompleted));
}

TEST_CASE("Type separator list parsing and drop rest of word") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("");
	editor.AutoCShow(0, "alpha?1 beta?2");
	REQUIRE(host.listBox.items.size() == 2);
	CHECK(host.listBox.items[0].text == "alpha");
	CHECK(host.listBox.items[0].type == 1);
	CHECK(host.listBox.items[1].text == "beta");
	CHECK(host.listBox.items[1].type == 2);
	editor.AutoCCancel();

	editor.SetText("preXX");
	editor.GotoPos(3);
	editor.AutoCSetDropRestOfWord(true);
	editor.AutoCShow(3, "prefix");
	// With choose-single off, show the one-item list then complete.
	REQUIRE(editor.AutoCActive());
	editor.AutoCComplete();
	CHECK(editor.Text() == "prefix");
}

TEST_CASE("Stop character cancels with notification") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("");
	editor.AutoCStops(".");
	editor.AutoCShow(0, "alpha beta");
	REQUIRE(editor.AutoCActive());
	editor.ClearObservations();

	editor.InsertInput(".");

	CHECK_FALSE(editor.AutoCActive());
	CHECK(HasNotification(editor, Notification::AutoCCancelled));
	// Stop characters cancel without inserting the character into completion;
	// InsertCharacter still inserts when not a fill-up.
	CHECK(editor.Text() == ".");
}
