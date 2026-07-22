#include <optional>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "WaylandLifecycle.h"
#include "WaylandWindow.h"

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
