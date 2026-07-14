// scalpel-editor test code
/** @file EditorWrappingTest.cxx
 ** Focused behavior tests for line wrapping.
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

TestEditorSnapshot WrapSnapshot(bool throughMessage) {
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

Sci::Line WrapCount(TestEditor &editor, Sci::Line line) {
	return static_cast<Sci::Line>(editor.WndProc(Message::WrapCount, static_cast<uptr_t>(line), 0));
}

struct WrapSetting {
	Message setter;
	Message getter;
	uptr_t value;
	bool reconfiguresScrollbars;
	const char *name;
};

constexpr std::array wrapSettings {
	WrapSetting { Message::SetWrapVisualFlags, Message::GetWrapVisualFlags,
		static_cast<uptr_t>(WrapVisualFlag::End), true, "visual flags" },
	WrapSetting { Message::SetWrapVisualFlagsLocation, Message::GetWrapVisualFlagsLocation,
		static_cast<uptr_t>(WrapVisualLocation::EndByText), false, "visual flag location" },
	WrapSetting { Message::SetWrapStartIndent, Message::GetWrapStartIndent,
		3, true, "start indent" },
	WrapSetting { Message::SetWrapIndentMode, Message::GetWrapIndentMode,
		static_cast<uptr_t>(WrapIndentMode::Indent), true, "indent mode" },
};

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

TEST_CASE("Wrap message forwards to the named method") {
	CHECK(WrapSnapshot(true) == WrapSnapshot(false));

	TestHost host;
	TestEditor editor(host);
	editor.SetWrapMode(Wrap::Char);
	CHECK(static_cast<Wrap>(editor.WndProc(Message::GetWrapMode, 0, 0)) == editor.GetWrapMode());
}

TEST_CASE("Wrap display settings redraw only when changed") {
	for (const WrapSetting &setting : wrapSettings) {
		DYNAMIC_SECTION(setting.name) {
			TestHost host;
			TestEditor editor(host);
			CHECK(editor.WndProc(setting.getter, 0, 0) == 0);
			editor.PaintAll();
			editor.ClearObservations();

			editor.WndProc(setting.setter, setting.value, 0);

			CHECK(static_cast<uptr_t>(editor.WndProc(setting.getter, 0, 0)) == setting.value);
			CHECK(editor.Snapshot().invalidatedRectangles == 1);
			CHECK(editor.Snapshot().scrollbarReconfigurations == (setting.reconfiguresScrollbars ? 1 : 0));

			editor.PaintAll();
			editor.ClearObservations();
			editor.WndProc(setting.setter, setting.value, 0);

			CHECK(editor.Snapshot().invalidatedRectangles == 0);
			CHECK(editor.Snapshot().scrollbarReconfigurations == 0);
		}
	}
}

TEST_CASE("Wrap count reports display rows and disabling wrap restores one row") {
	TestHost host;
	TestEditor editor(host, PRectangle(0, 0, 100, 100));
	editor.SetText("one two three four five six");
	CHECK(WrapCount(editor, 0) == 1);

	editor.SetWrapMode(Wrap::Word);
	editor.PaintAll();
	CHECK(WrapCount(editor, 0) > 1);

	editor.SetWrapMode(Wrap::None);
	editor.PaintAll();
	CHECK(WrapCount(editor, 0) == 1);
}

TEST_CASE("Wrapping recalculates display rows after resize and edit") {
	TestHost host;
	TestEditor editor(host, PRectangle(0, 0, 100, 100));
	editor.SetText("one two three four five six");
	editor.SetWrapMode(Wrap::Word);
	editor.PaintAll();
	const Sci::Line narrowCount = WrapCount(editor, 0);
	REQUIRE(narrowCount > 1);

	editor.SetClientRectangle(PRectangle(0, 0, 300, 100));
	editor.PaintAll();
	const Sci::Line wideCount = WrapCount(editor, 0);
	CHECK(wideCount < narrowCount);

	editor.InsertInput(" seven eight nine ten eleven twelve");
	CHECK(editor.observations.idleRequested);
	editor.PaintAll();
	CHECK(WrapCount(editor, 0) > wideCount);
}
