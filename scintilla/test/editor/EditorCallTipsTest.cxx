// scalpel-editor test code
/** @file EditorCallTipsTest.cxx
 ** Focused behavior tests for call tips.
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

struct CallTipActiveSnapshot {
	bool active = false;
	bool windowCreated = false;
	bool windowVisible = false;
	int createCount = 0;
	Sci::Position posStart = 0;
	bool listActive = false;

	bool operator==(const CallTipActiveSnapshot &other) const noexcept {
		return active == other.active
			&& windowCreated == other.windowCreated
			&& windowVisible == other.windowVisible
			&& createCount == other.createCount
			&& posStart == other.posStart
			&& listActive == other.listActive;
	}
};

CallTipActiveSnapshot CaptureShow(bool throughMessage) {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("fn(");
	editor.WndProc(Message::GotoPos, 3, 0);
	const char *defn = "fn(int x)";
	if (throughMessage) {
		editor.WndProc(Message::CallTipShow, 3, reinterpret_cast<sptr_t>(defn));
	} else {
		editor.CallTipShow(3, defn);
	}
	CallTipActiveSnapshot snapshot;
	snapshot.active = editor.CallTipActive();
	snapshot.windowCreated = host.callTip.created;
	snapshot.windowVisible = host.callTip.visible;
	snapshot.createCount = host.callTip.createCount;
	snapshot.posStart = editor.CallTipPosStart();
	snapshot.listActive = editor.AutoCActive();
	return snapshot;
}

}

TEST_CASE("Call tip show and cancel update host state") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("fn(");
	editor.WndProc(Message::GotoPos, 3, 0);

	CHECK_FALSE(editor.CallTipActive());
	editor.CallTipShow(3, "fn(int x)");

	CHECK(editor.CallTipActive());
	CHECK(host.callTip.created);
	CHECK(host.callTip.visible);
	CHECK(host.callTip.rect.Width() > 0);
	CHECK(host.callTip.rect.Height() > 0);
	CHECK(editor.CallTipPosStart() == 3);
	REQUIRE(editor.observations.callTipWindows.size() == 1);

	editor.CallTipCancel();
	CHECK_FALSE(editor.CallTipActive());
	CHECK_FALSE(host.callTip.created);
	CHECK_FALSE(host.callTip.visible);
}

TEST_CASE("Call tip message path matches named method") {
	CHECK(CaptureShow(false) == CaptureShow(true));

	TestHost host;
	TestEditor editor(host);
	editor.SetText("fn(");
	editor.CallTipShow(3, "fn()");
	CHECK(editor.WndProc(Message::CallTipActive, 0, 0) != 0);
	CHECK(editor.WndProc(Message::CallTipPosStart, 0, 0) == editor.CallTipPosStart());
	editor.CallTipSetPosStart(1);
	CHECK(editor.CallTipPosStart() == 1);
	CHECK(editor.WndProc(Message::CallTipPosStart, 0, 0) == 1);
}

TEST_CASE("Call tip cancels autocomplete and autocomplete cancels call tip") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("fn");
	editor.WndProc(Message::GotoPos, 2, 0);

	editor.AutoCShow(0, "fn function");
	REQUIRE(editor.AutoCActive());
	REQUIRE(host.listBox.created);

	editor.CallTipShow(2, "fn()");
	CHECK_FALSE(editor.AutoCActive());
	CHECK_FALSE(host.listBox.created);
	CHECK(editor.CallTipActive());
	CHECK(host.callTip.created);

	editor.AutoCShow(0, "fn function");
	CHECK_FALSE(editor.CallTipActive());
	CHECK_FALSE(host.callTip.created);
	CHECK(editor.AutoCActive());
	CHECK(host.listBox.created);
}

TEST_CASE("Call tip highlight position style and placement setters") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("fn(");
	editor.CallTipShow(3, "fn(int x, int y)");
	REQUIRE(editor.CallTipActive());

	// Highlight "int x" (indices into the tip text).
	editor.CallTipSetHlt(3, 8);
	editor.CallTipSetBack(ColourRGBA(0xff, 0xff, 0xee));
	editor.CallTipSetFore(ColourRGBA(0x20, 0x20, 0x20));
	editor.CallTipSetForeHlt(ColourRGBA(0x00, 0x00, 0x80));
	editor.CallTipUseStyle(8);
	editor.CallTipSetPosition(true);
	// Still active after option changes.
	CHECK(editor.CallTipActive());

	editor.ClearObservations();
	editor.CallTipCancel();
	CHECK_FALSE(editor.CallTipActive());
}

TEST_CASE("Call tip cancel through CancelModes") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("fn(");
	editor.CallTipShow(3, "fn()");
	REQUIRE(editor.CallTipActive());

	editor.MouseDown(Point(0, 0), KeyMod::Norm);
	CHECK_FALSE(editor.CallTipActive());
	CHECK_FALSE(host.callTip.created);
}

TEST_CASE("Call tip message setters forward to named methods") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("x");
	editor.CallTipShow(0, "tip");
	REQUIRE(editor.CallTipActive());

	editor.WndProc(Message::CallTipSetPosStart, 2, 0);
	CHECK(editor.CallTipPosStart() == 2);
	editor.WndProc(Message::CallTipSetHlt, 0, 3);
	editor.WndProc(Message::CallTipSetPosition, 1, 0);
	editor.WndProc(Message::CallTipCancel, 0, 0);
	CHECK_FALSE(editor.CallTipActive());
}
