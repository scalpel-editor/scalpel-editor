// Fixed platform helpers for the standalone application.

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "ApplicationPlatform.h"
#include "Debugging.h"
#include "Platform.h"

namespace Scintilla::Internal {

namespace {

Scalpel::ApplicationWindow *WindowData(WindowID wid) noexcept {
	return static_cast<Scalpel::ApplicationWindow *>(wid);
}

const Scalpel::ApplicationWindow *WindowDataConst(WindowID wid) noexcept {
	return static_cast<const Scalpel::ApplicationWindow *>(wid);
}

}

Window::~Window() noexcept = default;

void Window::Destroy() noexcept {
	if (Scalpel::ApplicationWindow *window = WindowData(wid)) {
		window->visible = false;
	}
	wid = nullptr;
}

PRectangle Window::GetPosition() const {
	const Scalpel::ApplicationWindow *window = WindowDataConst(wid);
	return window ? window->rectangle : PRectangle();
}

void Window::SetPosition(PRectangle rc) {
	if (Scalpel::ApplicationWindow *window = WindowData(wid)) {
		window->rectangle = rc;
	}
}

void Window::SetPositionRelative(PRectangle rc, const Window *) {
	SetPosition(rc);
}

PRectangle Window::GetClientPosition() const {
	const Scalpel::ApplicationWindow *window = WindowDataConst(wid);
	return window ? PRectangle(0, 0, window->rectangle.Width(), window->rectangle.Height()) : PRectangle();
}

void Window::Show(bool show) {
	if (Scalpel::ApplicationWindow *window = WindowData(wid)) {
		window->visible = show;
	}
}

void Window::InvalidateAll() {
	if (Scalpel::ApplicationWindow *window = WindowData(wid)) {
		window->invalidateAllCount++;
		window->invalidatedRectangles.push_back(GetClientPosition());
	}
}

void Window::InvalidateRectangle(PRectangle rc) {
	if (Scalpel::ApplicationWindow *window = WindowData(wid)) {
		window->invalidatedRectangles.push_back(rc);
	}
}

void Window::SetCursor(Cursor cursor) {
	cursorLast = cursor;
	if (Scalpel::ApplicationWindow *window = WindowData(wid)) {
		window->cursor = cursor;
	}
}

PRectangle Window::GetMonitorRect(Point) {
	return GetClientPosition();
}

ColourRGBA Platform::Chrome() {
	return ColourRGBA(0xe0, 0xe0, 0xe0);
}

ColourRGBA Platform::ChromeHighlight() {
	return ColourRGBA(0xff, 0xff, 0xff);
}

const char *Platform::DefaultFont() {
	return "sans";
}

int Platform::DefaultFontSize() {
	return 16;
}

unsigned int Platform::DoubleClickTime() {
	return 500;
}

void Platform::DebugDisplay(const char *text) noexcept {
	std::fputs(text ? text : "", stderr);
}

void Platform::DebugPrintf(const char *format, ...) noexcept {
	va_list arguments;
	va_start(arguments, format);
	std::vfprintf(stderr, format, arguments);
	va_end(arguments);
}

bool Platform::ShowAssertionPopUps(bool) noexcept {
	return false;
}

void Platform::Assert(const char *condition, const char *file, int line) noexcept {
	std::fprintf(stderr, "Assertion [%s] failed at %s %d\n", condition, file, line);
	std::abort();
}

}
