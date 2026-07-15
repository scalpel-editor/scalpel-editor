// scalpel-editor test code
/** @file TestPlatform.h
 ** Deterministic test implementation of the Platform.h contracts.
 **
 ** The platform here never draws pixels, opens real windows, or talks to a
 ** display server. Windows store their state in memory, surfaces record
 ** drawing commands as text, fonts report fixed metrics, and autocomplete
 ** list boxes and call-tip windows are fully in-memory and inspectable.
 ** Menus and bidirectional layout still record the request instead of
 ** pretending they succeeded.
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

// One entry in the in-memory autocomplete list box.
struct TestListBoxItem {
	std::string text;
	int type = -1;
};

// Inspectable state of the autocomplete list box. Updated on every list-box
// method so tests can assert without parsing log strings.
struct TestListBoxState {
	bool created = false;
	bool visible = false;
	PRectangle rect{};
	Point location{};
	int lineHeight = 0;
	int averageCharWidth = static_cast<int>(testCharWidth);
	int visibleRows = 5;
	int selection = -1;
	int caretFromEdge = 0;
	int createCount = 0;
	int imageRegisterCount = 0;
	int rgbaImageRegisterCount = 0;
	std::vector<TestListBoxItem> items;
};

// Inspectable state of the call-tip window owned by TestHost and assigned
// from CreateCallTipWindow.
struct TestCallTipState {
	bool created = false;
	bool visible = false;
	PRectangle rect{};
	int createCount = 0;
	int invalidateAllCount = 0;
};

// Everything the platform layer observes. Owned by TestHost; the platform
// free functions reach it through TestHost::CurrentLog().
struct TestPlatformLog {
	// One formatted line per Surface drawing call, in call order.
	std::vector<std::string> drawCommands;
	// Popup-window activity: menu creation and show requests.
	std::vector<std::string> popupRequests;
	// Calls into features the test platform does not implement
	// (bidirectional layout). Recorded so a test that strays into them sees
	// the request instead of silent fake success.
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

// Forward declaration: the concrete list box lives in TestPlatform.cxx and
// registers itself with the host for event injection.
class TestListBox;

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
	// The live host, or null when no host is alive.
	static TestHost *Current() noexcept;

	// Inject list-box events as if the user changed selection or double-clicked.
	// No-op when no list box is registered.
	void NotifyListBoxSelectionChange();
	void NotifyListBoxDoubleClick();

	TestPlatformLog log;
	TestWindow mainWindow;	// becomes the editor's wMain
	TestWindow callTipWindow;	// becomes ct.wCallTip from CreateCallTipWindow
	TestListBoxState listBox;
	TestCallTipState callTip;

	// Non-owning pointer to the allocated list box, set by ListBox::Allocate
	// and cleared when the list box is destroyed. Used for event injection.
	TestListBox *liveListBox = nullptr;
	// WindowID of the list box after Create, for Destroy tracking.
	WindowID listBoxWindowId = nullptr;
};

}

#endif
