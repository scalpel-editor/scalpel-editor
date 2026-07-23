#include <algorithm>
#include <initializer_list>
#include <optional>
#include <string_view>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "WaylandLifecycle.h"
#include "WaylandCursor.h"
#include "WaylandWindow.h"

namespace {

using Cursor = Scintilla::Internal::Window::Cursor;

std::string_view FirstAvailable(const Scalpel::WaylandCursorNames &names,
	std::initializer_list<std::string_view> available) {
	for (std::size_t index = 0; index < names.count; ++index) {
		if (std::find(available.begin(), available.end(), names[index]) != available.end()) {
			return names[index];
		}
	}
	return {};
}

}

TEST_CASE("Wayland cursor choices cover every editor cursor") {
	const std::array cursors{
		Cursor::invalid, Cursor::text, Cursor::arrow, Cursor::up, Cursor::wait,
		Cursor::horizontal, Cursor::vertical, Cursor::reverseArrow, Cursor::hand,
	};
	for (const Cursor cursor : cursors) {
		const Scalpel::WaylandCursorNames names = Scalpel::CursorNames(cursor);
		INFO(static_cast<int>(cursor));
		REQUIRE(names.count > 0);
		CHECK(names[names.count - 1] == "arrow");
	}
	CHECK(Scalpel::CursorNames(Cursor::text)[0] == "text");
	CHECK(Scalpel::CursorNames(Cursor::reverseArrow)[0] == "right_ptr");
	CHECK(Scalpel::CursorNames(Cursor::hand)[0] == "pointer");
	CHECK(FirstAvailable(Scalpel::CursorNames(Cursor::text), {"left_ptr"}) ==
		"left_ptr");
	CHECK(FirstAvailable(Scalpel::CursorNames(Cursor::hand), {"arrow"}) == "arrow");
}

TEST_CASE("Wayland cursor state retains requests until pointer entry") {
	Scalpel::WaylandCursorState cursor;

	CHECK_FALSE(cursor.Request(Cursor::text).has_value());
	CHECK_FALSE(cursor.Enter(42).has_value());
	const auto available = cursor.SetThemeAvailable(true);
	REQUIRE(available.has_value());
	CHECK(available->cursor == Cursor::text);
	CHECK(available->serial == 42);
	CHECK(available->scale == 1);

	const auto changed = cursor.Request(Cursor::hand);
	REQUIRE(changed.has_value());
	CHECK(changed->cursor == Cursor::hand);
	CHECK_FALSE(cursor.Request(Cursor::hand).has_value());
}

TEST_CASE("Wayland cursor state clears stale pointer serials") {
	Scalpel::WaylandCursorState cursor;
	(void)cursor.SetThemeAvailable(true);
	REQUIRE(cursor.Enter(7).has_value());

	cursor.Leave();
	CHECK_FALSE(cursor.Request(Cursor::arrow).has_value());
	const auto reentered = cursor.Enter(8);
	REQUIRE(reentered.has_value());
	CHECK(reentered->serial == 8);
	CHECK(reentered->cursor == Cursor::arrow);

	cursor.ResetPointer();
	CHECK_FALSE(cursor.Request(Cursor::wait).has_value());
	const auto replacement = cursor.Enter(9);
	REQUIRE(replacement.has_value());
	CHECK(replacement->serial == 9);
	CHECK(replacement->cursor == Cursor::wait);
}

TEST_CASE("Wayland cursor state rebuilds scaled themes and hotspots") {
	Scalpel::WaylandCursorState cursor;
	(void)cursor.SetThemeAvailable(true);
	(void)cursor.Enter(24);
	const auto scaled = cursor.SetScale(2);
	REQUIRE(scaled.has_value());
	CHECK(scaled->scale == 2);
	CHECK(Scalpel::CursorThemePixelSize(24, scaled->scale) == 48);
	CHECK(Scalpel::CursorImageGeometry(48, 48, 14, 18, scaled->scale) ==
		Scalpel::WaylandCursorImageGeometry{48, 48, 7, 9});
	CHECK_THROWS_WITH(cursor.SetScale(0), "Wayland cursor scale must be positive");
}

