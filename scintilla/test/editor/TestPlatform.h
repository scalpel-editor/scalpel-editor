// scalpel-editor test code
/** @file TestPlatform.h
 ** Deterministic test implementation of the Platform.h contracts.
 **
 ** The platform here never draws pixels, opens windows, or talks to a
 ** display server. Windows store their state in memory, surfaces record
 ** drawing commands as text, fonts report fixed metrics, and features the
 ** tests do not exercise (list boxes, menus, bidirectional layout) record
 ** the request instead of pretending it succeeded.
 **
 ** Like Scintilla's internal headers, this header expects Geometry.h and
 ** Platform.h (and their standard-library prerequisites) to be included
 ** before it.
 **/

#ifndef TESTPLATFORM_H
#define TESTPLATFORM_H

namespace Scintilla::Internal {

// Fixed metrics reported by every test font, and the fixed advance of every
// code point. Small round numbers so tests can compute expected pixel
// positions by hand: a line is 10 pixels high, a character 10 wide.
constexpr XYPOSITION testFontAscent = 8;
constexpr XYPOSITION testFontDescent = 2;
constexpr XYPOSITION testCharWidth = 10;

// Everything the platform layer observes. Owned by TestHost; the platform
// free functions reach it through TestHost::CurrentLog().
struct TestPlatformLog {
	// One formatted line per Surface drawing call, in call order.
	std::vector<std::string> drawCommands;
	// Popup-window activity: menu creation and show requests.
	std::vector<std::string> popupRequests;
	// Calls into features the test platform does not implement (list box
	// display, bidirectional layout). Recorded so a test that strays into
	// them sees the request instead of silent fake success.
	std::vector<std::string> unsupportedRequests;
	int surfacesAllocated = 0;
	int fontsAllocated = 0;
	int listBoxesAllocated = 0;
};

// The in-memory state behind a WindowID. Scintilla's Window methods
// (defined in TestPlatform.cxx) read and write this.
struct TestWindow {
	PRectangle rect;
	Window::Cursor cursor = Window::Cursor::invalid;
	bool visible = false;
	bool mouseCaptured = false;
	std::vector<PRectangle> invalidations;
	int invalidateAllCount = 0;
};

// A test constructs one TestHost before its editor and keeps it alive
// throughout. The platform free functions (Surface::Allocate,
// Font::Allocate, ListBox::Allocate, Menu and Platform calls) take no
// context argument, so they find the log through the single current host;
// only one TestHost may be alive at a time. Fixtures that compare two
// editors therefore run them one after the other, not side by side.
class TestHost {
public:
	TestHost();
	~TestHost();
	TestHost(const TestHost &) = delete;
	TestHost(TestHost &&) = delete;
	TestHost &operator=(const TestHost &) = delete;
	TestHost &operator=(TestHost &&) = delete;

	// The log of the live host, or null when no host is alive.
	static TestPlatformLog *CurrentLog() noexcept;

	TestPlatformLog log;
	TestWindow mainWindow;	// becomes the editor's wMain
};

}

#endif
