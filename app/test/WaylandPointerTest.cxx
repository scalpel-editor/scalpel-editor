#include "WaylandInputTest.h"

TEST_CASE("Wayland pointer retains coordinates modifiers buttons and axes") {
	const TestKeymap keymap = MakeTestKeymap();
	Scalpel::WaylandInput input;
	REQUIRE(input.SetKeymap(keymap.text));
	input.UpdateModifiers(keymap.shiftMask, 0, 0, 0);

	input.RecordPointerMotion(30, 12.5, 18.25);
	input.RecordPointerButton(31, BTN_LEFT, true);
	input.RecordPointerAxis(32, WL_POINTER_AXIS_VERTICAL_SCROLL, 10.0);
	input.RecordPointerButton(33, BTN_LEFT, false);
	input.RecordPointerLeave();
	const std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 5);
	const auto &motion = std::get<Scalpel::PointerInput>(events[0]);
	CHECK(motion.action == Scalpel::PointerAction::Move);
	CHECK(motion.modifiers == Scintilla::KeyMod::Shift);
	CHECK(motion.x == 12.5);
	CHECK(motion.y == 18.25);
	const auto &press = std::get<Scalpel::PointerInput>(events[1]);
	CHECK(press.action == Scalpel::PointerAction::Press);
	CHECK(press.button == 0);
	CHECK(press.time == 31);
	const auto &scroll = std::get<Scalpel::PointerInput>(events[2]);
	CHECK(scroll.action == Scalpel::PointerAction::Scroll);
	CHECK(scroll.deltaX == 0);
	CHECK(scroll.deltaY == 10.0);
	CHECK(std::get<Scalpel::PointerInput>(events[3]).action ==
		Scalpel::PointerAction::Release);
	CHECK(std::get<Scalpel::PointerInput>(events[4]).action ==
		Scalpel::PointerAction::Leave);
}

TEST_CASE("Wayland pointer popup surface and serial are preserved") {
	Scalpel::WaylandInput input;
	input.SetPointerSurface(Scalpel::PointerSurface::ContextPopup);
	input.RecordPointerMotion(10, 4.0, 6.0);
	input.RecordPointerButton(11, BTN_RIGHT, true, 99);
	input.RecordPointerButton(12, BTN_RIGHT, false, 100);
	const std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 3);
	const auto &motion = std::get<Scalpel::PointerInput>(events[0]);
	CHECK(motion.surface == Scalpel::PointerSurface::ContextPopup);
	CHECK(motion.serial == 0);
	const auto &press = std::get<Scalpel::PointerInput>(events[1]);
	CHECK(press.action == Scalpel::PointerAction::Press);
	CHECK(press.button == 1);
	CHECK(press.serial == 99);
	CHECK(press.surface == Scalpel::PointerSurface::ContextPopup);
	const auto &release = std::get<Scalpel::PointerInput>(events[2]);
	CHECK(release.serial == 100);
	CHECK(release.surface == Scalpel::PointerSurface::ContextPopup);

	// Leave resets the surface target to the toplevel for the next enter.
	input.SetPointerSurface(Scalpel::PointerSurface::ContextPopup);
	input.RecordPointerMotion(13, 1, 1);
	(void)input.TakeInputs();
	input.RecordPointerLeave();
	const auto leave = std::get<Scalpel::PointerInput>(input.TakeInputs().front());
	CHECK(leave.surface == Scalpel::PointerSurface::ContextPopup);
	CHECK(input.CurrentPointerSurface() == Scalpel::PointerSurface::Toplevel);
}

TEST_CASE("Wayland pointer coalesces current axis frames") {
	Scalpel::WaylandInput input;
	input.SetPointerVersion(9);
	input.RecordPointerMotion(30, 12.5, 18.25);
	REQUIRE(input.TakeInputs().size() == 1);

	input.RecordPointerAxisSource(WL_POINTER_AXIS_SOURCE_WHEEL);
	input.RecordPointerAxis(31, WL_POINTER_AXIS_HORIZONTAL_SCROLL, 40.0);
	input.RecordPointerAxisDiscrete(WL_POINTER_AXIS_HORIZONTAL_SCROLL, 2);
	input.RecordPointerAxisValue120(WL_POINTER_AXIS_HORIZONTAL_SCROLL, 120);
	input.RecordPointerAxisRelativeDirection(WL_POINTER_AXIS_HORIZONTAL_SCROLL,
		WL_POINTER_AXIS_RELATIVE_DIRECTION_INVERTED);
	input.RecordPointerAxis(32, WL_POINTER_AXIS_VERTICAL_SCROLL, -30.0);
	input.RecordPointerAxisValue120(WL_POINTER_AXIS_VERTICAL_SCROLL, -60);
	CHECK(input.TakeInputs().empty());
	input.RecordPointerFrame();

	const std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 1);
	const auto &scroll = std::get<Scalpel::PointerInput>(events.front());
	CHECK(scroll.action == Scalpel::PointerAction::Scroll);
	CHECK(scroll.x == 12.5);
	CHECK(scroll.y == 18.25);
	CHECK(scroll.deltaX == 10.0);
	CHECK(scroll.deltaY == -5.0);
	CHECK(scroll.time == 32);
}