TEST_CASE("Wayland window reports the constructor argument it rejects") {
	CHECK_THROWS_WITH(Scalpel::WaylandWindow(nullptr, 800, 600),
		"WaylandWindow requires a title");
	CHECK_THROWS_WITH(Scalpel::WaylandWindow("scalpel-editor", 0, 600),
		"WaylandLifecycle requires a positive size");
}

TEST_CASE("Wayland lifecycle coalesces configured sizes") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	lifecycle.ProposeSize(900, 650);
	lifecycle.ProposeSize(1024, 768);
	CHECK(lifecycle.CommitConfigure() == Scalpel::WindowSize{1024, 768});
	CHECK(lifecycle.Width() == 1024);
	CHECK(lifecycle.Height() == 768);
	CHECK(lifecycle.TakeResize() == Scalpel::WindowSize{1024, 768});
	CHECK_FALSE(lifecycle.TakeResize().has_value());

	lifecycle.ProposeSize(1100, 700);
	lifecycle.ProposeSize(0, 0);
	CHECK_FALSE(lifecycle.CommitConfigure().has_value());
	lifecycle.ProposeSize(1024, 768);
	CHECK_FALSE(lifecycle.CommitConfigure().has_value());
}

TEST_CASE("Wayland size proposals preserve toplevel state") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	lifecycle.ProposeToplevel(900, 650, {1, 4});
	lifecycle.ProposeSize(1024, 768);
	REQUIRE(lifecycle.CommitConfigure() == Scalpel::WindowSize{1024, 768});
	CHECK(lifecycle.ToplevelState().maximized);
	CHECK(lifecycle.ToplevelState().activated);

	lifecycle.ProposeSize(1100, 700);
	REQUIRE(lifecycle.CommitConfigure() == Scalpel::WindowSize{1100, 700});
	CHECK(lifecycle.ToplevelState().maximized);
	CHECK(lifecycle.ToplevelState().activated);
}

TEST_CASE("Wayland lifecycle commits retained toplevel state") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	lifecycle.ProposeConfigureBounds(1600, 1000);
	CHECK_FALSE(lifecycle.ToplevelState().serverSideDecoration);
	lifecycle.ProposeWmCapabilities({1, 2, 3, 4, 999});
	lifecycle.ProposeDecoration(true);
	lifecycle.ProposeToplevel(1024, 768, {1, 2, 4, 999});
	CHECK_FALSE(lifecycle.ToplevelState().configureBounds.has_value());
	REQUIRE(lifecycle.CommitConfigure() == Scalpel::WindowSize{1024, 768});
	const Scalpel::WaylandToplevelState &configured = lifecycle.ToplevelState();
	CHECK(configured.configureBounds == Scalpel::WindowSize{1600, 1000});
	CHECK(configured.maximized);
	CHECK(configured.fullscreen);
	CHECK_FALSE(configured.resizing);
	CHECK(configured.activated);
	CHECK(configured.windowMenuAvailable);
	CHECK(configured.maximizeAvailable);
	CHECK(configured.fullscreenAvailable);
	CHECK(configured.minimizeAvailable);
	CHECK(configured.serverSideDecoration);

	lifecycle.ProposeConfigureBounds(0, 0);
	lifecycle.ProposeWmCapabilities({});
	lifecycle.ProposeDecoration(false);
	lifecycle.ProposeToplevel(0, 0, {3});
	CHECK_FALSE(lifecycle.CommitConfigure().has_value());
	const Scalpel::WaylandToplevelState &updated = lifecycle.ToplevelState();
	CHECK_FALSE(updated.configureBounds.has_value());
	CHECK_FALSE(updated.maximized);
	CHECK_FALSE(updated.fullscreen);
	CHECK(updated.resizing);
	CHECK_FALSE(updated.activated);
	CHECK_FALSE(updated.windowMenuAvailable);
	CHECK_FALSE(updated.maximizeAvailable);
	CHECK_FALSE(updated.fullscreenAvailable);
	CHECK_FALSE(updated.minimizeAvailable);
	CHECK_FALSE(updated.serverSideDecoration);
}

TEST_CASE("Wayland lifecycle retains a close request") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	CHECK_FALSE(lifecycle.CloseRequested());
	lifecycle.RequestClose();
	CHECK(lifecycle.CloseRequested());
}

