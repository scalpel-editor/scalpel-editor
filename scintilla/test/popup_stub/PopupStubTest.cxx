// scalpel-editor test code
/** @file PopupStubTest.cxx
 ** Focused tests for production ListBox and Menu stubs (PlatformPopups.cxx).
 **
 ** editorTest links TestPlatform's inspectable list box instead, so regressions
 ** in the production stubs (Created stays false, Cancel clears Active, list
 ** parsing) need this separate binary.
 **/

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "EditorBasicTypes.h"
#include "EditorDocumentTypes.h"
#include "EditorInputTypes.h"
#include "EditorLayoutTypes.h"
#include "EditorStyleTypes.h"

#include "Debugging.h"
#include "Geometry.h"
#include "Platform.h"
#include "CharacterType.h"
#include "Position.h"
#include "AutoComplete.h"

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

using namespace Scintilla;
using namespace Scintilla::Internal;

// Minimal Window method definitions so ListBox (a Window) links without the
// application or editor test platforms.
namespace Scintilla::Internal {

Window::~Window() noexcept = default;

void Window::Destroy() noexcept {
	wid = nullptr;
}

PRectangle Window::GetPosition() const {
	return PRectangle();
}

void Window::SetPosition(PRectangle) {
}

void Window::SetPositionRelative(PRectangle, const Window *) {
}

PRectangle Window::GetClientPosition() const {
	return PRectangle();
}

void Window::Show(bool) {
}

void Window::InvalidateAll() {
}

void Window::InvalidateRectangle(PRectangle) {
}

void Window::SetCursor(Cursor) {
}

PRectangle Window::GetMonitorRect(Point) {
	return PRectangle();
}

ColourRGBA Platform::Chrome() {
	return ColourRGBA(0xe0, 0xe0, 0xe0);
}

ColourRGBA Platform::ChromeHighlight() {
	return ColourRGBA(0xff, 0xff, 0xff);
}

const char *Platform::DefaultFont() {
	return "fixture";
}

int Platform::DefaultFontSize() {
	return 10;
}

unsigned int Platform::DoubleClickTime() {
	return 500;
}

void Platform::DebugDisplay(const char *s) noexcept {
	std::fputs(s, stderr);
}

void Platform::DebugPrintf(const char *format, ...) noexcept {
	char buffer[2000];
	va_list args;
	va_start(args, format);
	std::vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	std::fputs(buffer, stderr);
}

bool Platform::ShowAssertionPopUps(bool) noexcept {
	return false;
}

void Platform::Assert(const char *c, const char *file, int line) noexcept {
	std::fprintf(stderr, "Assertion [%s] failed at %s %d\n", c, file, line);
	std::abort();
}

}

TEST_CASE("Production ListBox Create leaves no window and keeps list storage") {
	std::unique_ptr<ListBox> lb = ListBox::Allocate();
	REQUIRE(lb != nullptr);

	Window parent;
	lb->Create(parent, 1, Point(0, 0), 12);
	CHECK_FALSE(lb->Created());

	lb->SetList("alpha beta gamma", ' ', '?');
	CHECK(lb->Length() == 3);
	CHECK(lb->GetValue(0) == "alpha");
	CHECK(lb->GetValue(1) == "beta");
	CHECK(lb->GetValue(2) == "gamma");
	CHECK(lb->Find("ga") == 2);
	CHECK(lb->Find("zz") == -1);

	lb->Select(1);
	CHECK(lb->GetSelection() == 1);
	CHECK(lb->GetValue(1) == "beta");

	lb->Clear();
	CHECK(lb->Length() == 0);
	CHECK(lb->GetSelection() == -1);
	CHECK_FALSE(lb->Created());
}

TEST_CASE("Production ListBox SetList splits typesep without appending digits") {
	std::unique_ptr<ListBox> lb = ListBox::Allocate();
	Window parent;
	lb->Create(parent, 1, Point(0, 0), 12);

	lb->SetList("alpha?1 beta?2", ' ', '?');
	REQUIRE(lb->Length() == 2);
	CHECK(lb->GetValue(0) == "alpha");
	CHECK(lb->GetValue(1) == "beta");
}

TEST_CASE("Production Menu CreatePopUp does not report a menu id") {
	Menu menu;
	menu.CreatePopUp();
	CHECK(menu.GetID() == nullptr);
	menu.Show(Point(1, 2), Window());
	menu.Destroy();
	CHECK(menu.GetID() == nullptr);
}

TEST_CASE("AutoComplete Cancel clears Active when production stub never Created a window") {
	AutoComplete ac;
	Window parent;
	ac.Start(parent, 1, 0, Point(0, 0), 0, 12, ListOptions{});
	REQUIRE(ac.Active());
	REQUIRE(ac.lb != nullptr);
	CHECK_FALSE(ac.lb->Created());

	ac.SetList("one two three");
	CHECK(ac.lb->Length() == 3);
	CHECK(ac.GetSelection() == -1);

	ac.Cancel();
	CHECK_FALSE(ac.Active());
	CHECK(ac.lb->Length() == 0);
	CHECK_FALSE(ac.lb->Created());

	// Second show/cancel cycle must not leave Active stuck.
	ac.Start(parent, 1, 0, Point(0, 0), 0, 12, ListOptions{});
	REQUIRE(ac.Active());
	ac.Cancel();
	CHECK_FALSE(ac.Active());
}
