#include "WaylandLifecycleTest.h"

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
	CHECK_FALSE(lifecycle.ForceCloseRequested());
	lifecycle.RequestClose();
	CHECK(lifecycle.CloseRequested());
	CHECK_FALSE(lifecycle.ForceCloseRequested());
}

TEST_CASE("Wayland lifecycle clears a user close request") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	lifecycle.RequestClose();
	CHECK(lifecycle.CloseRequested());
	lifecycle.ClearCloseRequest();
	CHECK_FALSE(lifecycle.CloseRequested());
	CHECK_FALSE(lifecycle.ForceCloseRequested());
}

TEST_CASE("Wayland lifecycle accepts globals during clearable user close") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	lifecycle.RequestClose();
	CHECK(lifecycle.CloseRequested());
	CHECK_FALSE(lifecycle.ForceCloseRequested());
	const auto seat = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Seat, 40, 9);
	REQUIRE(seat.size() == 1);
	CHECK(seat.front().type == Scalpel::WaylandLifecycleActionType::BindSeat);
	CHECK(lifecycle.SeatCount() == 1);

	lifecycle.ClearCloseRequest();
	CHECK_FALSE(lifecycle.CloseRequested());
	// Announcement during the prompt window remains after Cancel.
	CHECK(lifecycle.SeatCount() == 1);
	const auto output = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Output, 30, 4);
	REQUIRE(output.size() == 1);
	CHECK(lifecycle.OutputCount() == 1);
}

TEST_CASE("Wayland lifecycle force close is not cleared") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	lifecycle.RequestForceClose();
	CHECK(lifecycle.CloseRequested());
	CHECK(lifecycle.ForceCloseRequested());
	lifecycle.ClearCloseRequest();
	CHECK(lifecycle.CloseRequested());
	CHECK(lifecycle.ForceCloseRequested());
}

TEST_CASE("Wayland lifecycle force close coexists with user close") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	lifecycle.RequestClose();
	lifecycle.RequestForceClose();
	CHECK(lifecycle.CloseRequested());
	CHECK(lifecycle.ForceCloseRequested());
	lifecycle.ClearCloseRequest();
	CHECK(lifecycle.CloseRequested());
	CHECK(lifecycle.ForceCloseRequested());
}
