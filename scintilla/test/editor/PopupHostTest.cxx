// scalpel-editor test code
/** @file PopupHostTest.cxx
 ** Smoke tests for the deterministic autocomplete list box and call-tip host.
 **
 ** These pin host observability for phase 4 step 6. Full coverage of every
 ** autocomplete and call-tip interface entry belongs with the step 7 pilot.
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

TEST_CASE("Autocomplete list box show is inspectable") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("");

	editor.AutoCShow(0, "alpha beta gamma");

	CHECK(editor.AutoCActive());
	CHECK(host.listBox.created);
	CHECK(host.listBox.visible);
	CHECK(host.listBox.items.size() == 3);
	CHECK(host.listBox.items[0].text == "alpha");
	CHECK(host.listBox.items[1].text == "beta");
	CHECK(host.listBox.items[2].text == "gamma");
	CHECK(host.listBox.selection == 0);
	CHECK(host.listBox.rect.Width() > 0);
	CHECK(host.listBox.rect.Height() > 0);
	CHECK(editor.AutoCGetCurrent() == 0);
}

TEST_CASE("Autocomplete cancel destroys the list box") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("");

	editor.AutoCShow(0, "one two");
	REQUIRE(editor.AutoCActive());

	// AutoCCancel only calls ac.Cancel(); it does not fire AutoCCancelled.
	// That notification comes from AutoCompleteCancel (CancelModes, stop chars).
	editor.AutoCCancel();

	CHECK_FALSE(editor.AutoCActive());
	CHECK_FALSE(host.listBox.created);
	CHECK_FALSE(host.listBox.visible);
}

TEST_CASE("Autocomplete cancel through CancelModes notifies AutoCCancelled") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("x");

	editor.AutoCShow(0, "one two");
	REQUIRE(editor.AutoCActive());
	editor.ClearObservations();

	// Mouse down runs CancelModes -> AutoCompleteCancel, which notifies.
	editor.MouseDown(Point(0, 0), KeyMod::Norm);

	CHECK_FALSE(editor.AutoCActive());
	CHECK(HasNotification(editor, Notification::AutoCCancelled));
	CHECK_FALSE(host.listBox.created);
}

TEST_CASE("Autocomplete complete via double-click inserts text and notifies") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("");

	editor.AutoCShow(0, "alpha beta gamma");
	REQUIRE(editor.AutoCActive());
	// Show selects the first item; move to the second before completing.
	editor.AutoCSelect("beta");
	REQUIRE(host.listBox.selection == 1);
	editor.ClearObservations();

	host.NotifyListBoxDoubleClick();

	CHECK_FALSE(editor.AutoCActive());
	CHECK(editor.Text() == "beta");
	const TestNotification *selection = FindNotification(editor, Notification::AutoCSelection);
	REQUIRE(selection != nullptr);
	CHECK(selection->text == "beta");
	const TestNotification *completed = FindNotification(editor, Notification::AutoCCompleted);
	REQUIRE(completed != nullptr);
	CHECK(completed->text == "beta");
	CHECK_FALSE(host.listBox.created);
}

TEST_CASE("Call tip show and cancel update host window state") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("fn(");
	editor.GotoPos(3);

	editor.CallTipShow(3, "fn(int x)");

	CHECK(editor.CallTipActive());
	CHECK(host.callTip.created);
	CHECK(host.callTip.visible);
	CHECK(host.callTip.rect.Width() > 0);
	CHECK(host.callTip.rect.Height() > 0);
	CHECK(host.callTip.createCount == 1);
	REQUIRE(editor.observations.callTipWindows.size() == 1);
	CHECK(editor.observations.callTipWindows[0].Width() == host.callTip.rect.Width());

	editor.CallTipCancel();

	CHECK_FALSE(editor.CallTipActive());
	CHECK_FALSE(host.callTip.created);
	CHECK_FALSE(host.callTip.visible);
}

TEST_CASE("Call tip and autocomplete cancel each other") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("fn");
	editor.GotoPos(2);

	editor.AutoCShow(0, "fn function");
	REQUIRE(editor.AutoCActive());
	REQUIRE(host.listBox.created);

	editor.CallTipShow(2, "fn()");

	// CallTipShow cancels autocomplete first.
	CHECK_FALSE(editor.AutoCActive());
	CHECK_FALSE(host.listBox.created);
	CHECK(editor.CallTipActive());
	CHECK(host.callTip.created);
	CHECK(host.callTip.visible);

	// AutoCompleteStart cancels an open call tip.
	editor.AutoCShow(0, "fn function");
	CHECK_FALSE(editor.CallTipActive());
	CHECK_FALSE(host.callTip.created);
	CHECK(editor.AutoCActive());
	CHECK(host.listBox.created);
}

TEST_CASE("List box SetList parses type separators") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("");

	// Default type separator is '?'.
	editor.AutoCShow(0, "alpha?1 beta?2");

	REQUIRE(host.listBox.items.size() == 2);
	CHECK(host.listBox.items[0].text == "alpha");
	CHECK(host.listBox.items[0].type == 1);
	CHECK(host.listBox.items[1].text == "beta");
	CHECK(host.listBox.items[1].type == 2);
}

namespace {

bool PopupItemEnabled(const TestEditor &editor, std::string_view label) {
	for (const std::string &item : editor.observations.popupItems) {
		if (item.rfind(label, 0) == 0)
			return item.find(" enabled") != std::string::npos;
	}
	return false;
}

bool PopupItemDisabled(const TestEditor &editor, std::string_view label) {
	for (const std::string &item : editor.observations.popupItems) {
		if (item.rfind(label, 0) == 0)
			return item.find(" disabled") != std::string::npos;
	}
	return false;
}

}

TEST_CASE("Context menu enablement uses named read-only and CanPaste") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "hello");
	editor.SetSel(0, 5);
	editor.ClearObservations();

	editor.ContextMenu(Point(10, 10));
	CHECK(PopupItemEnabled(editor, "Cut "));
	CHECK(PopupItemEnabled(editor, "Copy "));
	// CanPaste is true when the document is writable (clipboard emptiness is host policy).
	CHECK(PopupItemEnabled(editor, "Paste "));
	CHECK(PopupItemEnabled(editor, "Delete "));
	CHECK(PopupItemEnabled(editor, "Select All "));

	editor.ClearObservations();
	editor.SetReadOnly(true);
	editor.ContextMenu(Point(10, 10));
	// Writable actions off when read-only; copy and select-all stay available.
	CHECK(PopupItemDisabled(editor, "Cut "));
	CHECK(PopupItemEnabled(editor, "Copy "));
	CHECK(PopupItemDisabled(editor, "Paste "));
	CHECK(PopupItemDisabled(editor, "Delete "));
	CHECK(PopupItemDisabled(editor, "Undo "));
	CHECK(PopupItemEnabled(editor, "Select All "));
}

TEST_CASE("Context menu commands run through ExecuteCommand") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "abcd");
	editor.SetSel(1, 3);
	editor.observations.clipboard.clear();

	editor.Command(TestEditor::IdCmdCopy);
	CHECK(editor.observations.clipboard == "bc");
	CHECK(editor.GetText() == "abcd");

	editor.Command(TestEditor::IdCmdCut);
	CHECK(editor.GetText() == "ad");
	CHECK(editor.observations.clipboard == "bc");

	editor.Command(TestEditor::IdCmdSelectAll);
	CHECK_FALSE(editor.GetSelectionEmpty());
	CHECK(editor.GetSelectionStart() == 0);
	CHECK(editor.GetSelectionEnd() == 2);

	editor.Command(TestEditor::IdCmdDelete);
	CHECK(editor.GetText() == "");
	CHECK(editor.CanUndo());

	editor.Command(TestEditor::IdCmdUndo);
	CHECK(editor.GetText() == "ad");
	CHECK(editor.CanRedo());

	editor.Command(TestEditor::IdCmdRedo);
	CHECK(editor.GetText() == "");

	editor.observations.clipboard = "xy";
	editor.Command(TestEditor::IdCmdPaste);
	CHECK(editor.GetText() == "xy");
}
