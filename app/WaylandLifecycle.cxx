#include "WaylandLifecycle.h"

#include <stdexcept>
#include <utility>

namespace Scalpel {

WaylandLifecycle::WaylandLifecycle(int width, int height) : currentSize{width, height} {
	if (width <= 0 || height <= 0) {
		throw std::invalid_argument("WaylandLifecycle requires a positive size");
	}
}

void WaylandLifecycle::ProposeSize(int width, int height) noexcept {
	if (width > 0 && height > 0) {
		proposedSize = WindowSize{width, height};
	} else {
		proposedSize.reset();
	}
}

std::optional<WindowSize> WaylandLifecycle::CommitConfigure() noexcept {
	std::optional<WindowSize> committed;
	if (proposedSize && *proposedSize != currentSize) {
		currentSize = *proposedSize;
		pendingResize = currentSize;
		committed = currentSize;
	}
	proposedSize.reset();
	return committed;
}

void WaylandLifecycle::RecordKeyboardFocus(bool focused) noexcept {
	if (focused != keyboardFocused) {
		keyboardFocused = focused;
		pendingKeyboardFocus = focused;
	}
}

void WaylandLifecycle::RequestClose() noexcept {
	closeRequested = true;
}

std::optional<WindowSize> WaylandLifecycle::TakeResize() noexcept {
	return std::exchange(pendingResize, std::nullopt);
}

std::optional<bool> WaylandLifecycle::TakeKeyboardFocus() noexcept {
	return std::exchange(pendingKeyboardFocus, std::nullopt);
}

}
