// Deterministic state and name choices for Wayland cursor themes.

#include "WaylandCursor.h"

#include <climits>
#include <stdexcept>

namespace Scalpel {

using Cursor = Scintilla::Internal::Window::Cursor;

WaylandCursorNames CursorNames(Cursor cursor) noexcept {
	using namespace std::literals;
	switch (cursor) {
	case Cursor::text:
		return {{{"text"sv, "xterm"sv, "ibeam"sv, "default"sv, "left_ptr"sv, "arrow"sv}}, 6};
	case Cursor::arrow:
	case Cursor::invalid:
		return {{{"default"sv, "left_ptr"sv, "arrow"sv}}, 3};
	case Cursor::up:
		return {{{"n-resize"sv, "top_side"sv, "sb_up_arrow"sv,
			"default"sv, "left_ptr"sv, "arrow"sv}}, 6};
	case Cursor::wait:
		return {{{"wait"sv, "watch"sv, "progress"sv,
			"default"sv, "left_ptr"sv, "arrow"sv}}, 6};
	case Cursor::horizontal:
		return {{{"ew-resize"sv, "size_hor"sv, "sb_h_double_arrow"sv,
			"default"sv, "left_ptr"sv, "arrow"sv}}, 6};
	case Cursor::vertical:
		return {{{"ns-resize"sv, "size_ver"sv, "sb_v_double_arrow"sv,
			"default"sv, "left_ptr"sv, "arrow"sv}}, 6};
	case Cursor::reverseArrow:
		return {{{"right_ptr"sv, "default"sv, "left_ptr"sv, "arrow"sv}}, 4};
	case Cursor::hand:
		return {{{"pointer"sv, "hand2"sv, "pointing_hand"sv,
			"default"sv, "left_ptr"sv, "arrow"sv}}, 6};
	}
	return {{{"default"sv, "left_ptr"sv, "arrow"sv}}, 3};
}

std::optional<WaylandCursorAction> WaylandCursorState::Request(Cursor cursor) noexcept {
	if (requested == cursor) {
		return std::nullopt;
	}
	requested = cursor;
	return ApplyIfReady();
}

std::optional<WaylandCursorAction> WaylandCursorState::Enter(uint32_t serial) noexcept {
	pointerSerial = serial;
	return ApplyIfReady();
}

void WaylandCursorState::Leave() noexcept {
	pointerSerial.reset();
}

void WaylandCursorState::ResetPointer() noexcept {
	pointerSerial.reset();
}

std::optional<WaylandCursorAction> WaylandCursorState::SetThemeAvailable(
	bool available) noexcept {
	if (themeAvailable == available) {
		return std::nullopt;
	}
	themeAvailable = available;
	return ApplyIfReady();
}

std::optional<WaylandCursorAction> WaylandCursorState::SetScale(int scale_) {
	if (scale_ <= 0) {
		throw std::invalid_argument("Wayland cursor scale must be positive");
	}
	if (scale == scale_) {
		return std::nullopt;
	}
	scale = scale_;
	return ApplyIfReady();
}

std::optional<WaylandCursorAction> WaylandCursorState::ApplyIfReady() const noexcept {
	if (!pointerSerial || !themeAvailable) {
		return std::nullopt;
	}
	return WaylandCursorAction{requested, *pointerSerial, scale};
}

int CursorThemePixelSize(int logicalSize, int scale) {
	if (logicalSize <= 0 || scale <= 0 || logicalSize > INT_MAX / scale) {
		throw std::invalid_argument("Wayland cursor size and scale must be positive");
	}
	return logicalSize * scale;
}

WaylandCursorImageGeometry CursorImageGeometry(uint32_t width, uint32_t height,
	uint32_t hotspotX, uint32_t hotspotY, int scale) {
	if (scale <= 0) {
		throw std::invalid_argument("Wayland cursor scale must be positive");
	}
	return {width, height, static_cast<int32_t>(hotspotX / scale),
		static_cast<int32_t>(hotspotY / scale)};
}

}
