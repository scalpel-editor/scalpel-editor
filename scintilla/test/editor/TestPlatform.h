// scalpel-editor test code
/** @file TestPlatform.h
 ** Deterministic test host for Window, ListBox, Menu, and Platform helpers.
 **
 ** Surfaces and fonts come from the concrete DrawSurface / FontPlatform path
 ** (fixture fonts via UseTestFontPaths). This file keeps in-memory windows,
 ** autocomplete list boxes, call-tip windows, and menu request logs so host
 ** observation stays inspectable without a display server.
 **
 ** Like Scintilla's internal headers, this header expects Geometry.h and
 ** Platform.h (and their standard-library prerequisites) to be included
 ** before it.
 **/

#ifndef TESTPLATFORM_H
#define TESTPLATFORM_H

#include <memory>

namespace Scintilla::Internal {

class GlContext;
class Renderer;

// Default list-box geometry when the core has not supplied a line height or
// average character width yet. Not used for document layout or paint metrics.
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

// Everything the host-side platform helpers observe. Owned by TestHost; free
// functions reach it through TestHost::CurrentLog().
struct TestPlatformLog {
	// Popup-window activity: menu creation and show requests.
	std::vector<std::string> popupRequests;
	// Calls into features the host does not implement. Recorded so a test that
	// strays into them sees the request instead of silent fake success.
	std::vector<std::string> unsupportedRequests;
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
// throughout. Window / ListBox / Menu free functions take no context argument,
// so they find this host through Current(); only one TestHost may be alive at
// a time. Fixtures that compare two editors therefore run them one after the
// other, not side by side.
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

	// Create headless GL context and Renderer on first paint need.
	void EnsureRenderer();
	Renderer *GetRenderer() noexcept { return renderer.get(); }

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

private:
	std::unique_ptr<GlContext> glContext;
	std::unique_ptr<Renderer> renderer;
};

}

#endif
