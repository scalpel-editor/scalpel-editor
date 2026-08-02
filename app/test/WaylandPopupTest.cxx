#include "catch.hpp"

#include <cstddef>

#include "WaylandPopup.h"

using Scalpel::WaylandPopupLifecycle;
using Scalpel::WaylandPopupPhase;

TEST_CASE("Wayland popup lifecycle configure ack and paint readiness") {
	WaylandPopupLifecycle life;
	CHECK(life.Phase() == WaylandPopupPhase::Idle);
	CHECK_FALSE(life.CanPaint());
	CHECK_FALSE(life.TakeAckSerial().has_value());

	life.Begin();
	CHECK(life.Phase() == WaylandPopupPhase::AwaitingConfigure);
	CHECK_FALSE(life.CanPaint());

	// Surface configure alone is not enough.
	life.RecordSurfaceConfigure(11);
	CHECK_FALSE(life.TakeAckSerial().has_value());
	CHECK_FALSE(life.CanPaint());

	// Geometry alone is not enough.
	WaylandPopupLifecycle geometryFirst;
	geometryFirst.Begin();
	geometryFirst.RecordPopupConfigure(4, 8, 240, 170);
	CHECK_FALSE(geometryFirst.TakeAckSerial().has_value());

	// Complete pair: geometry then surface serial.
	life.RecordPopupConfigure(10, 20, 240, 170);
	const auto serial = life.TakeAckSerial();
	REQUIRE(serial.has_value());
	CHECK(*serial == 11);
	CHECK(life.CanPaint());
	CHECK(life.Phase() == WaylandPopupPhase::Ready);
	CHECK(life.Configure().x == 10);
	CHECK(life.Configure().y == 20);
	CHECK(life.Configure().width == 240);
	CHECK(life.Configure().height == 170);
	// Second take is empty until a new configure pair arrives.
	CHECK_FALSE(life.TakeAckSerial().has_value());
}

TEST_CASE("Wayland popup rejects zero size and paints only after ack") {
	WaylandPopupLifecycle life;
	life.Begin();
	life.RecordPopupConfigure(0, 0, 0, 40);
	life.RecordSurfaceConfigure(3);
	CHECK_FALSE(life.TakeAckSerial().has_value());
	CHECK_FALSE(life.CanPaint());

	life.RecordPopupConfigure(0, 0, 100, 40);
	life.RecordSurfaceConfigure(4);
	REQUIRE(life.TakeAckSerial().has_value());
	CHECK(life.CanPaint());
}

TEST_CASE("Wayland popup done and idempotent destroy") {
	WaylandPopupLifecycle life;
	life.Begin();
	life.RecordPopupConfigure(0, 0, 100, 50);
	life.RecordSurfaceConfigure(1);
	REQUIRE(life.TakeAckSerial().has_value());
	CHECK(life.CanPaint());

	life.RecordPopupDone();
	CHECK(life.Phase() == WaylandPopupPhase::Done);
	CHECK(life.NeedsDestroy());
	CHECK_FALSE(life.CanPaint());

	CHECK(life.FinishDestroy());
	CHECK(life.Phase() == WaylandPopupPhase::Idle);
	CHECK_FALSE(life.FinishDestroy());
	// Done after idle is ignored.
	life.RecordPopupDone();
	CHECK(life.Phase() == WaylandPopupPhase::Idle);
}

TEST_CASE("Wayland popup teardown order is reverse ownership") {
	// Frame/EGL before role before surfaces — matches Create reverse.
	REQUIRE(WaylandPopupLifecycle::kTeardownStepCount == 7);
	const auto *order = WaylandPopupLifecycle::kTeardownOrder;
	CHECK(static_cast<int>(order[0]) == 0); // FrameCallback
	CHECK(static_cast<int>(order[1]) == 1); // EglSurface
	CHECK(static_cast<int>(order[2]) == 2); // EglWindow
	CHECK(static_cast<int>(order[3]) == 3); // PopupRole
	CHECK(static_cast<int>(order[4]) == 4); // XdgSurface
	CHECK(static_cast<int>(order[5]) == 5); // Viewport
	CHECK(static_cast<int>(order[6]) == 6); // WlSurface
	// Strictly increasing so reverse ownership stays ordered.
	for (std::size_t i = 1; i < WaylandPopupLifecycle::kTeardownStepCount; ++i) {
		CHECK(static_cast<int>(order[i]) > static_cast<int>(order[i - 1]));
	}
}