TEST_CASE("Wayland registry binds each global only once") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	const auto compositor = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Compositor, 10, 6);
	REQUIRE(compositor.size() == 1);
	CHECK(compositor.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindCompositor, 10, 6});
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Compositor, 10, 6).empty());
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Compositor, 11, 6).empty());

	const auto wmBase = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::WmBase, 20, 7);
	REQUIRE(wmBase.size() == 1);
	CHECK(wmBase.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindWmBase, 20, 7});
}

TEST_CASE("Wayland registry replaces a removed decoration manager") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	const auto first = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::DecorationManager, 25, 1);
	REQUIRE(first.size() == 1);
	CHECK(first.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindDecorationManager, 25, 1});
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::DecorationManager, 26, 1).empty());

	const auto replacement = lifecycle.RemoveGlobal(25);
	REQUIRE(replacement.size() == 2);
	CHECK(replacement[0] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::ReleaseDecorationManager, 25});
	CHECK(replacement[1] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindDecorationManager, 26, 1});
	lifecycle.ProposeDecoration(true);
	(void)lifecycle.CommitConfigure();
	const auto removed = lifecycle.RemoveGlobal(26);
	REQUIRE(removed.size() == 1);
	CHECK(removed.front().type ==
		Scalpel::WaylandLifecycleActionType::ReleaseDecorationManager);
	CHECK(lifecycle.ToplevelState().serverSideDecoration);
}

TEST_CASE("Wayland registry replaces optional shared memory") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	const auto first = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::SharedMemory, 27, 1);
	REQUIRE(first.size() == 1);
	CHECK(first.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindSharedMemory, 27, 1});
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::SharedMemory, 28, 1).empty());

	const auto replacement = lifecycle.RemoveGlobal(27);
	REQUIRE(replacement.size() == 2);
	CHECK(replacement[0] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::ReleaseSharedMemory, 27});
	CHECK(replacement[1] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindSharedMemory, 28, 1});
	const auto removed = lifecycle.RemoveGlobal(28);
	REQUIRE(removed.size() == 1);
	CHECK(removed.front().type ==
		Scalpel::WaylandLifecycleActionType::ReleaseSharedMemory);
}

TEST_CASE("Wayland registry closes when an active required global disappears") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);
	(void)lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Compositor, 10, 4);
	(void)lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Compositor, 11, 4);

	CHECK(lifecycle.RemoveGlobal(999).empty());
	CHECK(lifecycle.RemoveGlobal(11).empty());
	CHECK_FALSE(lifecycle.CloseRequested());
	const auto actions = lifecycle.RemoveGlobal(10);
	REQUIRE(actions.size() == 1);
	CHECK(actions.front().type == Scalpel::WaylandLifecycleActionType::Close);
	CHECK(lifecycle.CloseRequested());
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Compositor, 12, 4).empty());
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Output, 30, 4).empty());
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Seat, 40, 9).empty());
	CHECK(lifecycle.OutputCount() == 0);
	CHECK(lifecycle.SeatCount() == 0);
}

TEST_CASE("Wayland lifecycle tracks hot-plugged output membership") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);
	CHECK(lifecycle.OutputCount() == 0);
	CHECK(lifecycle.EnteredOutputCount() == 0);

	const auto first = lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Output, 30, 4);
	const auto second = lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Output, 31, 2);
	REQUIRE(first.size() == 1);
	REQUIRE(second.size() == 1);
	CHECK(first.front().type == Scalpel::WaylandLifecycleActionType::BindOutput);
	CHECK(second.front().type == Scalpel::WaylandLifecycleActionType::BindOutput);
	CHECK(lifecycle.OutputCount() == 2);

	lifecycle.EnterOutput(30);
	lifecycle.EnterOutput(31);
	lifecycle.EnterOutput(999);
	CHECK(lifecycle.OutputEntered(30));
	CHECK(lifecycle.OutputEntered(31));
	CHECK(lifecycle.EnteredOutputCount() == 2);
	lifecycle.LeaveOutput(30);
	CHECK_FALSE(lifecycle.OutputEntered(30));
	CHECK(lifecycle.EnteredOutputCount() == 1);

	const auto removed = lifecycle.RemoveGlobal(31);
	REQUIRE(removed.size() == 1);
	CHECK(removed.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::ReleaseOutput, 31});
	CHECK(lifecycle.OutputCount() == 1);
	CHECK(lifecycle.EnteredOutputCount() == 0);
}

