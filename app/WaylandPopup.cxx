#include "WaylandPopup.h"

namespace Scalpel {

void WaylandPopupLifecycle::Begin() noexcept {
	phase = WaylandPopupPhase::AwaitingConfigure;
	configure = {};
}

void WaylandPopupLifecycle::RecordPopupConfigure(int x, int y, int width,
	int height) noexcept {
	if (phase != WaylandPopupPhase::AwaitingConfigure &&
		phase != WaylandPopupPhase::Ready) {
		return;
	}
	configure.x = x;
	configure.y = y;
	configure.width = width;
	configure.height = height;
	configure.hasPopupGeometry = true;
	configure.acknowledged = false;
	// A reposition while Ready returns to awaiting until surface configure.
	if (phase == WaylandPopupPhase::Ready) {
		phase = WaylandPopupPhase::AwaitingConfigure;
	}
}

void WaylandPopupLifecycle::RecordSurfaceConfigure(uint32_t serial) noexcept {
	if (phase != WaylandPopupPhase::AwaitingConfigure &&
		phase != WaylandPopupPhase::Ready) {
		return;
	}
	configure.surfaceSerial = serial;
	configure.hasSurfaceSerial = true;
	configure.acknowledged = false;
}

std::optional<uint32_t> WaylandPopupLifecycle::TakeAckSerial() noexcept {
	if (!configure.hasPopupGeometry || !configure.hasSurfaceSerial ||
		configure.acknowledged) {
		return std::nullopt;
	}
	if (configure.width <= 0 || configure.height <= 0) {
		return std::nullopt;
	}
	configure.acknowledged = true;
	phase = WaylandPopupPhase::Ready;
	return configure.surfaceSerial;
}

void WaylandPopupLifecycle::RecordPopupDone() noexcept {
	if (phase == WaylandPopupPhase::Idle ||
		phase == WaylandPopupPhase::Destroyed) {
		return;
	}
	phase = WaylandPopupPhase::Done;
}

bool WaylandPopupLifecycle::FinishDestroy() noexcept {
	if (phase == WaylandPopupPhase::Idle ||
		phase == WaylandPopupPhase::Destroyed) {
		return false;
	}
	phase = WaylandPopupPhase::Destroyed;
	configure = {};
	// Destroyed is terminal until the next Begin(); treat as idle for callers
	// that only check Idle vs active.
	phase = WaylandPopupPhase::Idle;
	return true;
}

}
