// Testable xdg_popup lifecycle state for the application context menu.
// Records configure pairing, paint readiness, popup_done, and reverse-order
// teardown decisions without requiring a live compositor. WaylandWindow owns
// the real wl_surface / xdg_surface / xdg_popup objects and drives this model
// from protocol callbacks; ApplicationUi and main only observe readiness and
// done through the window owner.

#ifndef WAYLANDPOPUP_H
#define WAYLANDPOPUP_H

#include <cstdint>
#include <optional>

namespace Scalpel {

/** Phase of a context-menu popup from create through destroy. */
enum class WaylandPopupPhase {
	/** No popup exists. */
	Idle,
	/** Role created; waiting for the first configure pair. */
	AwaitingConfigure,
	/** Configured and acknowledged; a buffer may be attached and painted. */
	Ready,
	/** Compositor dismissed via popup_done; client must destroy. */
	Done,
	/** Client teardown finished (idempotent). */
	Destroyed,
};

/**
 * One configure sequence for the popup: geometry from xdg_popup.configure
 * paired with the serial from the following xdg_surface.configure.
 */
struct WaylandPopupConfigure {
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
	uint32_t surfaceSerial = 0;
	bool hasPopupGeometry = false;
	bool hasSurfaceSerial = false;
	bool acknowledged = false;
};

/**
 * Pure lifecycle model for one grabbed context-menu popup.
 * Rejects paint until a configure pair is acknowledged. Teardown is
 * idempotent and reports the reverse ownership order the shell must follow
 * when live objects exist: frame callbacks and EGL objects first, then popup
 * role, xdg surface, viewport, and wl surface.
 */
class WaylandPopupLifecycle final {
public:
	[[nodiscard]] WaylandPopupPhase Phase() const noexcept { return phase; }
	[[nodiscard]] bool CanPaint() const noexcept {
		return phase == WaylandPopupPhase::Ready;
	}
	[[nodiscard]] bool NeedsDestroy() const noexcept {
		return phase == WaylandPopupPhase::Done;
	}
	[[nodiscard]] const WaylandPopupConfigure &Configure() const noexcept {
		return configure;
	}

	/** Begin a new popup; resets prior configure state. */
	void Begin() noexcept;
	/** Record xdg_popup.configure geometry (before the surface configure). */
	void RecordPopupConfigure(int x, int y, int width, int height) noexcept;
	/**
	 * Record xdg_surface.configure. When geometry was already received,
	 * marks the pair ready to acknowledge.
	 */
	void RecordSurfaceConfigure(uint32_t serial) noexcept;
	/**
	 * Acknowledge the latest configure pair. Returns the serial to pass to
	 * xdg_surface.ack_configure, or nullopt when nothing is pending.
	 */
	[[nodiscard]] std::optional<uint32_t> TakeAckSerial() noexcept;
	/** Compositor dismissed the popup; client must destroy. */
	void RecordPopupDone() noexcept;
	/**
	 * Mark teardown complete. Safe to call more than once. Returns true the
	 * first time teardown is applied from a non-idle phase.
	 */
	bool FinishDestroy() noexcept;

	/**
	 * Ordered teardown steps for live objects (tests assert this order).
	 * Index 0 is destroyed first.
	 */
	enum class TeardownStep {
		FrameCallback,
		EglSurface,
		EglWindow,
		PopupRole,
		XdgSurface,
		Viewport,
		WlSurface,
	};
	static constexpr TeardownStep kTeardownOrder[] = {
		TeardownStep::FrameCallback,
		TeardownStep::EglSurface,
		TeardownStep::EglWindow,
		TeardownStep::PopupRole,
		TeardownStep::XdgSurface,
		TeardownStep::Viewport,
		TeardownStep::WlSurface,
	};
	static constexpr std::size_t kTeardownStepCount =
		sizeof(kTeardownOrder) / sizeof(kTeardownOrder[0]);

private:
	WaylandPopupPhase phase = WaylandPopupPhase::Idle;
	WaylandPopupConfigure configure;
};

}

#endif
