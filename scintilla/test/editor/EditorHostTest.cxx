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

	CHECK(editor.WndProc(Message::GetModEventMask, 0, 0) ==
		static_cast<sptr_t>(editor.GetModEventMask()));
	CHECK(editor.WndProc(Message::GetCommandEvents, 0, 0) == 0);
	CHECK(editor.WndProc(Message::GetStatus, 0, 0) == static_cast<sptr_t>(Status::RegEx));

	editor.WndProc(Message::SetModEventMask,
		static_cast<uptr_t>(ModificationFlags::EventMaskAll), 0);
	editor.WndProc(Message::SetCommandEvents, 1, 0);
	editor.WndProc(Message::SetStatus, static_cast<uptr_t>(Status::Ok), 0);
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
		CHECK(editor.WndProc(Message::SupportsFeature, static_cast<uptr_t>(feature), 0) == 0);
	}
}

TEST_CASE("Deleted widget identifier messages fall through without affecting notifications") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "solo");
	editor.ClearObservations();

	// SetIdentifier / GetIdentifier are removed from dispatch; they fall through to DefWndProc.
	CHECK(editor.WndProc(Message::SetIdentifier, 99, 0) == 0);
	CHECK(editor.WndProc(Message::GetIdentifier, 0, 0) == 0);
	CHECK(std::find(editor.observations.defaultWindowCalls.begin(),
		editor.observations.defaultWindowCalls.end(),
		Message::SetIdentifier) != editor.observations.defaultWindowCalls.end());
	CHECK(std::find(editor.observations.defaultWindowCalls.begin(),
		editor.observations.defaultWindowCalls.end(),
		Message::GetIdentifier) != editor.observations.defaultWindowCalls.end());
	CHECK(editor.Text() == "solo");

	// The single test host still receives notifications without a widget identifier.
	editor.ClearObservations();
	editor.InsertInput("x");
	CHECK(ModifiedNotifications(editor) >= 1);
	CHECK(editor.Text().size() == 5);
	CHECK(editor.Text().find('x') != std::string::npos);
}

TEST_CASE("Deleted direct-call accessor messages fall through") {
	TestHost host;
	TestEditor editor(host);
	LoadClean(editor, "direct");
	editor.ClearObservations();

	// These never had core Editor dispatch; they only existed as platform direct-call glue.
	CHECK(editor.WndProc(Message::GetDirectFunction, 0, 0) == 0);
	CHECK(editor.WndProc(Message::GetDirectStatusFunction, 0, 0) == 0);
	CHECK(editor.WndProc(Message::GetDirectPointer, 0, 0) == 0);
	CHECK(std::find(editor.observations.defaultWindowCalls.begin(),
		editor.observations.defaultWindowCalls.end(),
		Message::GetDirectFunction) != editor.observations.defaultWindowCalls.end());
	CHECK(std::find(editor.observations.defaultWindowCalls.begin(),
		editor.observations.defaultWindowCalls.end(),
		Message::GetDirectStatusFunction) != editor.observations.defaultWindowCalls.end());
	CHECK(std::find(editor.observations.defaultWindowCalls.begin(),
		editor.observations.defaultWindowCalls.end(),
		Message::GetDirectPointer) != editor.observations.defaultWindowCalls.end());
	CHECK(editor.Text() == "direct");
	// Named whole-buffer and range access remain the positive path.
	CHECK(editor.GetTextLength() == 6);
}