TEST_CASE("Wayland lifecycle selects one hot-plugged seat") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);
	CHECK(lifecycle.SeatCount() == 0);
	CHECK_FALSE(lifecycle.ActiveSeat().has_value());

	const auto first = lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Seat, 40, 9);
	REQUIRE(first.size() == 1);
	CHECK(first.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindSeat, 40, 9});
	CHECK(lifecycle.ActiveSeat() == 40);
	CHECK(lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Seat, 41, 7).empty());
	CHECK(lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Seat, 41, 7).empty());
	CHECK(lifecycle.SeatCount() == 2);
	CHECK(lifecycle.ActiveSeat() == 40);

	CHECK(lifecycle.RemoveGlobal(41).empty());
	CHECK(lifecycle.SeatCount() == 1);
	CHECK(lifecycle.ActiveSeat() == 40);
}

TEST_CASE("Wayland lifecycle recreates devices after capability changes") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);
	(void)lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Seat, 40, 9);

	const auto added = lifecycle.UpdateSeatCapabilities(40, true, true);
	REQUIRE(added.size() == 2);
	CHECK(added[0].type == Scalpel::WaylandLifecycleActionType::CreatePointer);
	CHECK(added[1].type == Scalpel::WaylandLifecycleActionType::CreateKeyboard);
	CHECK(lifecycle.PointerActive());
	CHECK(lifecycle.KeyboardActive());
	CHECK(lifecycle.UpdateSeatCapabilities(40, true, true).empty());
	CHECK(lifecycle.UpdateSeatCapabilities(999, false, false).empty());

	const auto removed = lifecycle.UpdateSeatCapabilities(40, false, false);
	REQUIRE(removed.size() == 2);
	CHECK(removed[0].type == Scalpel::WaylandLifecycleActionType::ReleasePointer);
	CHECK(removed[1].type == Scalpel::WaylandLifecycleActionType::ReleaseKeyboard);
	CHECK_FALSE(lifecycle.PointerActive());
	CHECK_FALSE(lifecycle.KeyboardActive());

	const auto regained = lifecycle.UpdateSeatCapabilities(40, true, true);
	REQUIRE(regained.size() == 2);
	CHECK(regained[0].type == Scalpel::WaylandLifecycleActionType::CreatePointer);
	CHECK(regained[1].type == Scalpel::WaylandLifecycleActionType::CreateKeyboard);
}

TEST_CASE("Wayland lifecycle promotes a fresh seat after active removal") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);
	(void)lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Seat, 40, 9);
	(void)lifecycle.AddGlobal(Scalpel::WaylandGlobalKind::Seat, 41, 7);
	(void)lifecycle.UpdateSeatCapabilities(40, true, true);

	const auto actions = lifecycle.RemoveGlobal(40);
	REQUIRE(actions.size() == 4);
	CHECK(actions[0].type == Scalpel::WaylandLifecycleActionType::ReleasePointer);
	CHECK(actions[1].type == Scalpel::WaylandLifecycleActionType::ReleaseKeyboard);
	CHECK(actions[2].type == Scalpel::WaylandLifecycleActionType::ReleaseSeat);
	CHECK(actions[3] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindSeat, 41, 7});
	CHECK(lifecycle.ActiveSeat() == 41);
	CHECK_FALSE(lifecycle.PointerActive());
	CHECK_FALSE(lifecycle.KeyboardActive());

	const auto last = lifecycle.RemoveGlobal(41);
	REQUIRE(last.size() == 1);
	CHECK(last.front().type == Scalpel::WaylandLifecycleActionType::ReleaseSeat);
	CHECK_FALSE(lifecycle.ActiveSeat().has_value());
	CHECK(lifecycle.SeatCount() == 0);

	const auto replacement = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Seat, 40, 5);
	REQUIRE(replacement.size() == 1);
	CHECK(replacement.front().type == Scalpel::WaylandLifecycleActionType::BindSeat);
}
