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

sptr_t Send(TestEditor &editor, Message message, uptr_t wParam = 0, sptr_t lParam = 0) {
	return editor.WndProc(message, wParam, lParam);
}

}

TEST_CASE("Autocomplete list box show is inspectable") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("");

	const char *list = "alpha beta gamma";
	Send(editor, Message::AutoCShow, 0, reinterpret_cast<sptr_t>(list));

	CHECK(Send(editor, Message::AutoCActive) != 0);
	CHECK(host.listBox.created);
	CHECK(host.listBox.visible);
	CHECK(host.listBox.items.size() == 3);
	CHECK(host.listBox.items[0].text == "alpha");
	CHECK(host.listBox.items[1].text == "beta");
	CHECK(host.listBox.items[2].text == "gamma");
	CHECK(host.listBox.selection == 0);
	CHECK(host.listBox.rect.Width() > 0);
	CHECK(host.listBox.rect.Height() > 0);
	CHECK(Send(editor, Message::AutoCGetCurrent) == 0);
}

TEST_CASE("Autocomplete cancel destroys the list box") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("");

	const char *list = "one two";
	Send(editor, Message::AutoCShow, 0, reinterpret_cast<sptr_t>(list));
	REQUIRE(Send(editor, Message::AutoCActive) != 0);

	// SCI_AUTOC_CANCEL only calls ac.Cancel(); it does not fire AutoCCancelled.
	// That notification comes from AutoCompleteCancel (CancelModes, stop chars).
	Send(editor, Message::AutoCCancel);

	CHECK(Send(editor, Message::AutoCActive) == 0);
	CHECK_FALSE(host.listBox.created);
	CHECK_FALSE(host.listBox.visible);
}

TEST_CASE("Autocomplete cancel through CancelModes notifies AutoCCancelled") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("x");

	const char *list = "one two";
	Send(editor, Message::AutoCShow, 0, reinterpret_cast<sptr_t>(list));
	REQUIRE(Send(editor, Message::AutoCActive) != 0);
	editor.ClearObservations();

	// Mouse down runs CancelModes -> AutoCompleteCancel, which notifies.
	editor.MouseDown(Point(0, 0), KeyMod::Norm);

	CHECK(Send(editor, Message::AutoCActive) == 0);
	CHECK(HasNotification(editor, Notification::AutoCCancelled));
	CHECK_FALSE(host.listBox.created);
}

TEST_CASE("Autocomplete complete via double-click inserts text and notifies") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("");

	const char *list = "alpha beta gamma";
	Send(editor, Message::AutoCShow, 0, reinterpret_cast<sptr_t>(list));
	REQUIRE(Send(editor, Message::AutoCActive) != 0);
	// Show selects the first item; move to the second before completing.
	Send(editor, Message::AutoCSelect, 0, reinterpret_cast<sptr_t>("beta"));
	REQUIRE(host.listBox.selection == 1);
	editor.ClearObservations();

	host.NotifyListBoxDoubleClick();

	CHECK(Send(editor, Message::AutoCActive) == 0);
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
	Send(editor, Message::GotoPos, 3, 0);

	const char *defn = "fn(int x)";
	Send(editor, Message::CallTipShow, 3, reinterpret_cast<sptr_t>(defn));

	CHECK(Send(editor, Message::CallTipActive) != 0);
	CHECK(host.callTip.created);
	CHECK(host.callTip.visible);
	CHECK(host.callTip.rect.Width() > 0);
	CHECK(host.callTip.rect.Height() > 0);
	CHECK(host.callTip.createCount == 1);
	REQUIRE(editor.observations.callTipWindows.size() == 1);
	CHECK(editor.observations.callTipWindows[0].Width() == host.callTip.rect.Width());

	Send(editor, Message::CallTipCancel);

	CHECK(Send(editor, Message::CallTipActive) == 0);
	CHECK_FALSE(host.callTip.created);
	CHECK_FALSE(host.callTip.visible);
}

TEST_CASE("Call tip and autocomplete cancel each other") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("fn");
	Send(editor, Message::GotoPos, 2, 0);

	const char *list = "fn function";
	Send(editor, Message::AutoCShow, 0, reinterpret_cast<sptr_t>(list));
	REQUIRE(Send(editor, Message::AutoCActive) != 0);
	REQUIRE(host.listBox.created);

	const char *defn = "fn()";
	Send(editor, Message::CallTipShow, 2, reinterpret_cast<sptr_t>(defn));

	// CallTipShow cancels autocomplete first.
	CHECK(Send(editor, Message::AutoCActive) == 0);
	CHECK_FALSE(host.listBox.created);
	CHECK(Send(editor, Message::CallTipActive) != 0);
	CHECK(host.callTip.created);
	CHECK(host.callTip.visible);

	// AutoCompleteStart cancels an open call tip.
	Send(editor, Message::AutoCShow, 0, reinterpret_cast<sptr_t>(list));
	CHECK(Send(editor, Message::CallTipActive) == 0);
	CHECK_FALSE(host.callTip.created);
	CHECK(Send(editor, Message::AutoCActive) != 0);
	CHECK(host.listBox.created);
}

TEST_CASE("List box SetList parses type separators") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("");

	// Default type separator is '?'.
	const char *list = "alpha?1 beta?2";
	Send(editor, Message::AutoCShow, 0, reinterpret_cast<sptr_t>(list));

	REQUIRE(host.listBox.items.size() == 2);
	CHECK(host.listBox.items[0].text == "alpha");
	CHECK(host.listBox.items[0].type == 1);
	CHECK(host.listBox.items[1].text == "beta");
	CHECK(host.listBox.items[1].type == 2);
}
