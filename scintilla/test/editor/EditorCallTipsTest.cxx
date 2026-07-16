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

TEST_CASE("Call tip show and cancel update host state") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("fn(");
	editor.GotoPos(3);

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

TEST_CASE("Call tip pos start set and get") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("fn(");
	editor.GotoPos(3);
	editor.CallTipShow(3, "fn()");
	CHECK(editor.CallTipActive());
	CHECK(editor.CallTipPosStart() == 3);
	editor.CallTipSetPosStart(1);
	CHECK(editor.CallTipPosStart() == 1);
}

TEST_CASE("Call tip cancels autocomplete and autocomplete cancels call tip") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("fn");
	editor.GotoPos(2);

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

TEST_CASE("Call tip named setters update tip state") {
	TestHost host;
	TestEditor editor(host);
	editor.SetText("x");
	editor.CallTipShow(0, "tip");
	REQUIRE(editor.CallTipActive());

	editor.CallTipSetPosStart(2);
	CHECK(editor.CallTipPosStart() == 2);
	editor.CallTipSetHlt(0, 3);
	editor.CallTipSetPosition(true);
	editor.CallTipCancel();
	CHECK_FALSE(editor.CallTipActive());
}
