// Deterministic state and name choices for Wayland cursor themes.

#include "WaylandCursor.h"

#include <climits>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace Scalpel {

using Cursor = Scintilla::Internal::Window::Cursor;

namespace {

std::string EnvironmentValue(const char *name) {
	if (const char *value = std::getenv(name)) {
		return value;
	}
	return {};
}

std::string Trim(std::string value) {
	const std::size_t first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		return {};
	}
	const std::size_t last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

std::optional<int> ParseCursorSize(const std::string &value) {
	const std::string trimmed = Trim(value);
	if (trimmed.empty()) {
		return std::nullopt;
	}
	char *end = nullptr;
	const long parsed = std::strtol(trimmed.c_str(), &end, 10);
	if (!end || *end != '\0' || parsed <= 0 || parsed > 1024) {
		return std::nullopt;
	}
	return static_cast<int>(parsed);
}

bool IsKdeSession() {
	const std::string desktop = EnvironmentValue("XDG_CURRENT_DESKTOP") + ":" +
		EnvironmentValue("XDG_SESSION_DESKTOP");
	return desktop.find("KDE") != std::string::npos ||
		desktop.find("Plasma") != std::string::npos ||
		!EnvironmentValue("KDE_FULL_SESSION").empty();
}

void ReadKdeCursorConfig(const std::string &path, WaylandCursorSettings &settings,
	bool &hasSize) {
	std::ifstream stream(path);
	if (!stream) {
		return;
	}
	bool mouseGroup = false;
	for (std::string line; std::getline(stream, line);) {
		line = Trim(std::move(line));
		if (line.empty() || line[0] == '#' || line[0] == ';') {
			continue;
		}
		if (line.front() == '[' && line.back() == ']') {
			mouseGroup = line.substr(1, line.size() - 2) == "Mouse";
			continue;
		}
		if (!mouseGroup) {
			continue;
		}
		const std::size_t equals = line.find('=');
		if (equals == std::string::npos) {
			continue;
		}
		const std::string key = Trim(line.substr(0, equals));
		const std::string value = Trim(line.substr(equals + 1));
		if (settings.themeName.empty() && key == "cursorTheme") {
			settings.themeName = value;
		} else if (!hasSize && (key == "cursorSize" || key == "CursorSize")) {
			if (const std::optional<int> size = ParseCursorSize(value)) {
				settings.logicalSize = *size;
				hasSize = true;
			}
		}
	}
}

}

WaylandCursorNames CursorNames(Cursor cursor) noexcept {
	using namespace std::literals;
	switch (cursor) {
	case Cursor::text:
		return {{{"text"sv, "xterm"sv, "ibeam"sv, "default"sv, "left_ptr"sv, "arrow"sv}}, 6};
	case Cursor::arrow:
	case Cursor::invalid:
		return {{{"default"sv, "left_ptr"sv, "arrow"sv}}, 3};
	case Cursor::up:
		return {{{"sb_up_arrow"sv, "up-arrow"sv, "up_arrow"sv,
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

WaylandCursorSettings ResolveCursorSettings() {
	WaylandCursorSettings settings;
	settings.themeName = Trim(EnvironmentValue("XCURSOR_THEME"));
	bool hasSize = false;
	if (const std::optional<int> size = ParseCursorSize(EnvironmentValue("XCURSOR_SIZE"))) {
		settings.logicalSize = *size;
		hasSize = true;
	}
	if (IsKdeSession()) {
		std::string configHome = EnvironmentValue("XDG_CONFIG_HOME");
		if (configHome.empty()) {
			const std::string home = EnvironmentValue("HOME");
			if (!home.empty()) {
				configHome = home + "/.config";
			}
		}
		if (!configHome.empty()) {
			ReadKdeCursorConfig(configHome + "/kcminputrc", settings, hasSize);
			ReadKdeCursorConfig(configHome + "/kdedefaults/kcminputrc", settings, hasSize);
		}
	}
	return settings;
}

std::optional<WaylandCursorAction> WaylandCursorState::Request(Cursor cursor) noexcept {
	if (requested == cursor) {
		return std::nullopt;
	}
	requested = cursor;
	return ApplyIfReady();
}

std::optional<WaylandCursorAction> WaylandCursorState::Enter(uint32_t serial) noexcept {
	enteredOverride.reset();
	pointerSerial = serial;
	return ApplyIfReady();
}

std::optional<WaylandCursorAction> WaylandCursorState::EnterContextPopup(
	uint32_t serial) noexcept {
	enteredOverride = Cursor::arrow;
	pointerSerial = serial;
	return ApplyIfReady();
}

void WaylandCursorState::Leave() noexcept {
	enteredOverride.reset();
	pointerSerial.reset();
}

void WaylandCursorState::ResetPointer() noexcept {
	enteredOverride.reset();
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
	return WaylandCursorAction{
		enteredOverride.value_or(requested), *pointerSerial, scale};
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
