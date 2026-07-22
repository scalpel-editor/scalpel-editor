// Compositor-driven state for the main Wayland window.

#ifndef WAYLANDLIFECYCLE_H
#define WAYLANDLIFECYCLE_H

#include <optional>

namespace Scalpel {

struct WindowSize {
	int width = 0;
	int height = 0;

	friend constexpr bool operator==(const WindowSize &left, const WindowSize &right) noexcept {
		return left.width == right.width && left.height == right.height;
	}
	friend constexpr bool operator!=(const WindowSize &left, const WindowSize &right) noexcept {
		return !(left == right);
	}
};

/** Coalesces protocol callbacks into changes consumed by the application loop. */
class WaylandLifecycle final {
public:
	WaylandLifecycle(int width, int height);

	void ProposeSize(int width, int height) noexcept;
	[[nodiscard]] std::optional<WindowSize> CommitConfigure() noexcept;
	void RequestClose() noexcept;

	[[nodiscard]] std::optional<WindowSize> TakeResize() noexcept;
	[[nodiscard]] int Width() const noexcept { return currentSize.width; }
	[[nodiscard]] int Height() const noexcept { return currentSize.height; }
	[[nodiscard]] bool CloseRequested() const noexcept { return closeRequested; }

private:
	WindowSize currentSize;
	std::optional<WindowSize> proposedSize;
	std::optional<WindowSize> pendingResize;
	bool closeRequested = false;
};

}

#endif