TEST_CASE("Wayland pointer uses discrete and continuous frame fallbacks") {
	Scalpel::WaylandInput input;
	input.SetPointerVersion(7);
	input.RecordPointerAxis(40, WL_POINTER_AXIS_HORIZONTAL_SCROLL, 3.0);
	input.RecordPointerAxis(41, WL_POINTER_AXIS_HORIZONTAL_SCROLL, 4.0);
	input.RecordPointerAxisDiscrete(WL_POINTER_AXIS_HORIZONTAL_SCROLL, 2);
	input.RecordPointerAxis(42, WL_POINTER_AXIS_VERTICAL_SCROLL, 8.0);
	input.RecordPointerAxis(42, WL_POINTER_AXIS_VERTICAL_SCROLL, 2.0);
	input.RecordPointerFrame();

	std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 1);
	const auto &scroll = std::get<Scalpel::PointerInput>(events.front());
	CHECK(scroll.deltaX == 20.0);
	CHECK(scroll.deltaY == 10.0);
	CHECK(scroll.time == 42);

	input.RecordPointerAxisStop(43, WL_POINTER_AXIS_VERTICAL_SCROLL);
	input.RecordPointerAxisSource(WL_POINTER_AXIS_SOURCE_FINGER);
	input.RecordPointerFrame();
	CHECK(input.TakeInputs().empty());
	input.RecordPointerAxis(44, WL_POINTER_AXIS_VERTICAL_SCROLL, 6.0);
	input.RecordPointerFrame();
	events = input.TakeInputs();
	REQUIRE(events.size() == 1);
	CHECK(std::get<Scalpel::PointerInput>(events.front()).deltaY == 6.0);
}

TEST_CASE("Wayland pointer keeps legacy axis delivery immediate") {
	Scalpel::WaylandInput input;
	input.SetPointerVersion(4);
	input.RecordPointerAxis(50, WL_POINTER_AXIS_VERTICAL_SCROLL, 10.0);
	REQUIRE(input.TakeInputs().size() == 1);
	input.RecordPointerFrame();
	CHECK(input.TakeInputs().empty());
}

TEST_CASE("Wayland pointer teardown drops an unfinished axis frame") {
	Scalpel::WaylandInput input;
	input.SetPointerVersion(9);
	input.RecordPointerMotion(60, 20, 25);
	REQUIRE(input.TakeInputs().size() == 1);
	input.RecordPointerAxis(61, WL_POINTER_AXIS_VERTICAL_SCROLL, 10.0);
	input.ResetPointerDevice();
	const std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 1);
	CHECK(std::get<Scalpel::PointerInput>(events.front()).action ==
		Scalpel::PointerAction::Leave);
}

TEST_CASE("Wayland pointer teardown removes stale input and reports leave") {
	Scalpel::WaylandInput input;
	input.RecordPointerMotion(30, 12.5, 18.25);
	input.RecordPointerButton(31, BTN_LEFT, true);
	input.RecordPointerButton(32, BTN_RIGHT, true);
	REQUIRE(input.TakeInputs().size() == 3);
	input.RecordPointerAxis(33, WL_POINTER_AXIS_VERTICAL_SCROLL, 10.0);
	input.ResetPointerDevice();
	const std::vector<Scalpel::InputEvent> events = input.TakeInputs();
	REQUIRE(events.size() == 3);
	const auto &leftRelease = std::get<Scalpel::PointerInput>(events[0]);
	CHECK(leftRelease.action == Scalpel::PointerAction::Release);
	CHECK(leftRelease.button == 0);
	CHECK(leftRelease.x == 12.5);
	CHECK(leftRelease.y == 18.25);
	const auto &rightRelease = std::get<Scalpel::PointerInput>(events[1]);
	CHECK(rightRelease.action == Scalpel::PointerAction::Release);
	CHECK(rightRelease.button == 1);
	const auto &leave = std::get<Scalpel::PointerInput>(events[2]);
	CHECK(leave.action == Scalpel::PointerAction::Leave);
	CHECK(leave.x == 12.5);
	CHECK(leave.y == 18.25);

	input.ResetPointerDevice();
	CHECK(input.TakeInputs().empty());

	input.RecordPointerMotion(32, 20, 25);
	input.RecordPointerLeave();
	input.ResetPointerDevice();
	input.ResetPointerDevice();
	const std::vector<Scalpel::InputEvent> pendingLeave = input.TakeInputs();
	REQUIRE(pendingLeave.size() == 1);
	const auto &repeatedLeave = std::get<Scalpel::PointerInput>(pendingLeave[0]);
	CHECK(repeatedLeave.action == Scalpel::PointerAction::Leave);
	CHECK(repeatedLeave.x == 20);
	CHECK(repeatedLeave.y == 25);
}
