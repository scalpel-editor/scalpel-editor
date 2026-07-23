// Deterministic state and name choices for Wayland cursor themes.

#ifndef WAYLANDCURSOR_H
#define WAYLANDCURSOR_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "Platform.h"

namespace Scalpel {

struct WaylandCursorNames {
	std::array<std::string_view, 6> values{};
	std::size_t count = 0;

	[[nodiscard]] constexpr std::string_view operator[](std::size_t index) const noexcept {
		return values[index];
	}
};

[[nodiscard]] WaylandCursorNames CursorNames(
	Scintilla::Internal::Window::Cursor cursor) noexcept;

struct WaylandCursorAction {
	Scintilla::Internal::Window::Cursor cursor =
		Scintilla::Internal::Window::Cursor::invalid;
	uint32_t serial = 0;
	int scale = 1;
};

/** Retains cursor requests until the Wayland pointer can accept them. */
class WaylandCursorState final {
public:
	[[nodiscard]] std::optional<WaylandCursorAction> Request(
		Scintilla::Internal::Window::Cursor cursor) noexcept;
	[[nodiscard]] std::optional<WaylandCursorAction> Enter(uint32_t serial) noexcept;
	void Leave() noexcept;
	void ResetPointer() noexcept;
	[[nodiscard]] std::optional<WaylandCursorAction> SetThemeAvailable(bool available) noexcept;
	[[nodiscard]] std::optional<WaylandCursorAction> SetScale(int scale);

	[[nodiscard]] Scintilla::Internal::Window::Cursor Requested() const noexcept {
		return requested;
	}
	[[nodiscard]] bool Entered() const noexcept { return pointerSerial.has_value(); }
	[[nodiscard]] bool ThemeAvailable() const noexcept { return themeAvailable; }
	[[nodiscard]] int Scale() const noexcept { return scale; }

private:
	[[nodiscard]] std::optional<WaylandCursorAction> ApplyIfReady() const noexcept;

	Scintilla::Internal::Window::Cursor requested =
		Scintilla::Internal::Window::Cursor::invalid;
	std::optional<uint32_t> pointerSerial;
	bool themeAvailable = false;
	int scale = 1;
};

struct WaylandCursorImageGeometry {
	uint32_t bufferWidth = 0;
	uint32_t bufferHeight = 0;
	int32_t hotspotX = 0;
	int32_t hotspotY = 0;

	friend constexpr bool operator==(const WaylandCursorImageGeometry &left,
		const WaylandCursorImageGeometry &right) noexcept {
		return left.bufferWidth == right.bufferWidth &&
			left.bufferHeight == right.bufferHeight &&
			left.hotspotX == right.hotspotX && left.hotspotY == right.hotspotY;
	}
};

[[nodiscard]] int CursorThemePixelSize(int logicalSize, int scale);
[[nodiscard]] WaylandCursorImageGeometry CursorImageGeometry(
	uint32_t width, uint32_t height, uint32_t hotspotX, uint32_t hotspotY, int scale);

}

#endif
