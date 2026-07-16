// scalpel-editor test code
/** @file EditorHostTest.cxx
 ** Focused tests for fixed-host notification policy, status, and features.
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

size_t ModifiedNotifications(const TestEditor &editor) {
	return static_cast<size_t>(std::count_if(editor.observations.notifications.begin(),
		editor.observations.notifications.end(), [](const TestNotification &notification) {
			return notification.code == Notification::Modified;
		}));
}

}

TEST_CASE("Host notification policy defaults and round-trips") {
	TestHost host;
	TestEditor editor(host);

	CHECK(editor.GetModEventMask() == ModificationFlags::EventMaskAll);
	CHECK(editor.GetCommandEvents());
	CHECK(editor.GetStatus() == Status::Ok);

	editor.SetModEventMask(ModificationFlags::InsertText | ModificationFlags::DeleteText);
	editor.SetCommandEvents(false);
	editor.SetStatus(Status::RegEx);
	CHECK(editor.GetModEventMask() ==
		(ModificationFlags::InsertText | ModificationFlags::DeleteText));
	CHECK_FALSE(editor.GetCommandEvents());
	CHECK(editor.GetStatus() == Status::RegEx);

	editor.SetModEventMask(ModificationFlags::EventMaskAll);
	editor.SetCommandEvents(true);
	editor.SetStatus(Status::Ok);
	CHECK(editor.GetModEventMask() == ModificationFlags::EventMaskAll);
	CHECK(editor.GetCommandEvents());
	CHECK(editor.GetStatus() == Status::Ok);
}

TEST_CASE("Modification mask filters detailed and command change notifications") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "base");

	editor.SetModEventMask(ModificationFlags::None);
	editor.ClearObservations();
	editor.InsertInput("x");
	CHECK(ModifiedNotifications(editor) == 0);
	CHECK(editor.observations.changeNotifications == 0);

	editor.SetModEventMask(ModificationFlags::InsertText);
	editor.ClearObservations();
	editor.InsertInput("y");
	CHECK(ModifiedNotifications(editor) == 1);
	CHECK(editor.observations.changeNotifications == 1);
}

TEST_CASE("Command events only filter the coarse text-change callback") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "base");
	editor.SetModEventMask(ModificationFlags::InsertText);
	editor.SetCommandEvents(false);
	editor.ClearObservations();

	editor.InsertInput("x");
	CHECK(ModifiedNotifications(editor) == 1);
	CHECK(editor.observations.changeNotifications == 0);
}

TEST_CASE("Fixed test surface reports every feature as unsupported") {
	TestHost host;
	TestEditor editor(host);
	constexpr std::array features {
		Supports::LineDrawsFinal,
		Supports::PixelDivisions,
		Supports::FractionalStrokeWidth,
		Supports::TranslucentStroke,
		Supports::PixelModification,
		Supports::ThreadSafeMeasureWidths,
	};

	for (const Supports feature : features) {
		CAPTURE(feature);
		CHECK(editor.SupportsFeature(feature) == 0);
	}
}

TEST_CASE("Named whole-buffer access works without widget identifier messages") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "solo");

	// Positive path: notifications still reach the single test host without a
	// platform widget identifier (those messages were deleted from dispatch).
	editor.ClearObservations();
	editor.InsertInput("x");
	CHECK(ModifiedNotifications(editor) >= 1);
	CHECK(editor.Text().size() == 5);
	CHECK(editor.Text().find('x') != std::string::npos);
	CHECK(editor.GetTextLength() == 5);
}

TEST_CASE("Named buffer length remains available without direct-call accessors") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "direct");
	// Platform direct-call glue messages are gone; length is still named.
	CHECK(editor.GetTextLength() == 6);
	CHECK(editor.GetText() == "direct");
}
