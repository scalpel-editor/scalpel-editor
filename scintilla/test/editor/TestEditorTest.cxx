// scalpel-editor test code
/** @file TestEditorTest.cxx
 ** Contract and focused behavior tests for the concrete test editor.
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

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

using namespace Scintilla;
using namespace Scintilla::Internal;

TEST_CASE("Test editor contract") {
	TestHost host;
	TestEditor editor(host, PRectangle(0, 0, 320, 200));

	CHECK(editor.ClientRectangle() == PRectangle(0, 0, 320, 200));
	editor.SetText("one\ntwo \xE2\x82\xAC");
	CHECK(editor.Text() == "one\ntwo \xE2\x82\xAC");
	editor.InsertInput("!");
	CHECK(editor.Text() == "one\ntwo \xE2\x82\xAC!");

	editor.SetClientRectangle(PRectangle(0, 0, 500, 300));
	CHECK(editor.ClientRectangle() == PRectangle(0, 0, 500, 300));
}

static TestEditorSnapshot WrapSnapshot(bool throughMessage) {
	TestHost host;
	TestEditor editor(host, PRectangle(0, 0, 320, 200));
	editor.SetText("a line long enough to wrap in the test window");
	editor.SetHorizontalOffset(40);
	editor.PaintAll();
	editor.ClearObservations();

	if (throughMessage) {
		editor.WndProc(Message::SetWrapMode, static_cast<uptr_t>(Wrap::Word), 0);
	} else {
		editor.SetWrapMode(Wrap::Word);
	}
	editor.FlushUpdateNotifications();
	return editor.Snapshot();
}

TEST_CASE("Wrap mode changes editor and host state") {
	const TestEditorSnapshot snapshot = WrapSnapshot(false);

	CHECK(snapshot.wrapMode == Wrap::Word);
	CHECK(snapshot.horizontalOffset == 0);
	CHECK(snapshot.invalidatedRectangles == 1);
	CHECK(snapshot.scrollbarReconfigurations == 1);
	REQUIRE(snapshot.updateNotifications.size() == 1);
	CHECK(FlagSet(snapshot.updateNotifications[0], Update::HScroll));
}

TEST_CASE("Setting the current wrap mode has no change effects") {
	TestHost host;
	TestEditor editor(host);
	editor.SetWrapMode(Wrap::Word);
	editor.FlushUpdateNotifications();
	editor.ClearObservations();

	editor.SetWrapMode(Wrap::Word);
	editor.FlushUpdateNotifications();

	const TestEditorSnapshot snapshot = editor.Snapshot();
	CHECK(snapshot.wrapMode == Wrap::Word);
	CHECK(snapshot.invalidateAllCount == 0);
	CHECK(snapshot.invalidatedRectangles == 0);
	CHECK(snapshot.scrollbarReconfigurations == 0);
	CHECK(snapshot.updateNotifications.empty());
}

TEST_CASE("Wrap message forwards to the named method") {
	CHECK(WrapSnapshot(true) == WrapSnapshot(false));

	TestHost host;
	TestEditor editor(host);
	editor.SetWrapMode(Wrap::Char);
	CHECK(static_cast<Wrap>(editor.WndProc(Message::GetWrapMode, 0, 0)) == editor.GetWrapMode());
}

TEST_CASE("Invalid UTF-8 passes through the editor unchanged") {
	// Policy test: the editor never validates, rejects, or rewrites the bytes it is
	// given. See the Document doc comment for the invalid-UTF-8 policy.
	TestHost host;
	TestEditor editor(host);
	// a [80] [C2] z : a stray trail byte and a lead byte whose trail byte is missing
	editor.SetText("a\x80\xc2z");
	CHECK(editor.Text() == "a\x80\xc2z");

	// Caret movement crosses each invalid byte as one character
	editor.WndProc(Message::GotoPos, 0, 0);
	bool consumed = false;
	for (Sci::Position expected = 1; expected <= 4; expected++) {
		editor.KeyDown(Keys::Right, KeyMod::Norm, &consumed);
		CHECK(static_cast<Sci::Position>(editor.WndProc(Message::GetCurrentPos, 0, 0)) == expected);
	}

	// Case conversion changes letters and leaves invalid bytes unchanged
	editor.WndProc(Message::SelectAll, 0, 0);
	editor.WndProc(Message::UpperCase, 0, 0);
	CHECK(editor.Text() == "A\x80\xc2Z");
}

TEST_CASE("UpperCase maps Unicode letters through CaseConvertString") {
	// CaseMapString uses Unicode case conversion, not ASCII-only byte mapping.
	// "café" (c a f U+00E9) uppercases to "CAFÉ"; "ß" (U+00DF) uppercases to "SS".
	TestHost host;
	TestEditor editor(host);
	editor.SetText("café ß");
	editor.WndProc(Message::SelectAll, 0, 0);
	editor.WndProc(Message::UpperCase, 0, 0);
	CHECK(editor.Text() == "CAFÉ SS");

	editor.WndProc(Message::SelectAll, 0, 0);
	editor.WndProc(Message::LowerCase, 0, 0);
	// "SS" does not round-trip to "ß"; lowercasing yields "ss".
	CHECK(editor.Text() == "café ss");
}
