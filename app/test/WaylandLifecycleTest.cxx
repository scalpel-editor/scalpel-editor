#include <optional>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "WaylandLifecycle.h"

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

TEST_CASE("Wayland lifecycle reports focus transitions once") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	lifecycle.RecordKeyboardFocus(true);
	CHECK(lifecycle.TakeKeyboardFocus() == true);
	CHECK_FALSE(lifecycle.TakeKeyboardFocus().has_value());
	lifecycle.RecordKeyboardFocus(true);
	CHECK_FALSE(lifecycle.TakeKeyboardFocus().has_value());
	lifecycle.RecordKeyboardFocus(false);
	CHECK(lifecycle.TakeKeyboardFocus() == false);
}

TEST_CASE("Wayland lifecycle retains a close request") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	CHECK_FALSE(lifecycle.CloseRequested());
	lifecycle.RequestClose();
	CHECK(lifecycle.CloseRequested());
}
