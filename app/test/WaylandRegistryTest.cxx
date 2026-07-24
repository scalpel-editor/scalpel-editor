#include "WaylandLifecycleTest.h"

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

TEST_CASE("Wayland registry replaces the optional data device manager") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	const auto first = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::DataDeviceManager, 29, 3);
	REQUIRE(first.size() == 1);
	CHECK(first.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindDataDeviceManager, 29, 3});
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::DataDeviceManager, 30, 2).empty());

	const auto replacement = lifecycle.RemoveGlobal(29);
	REQUIRE(replacement.size() == 2);
	CHECK(replacement[0] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::ReleaseDataDeviceManager, 29});
	CHECK(replacement[1] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindDataDeviceManager, 30, 2});
	const auto removed = lifecycle.RemoveGlobal(30);
	REQUIRE(removed.size() == 1);
	CHECK(removed.front().type ==
		Scalpel::WaylandLifecycleActionType::ReleaseDataDeviceManager);
}

TEST_CASE("Wayland registry replaces the optional primary selection manager") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	const auto first = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::PrimarySelectionManager, 31, 1);
	REQUIRE(first.size() == 1);
	CHECK(first.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindPrimarySelectionManager, 31, 1});
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::PrimarySelectionManager, 32, 1).empty());

	const auto replacement = lifecycle.RemoveGlobal(31);
	REQUIRE(replacement.size() == 2);
	CHECK(replacement[0] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::ReleasePrimarySelectionManager, 31});
	CHECK(replacement[1] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindPrimarySelectionManager, 32, 1});
	const auto removed = lifecycle.RemoveGlobal(32);
	REQUIRE(removed.size() == 1);
	CHECK(removed.front().type ==
		Scalpel::WaylandLifecycleActionType::ReleasePrimarySelectionManager);
}

TEST_CASE("Wayland registry replaces the optional text input manager") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	const auto first = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::TextInputManager, 33, 1);
	REQUIRE(first.size() == 1);
	CHECK(first.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindTextInputManager, 33, 1});
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::TextInputManager, 34, 1).empty());

	const auto replacement = lifecycle.RemoveGlobal(33);
	REQUIRE(replacement.size() == 2);
	CHECK(replacement[0] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::ReleaseTextInputManager, 33});
	CHECK(replacement[1] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindTextInputManager, 34, 1});
	const auto removed = lifecycle.RemoveGlobal(34);
	REQUIRE(removed.size() == 1);
	CHECK(removed.front().type ==
		Scalpel::WaylandLifecycleActionType::ReleaseTextInputManager);
}

TEST_CASE("Wayland registry treats presentation timing as optional") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	CHECK(lifecycle.RemoveGlobal(35).empty());
	const auto first = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Presentation, 35, 2);
	REQUIRE(first.size() == 1);
	CHECK(first.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindPresentation, 35, 2});
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Presentation, 36, 1).empty());

	const auto replacement = lifecycle.RemoveGlobal(35);
	REQUIRE(replacement.size() == 2);
	CHECK(replacement[0] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::ReleasePresentation, 35});
	CHECK(replacement[1] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindPresentation, 36, 1});
	const auto removed = lifecycle.RemoveGlobal(36);
	REQUIRE(removed.size() == 1);
	CHECK(removed.front().type ==
		Scalpel::WaylandLifecycleActionType::ReleasePresentation);

	const auto fresh = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Presentation, 37, 2);
	REQUIRE(fresh.size() == 1);
	CHECK(fresh.front().type ==
		Scalpel::WaylandLifecycleActionType::BindPresentation);
}

TEST_CASE("Wayland registry replaces optional scale managers") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);

	REQUIRE(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Viewporter, 40, 1).front() ==
		Scalpel::WaylandLifecycleAction{
			Scalpel::WaylandLifecycleActionType::BindViewporter, 40, 1});
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Viewporter, 41, 1).empty());
	const auto viewporterReplacement = lifecycle.RemoveGlobal(40);
	REQUIRE(viewporterReplacement.size() == 2);
	CHECK(viewporterReplacement[0].type ==
		Scalpel::WaylandLifecycleActionType::ReleaseViewporter);
	CHECK(viewporterReplacement[1] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindViewporter, 41, 1});

	REQUIRE(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::FractionalScaleManager, 42, 1).front() ==
		Scalpel::WaylandLifecycleAction{
			Scalpel::WaylandLifecycleActionType::BindFractionalScaleManager,
			42, 1});
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::FractionalScaleManager, 43, 1).empty());
	const auto fractionalReplacement = lifecycle.RemoveGlobal(42);
	REQUIRE(fractionalReplacement.size() == 2);
	CHECK(fractionalReplacement[0].type ==
		Scalpel::WaylandLifecycleActionType::ReleaseFractionalScaleManager);
	CHECK(fractionalReplacement[1] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindFractionalScaleManager,
		43, 1});
}

TEST_CASE("Wayland registry replaces the optional portal exporter") {
	Scalpel::WaylandLifecycle lifecycle(800, 600);
	const auto first = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Exporter, 44, 2);
	REQUIRE(first.size() == 1);
	CHECK(first.front() == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindExporter, 44, 2});
	CHECK(lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Exporter, 45, 1).empty());

	const auto replacement = lifecycle.RemoveGlobal(44);
	REQUIRE(replacement.size() == 2);
	CHECK(replacement[0] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::ReleaseExporter, 44});
	CHECK(replacement[1] == Scalpel::WaylandLifecycleAction{
		Scalpel::WaylandLifecycleActionType::BindExporter, 45, 1});

	const auto removed = lifecycle.RemoveGlobal(45);
	REQUIRE(removed.size() == 1);
	CHECK(removed.front().type ==
		Scalpel::WaylandLifecycleActionType::ReleaseExporter);
	const auto fresh = lifecycle.AddGlobal(
		Scalpel::WaylandGlobalKind::Exporter, 46, 1);
	REQUIRE(fresh.size() == 1);
	CHECK(fresh.front().type ==
		Scalpel::WaylandLifecycleActionType::BindExporter);
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
	CHECK(lifecycle.ForceCloseRequested());
	lifecycle.ClearCloseRequest();
	CHECK(lifecycle.CloseRequested());
	CHECK(lifecycle.ForceCloseRequested());
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
