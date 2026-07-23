// scalpel-editor test code
/** @file EditorWrappingTest.cxx
 ** Focused behavior tests for line wrapping.
 **/

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
#include "AutoComplete.h"
#include "CallTip.h"
#include "ScintillaBase.h"

#include "TestPlatform.h"
#include "TestEditor.h"

#include "catch.hpp"

using namespace Scintilla;
using namespace Scintilla::Internal;

namespace {

TestEditorSnapshot WrapSnapshot() {
	TestHost host;
	TestEditor editor(host, PRectangle(0, 0, 320, 200));
	editor.SetWrapMode(Wrap::None);
	editor.SetText("a line long enough to wrap in the test window");
	editor.SetHorizontalOffset(40);
	editor.PaintAll();
	editor.ClearObservations();

	editor.SetWrapMode(Wrap::Word);
	editor.FlushUpdateNotifications();
	return editor.Snapshot();
}

}

TEST_CASE("Wrap mode defaults to word wrapping") {
	TestHost host;
	TestEditor editor(host);

	CHECK(editor.GetWrapMode() == Wrap::Word);
}

TEST_CASE("Wrap mode changes editor and host state") {
	const TestEditorSnapshot snapshot = WrapSnapshot();

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
	editor.PaintAll();
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

TEST_CASE("Wrap display settings redraw only when changed") {
	SECTION("visual flags") {
		TestHost host;
		TestEditor editor(host);
		CHECK(editor.GetWrapVisualFlags() == WrapVisualFlag::None);
		editor.PaintAll();
		editor.ClearObservations();

		editor.SetWrapVisualFlags(WrapVisualFlag::End);

		CHECK(editor.GetWrapVisualFlags() == WrapVisualFlag::End);
		CHECK(editor.Snapshot().invalidatedRectangles == 1);
		CHECK(editor.Snapshot().scrollbarReconfigurations == 1);

		editor.PaintAll();
		editor.ClearObservations();
		editor.SetWrapVisualFlags(WrapVisualFlag::End);

		CHECK(editor.Snapshot().invalidatedRectangles == 0);
		CHECK(editor.Snapshot().scrollbarReconfigurations == 0);
	}
	SECTION("visual flag location") {
		TestHost host;
		TestEditor editor(host);
		CHECK(editor.GetWrapVisualFlagsLocation() == WrapVisualLocation::Default);
		editor.PaintAll();
		editor.ClearObservations();

		editor.SetWrapVisualFlagsLocation(WrapVisualLocation::EndByText);

		CHECK(editor.GetWrapVisualFlagsLocation() == WrapVisualLocation::EndByText);
		CHECK(editor.Snapshot().invalidatedRectangles == 1);
		CHECK(editor.Snapshot().scrollbarReconfigurations == 0);

		editor.PaintAll();
		editor.ClearObservations();
		editor.SetWrapVisualFlagsLocation(WrapVisualLocation::EndByText);

		CHECK(editor.Snapshot().invalidatedRectangles == 0);
		CHECK(editor.Snapshot().scrollbarReconfigurations == 0);
	}
	SECTION("start indent") {
		TestHost host;
		TestEditor editor(host);
		CHECK(editor.GetWrapStartIndent() == 0);
		editor.PaintAll();
		editor.ClearObservations();

		editor.SetWrapStartIndent(3);

		CHECK(editor.GetWrapStartIndent() == 3);
		CHECK(editor.Snapshot().invalidatedRectangles == 1);
		CHECK(editor.Snapshot().scrollbarReconfigurations == 1);

		editor.PaintAll();
		editor.ClearObservations();
		editor.SetWrapStartIndent(3);

		CHECK(editor.Snapshot().invalidatedRectangles == 0);
		CHECK(editor.Snapshot().scrollbarReconfigurations == 0);
	}
	SECTION("indent mode") {
		TestHost host;
		TestEditor editor(host);
		CHECK(editor.GetWrapIndentMode() == WrapIndentMode::Fixed);
		editor.PaintAll();
		editor.ClearObservations();

		editor.SetWrapIndentMode(WrapIndentMode::Indent);

		CHECK(editor.GetWrapIndentMode() == WrapIndentMode::Indent);
		CHECK(editor.Snapshot().invalidatedRectangles == 1);
		CHECK(editor.Snapshot().scrollbarReconfigurations == 1);

		editor.PaintAll();
		editor.ClearObservations();
		editor.SetWrapIndentMode(WrapIndentMode::Indent);

		CHECK(editor.Snapshot().invalidatedRectangles == 0);
		CHECK(editor.Snapshot().scrollbarReconfigurations == 0);
	}
}

TEST_CASE("Wrap count reports display rows and disabling wrap restores one row") {
	TestHost host;
	TestEditor editor(host, PRectangle(0, 0, 100, 100));
	editor.SetWrapMode(Wrap::None);
	editor.SetText("one two three four five six");
	CHECK(editor.WrapCount(0) == 1);

	editor.SetWrapMode(Wrap::Word);
	editor.PaintAll();
	CHECK(editor.WrapCount(0) > 1);

	editor.SetWrapMode(Wrap::None);
	editor.PaintAll();
	CHECK(editor.WrapCount(0) == 1);
}

TEST_CASE("Wrapping recalculates display rows after resize and edit") {
	TestHost host;
	TestEditor editor(host, PRectangle(0, 0, 100, 100));
	editor.SetText("one two three four five six");
	editor.SetWrapMode(Wrap::Word);
	editor.PaintAll();
	const Sci::Line narrowCount = editor.WrapCount(0);
	REQUIRE(narrowCount > 1);

	editor.SetClientRectangle(PRectangle(0, 0, 300, 100));
	editor.PaintAll();
	const Sci::Line wideCount = editor.WrapCount(0);
	CHECK(wideCount < narrowCount);

	editor.InsertInput(" seven eight nine ten eleven twelve");
	CHECK(editor.observations.idleRequested);
	editor.PaintAll();
	CHECK(editor.WrapCount(0) > wideCount);
}
