// scalpel-editor test code
/** @file TestPlatform.cxx
 ** Deterministic test implementation of the Platform.h contracts.
 ** See TestPlatform.h for the design.
 **/

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <memory>

#include "EditorBasicTypes.h"
#include "EditorDocumentTypes.h"
#include "EditorStyleTypes.h"
#include "EditorInputTypes.h"
#include "EditorLayoutTypes.h"

#include "Debugging.h"
#include "Geometry.h"
#include "Platform.h"

#include "UniConversion.h"

#include "TestPlatform.h"

namespace Scintilla::Internal {

namespace {

TestHost *currentHost = nullptr;

TestWindow *WindowData(WindowID wid) noexcept {
	return static_cast<TestWindow *>(wid);
}

void RecordInto(std::vector<std::string> *destination, const char *format, ...) {
	if (!destination)
		return;
	char buffer[512];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	destination->emplace_back(buffer);
}

std::vector<std::string> *DrawCommands() noexcept {
	TestPlatformLog *log = TestHost::CurrentLog();
	return log ? &log->drawCommands : nullptr;
}

std::vector<std::string> *PopupRequests() noexcept {
	TestPlatformLog *log = TestHost::CurrentLog();
	return log ? &log->popupRequests : nullptr;
}

std::vector<std::string> *UnsupportedRequests() noexcept {
	TestPlatformLog *log = TestHost::CurrentLog();
	return log ? &log->unsupportedRequests : nullptr;
}

std::string RectText(PRectangle rc) {
	char buffer[128];
	snprintf(buffer, sizeof(buffer), "%.1f,%.1f %.1fx%.1f", rc.left, rc.top, rc.Width(), rc.Height());
	return buffer;
}

std::string ColourText(ColourRGBA colour) {
	char buffer[16];
	snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X",
		colour.GetRed(), colour.GetGreen(), colour.GetBlue(), colour.GetAlpha());
	return buffer;
}

size_t CodePointsIn(std::string_view text) noexcept {
	size_t count = 0;
	size_t i = 0;
	while (i < text.length()) {
		size_t bytes = UTF8BytesOfLead[static_cast<unsigned char>(text[i])];
		if (i + bytes > text.length())
			bytes = text.length() - i;
		i += bytes;
		count++;
	}
	return count;
}

class TestFont final : public Font {
public:
	TestFont() noexcept = default;
};

class TestSurface final : public Surface {
public:
	void Init(WindowID) override {
		initialised = true;
	}

	void Init(SurfaceID, WindowID) override {
		initialised = true;
	}

	std::unique_ptr<Surface> AllocatePixMap(int width, int height) override {
		Record("AllocatePixMap %dx%d", width, height);
		auto pixMap = std::make_unique<TestSurface>();
		pixMap->mode = mode;
		pixMap->initialised = true;
		return pixMap;
	}

	void SetMode(SurfaceMode mode_) override {
		mode = mode_;
	}

	void Release() noexcept override {
		initialised = false;
	}

	int SupportsFeature(Scintilla::Supports) noexcept override {
		return 0;
	}

	bool Initialised() override {
		return initialised;
	}

	int LogPixelsY() override {
		return 96;
	}

	int PixelDivisions() override {
		return 1;
	}

	int DeviceHeightFont(int points) override {
		return (points * LogPixelsY() + 36) / 72;
	}

	void LineDraw(Point start, Point end, Stroke) override {
		Record("LineDraw %.1f,%.1f -> %.1f,%.1f", start.x, start.y, end.x, end.y);
	}

	void PolyLine(const Point *, size_t npts, Stroke) override {
		Record("PolyLine %zu points", npts);
	}

	void Polygon(const Point *, size_t npts, FillStroke) override {
		Record("Polygon %zu points", npts);
	}

	void RectangleDraw(PRectangle rc, FillStroke) override {
		Record("RectangleDraw %s", RectText(rc).c_str());
	}

	void RectangleFrame(PRectangle rc, Stroke) override {
		Record("RectangleFrame %s", RectText(rc).c_str());
	}

	void FillRectangle(PRectangle rc, Fill fill) override {
		Record("FillRectangle %s %s", RectText(rc).c_str(), ColourText(fill.colour).c_str());
	}

	void FillRectangleAligned(PRectangle rc, Fill fill) override {
		Record("FillRectangleAligned %s %s", RectText(rc).c_str(), ColourText(fill.colour).c_str());
	}

	void FillRectangle(PRectangle rc, Surface &) override {
		Record("FillRectanglePattern %s", RectText(rc).c_str());
	}

	void RoundedRectangle(PRectangle rc, FillStroke) override {
		Record("RoundedRectangle %s", RectText(rc).c_str());
	}

	void AlphaRectangle(PRectangle rc, XYPOSITION, FillStroke) override {
		Record("AlphaRectangle %s", RectText(rc).c_str());
	}

	void GradientRectangle(PRectangle rc, const std::vector<ColourStop> &, GradientOptions) override {
		Record("GradientRectangle %s", RectText(rc).c_str());
	}

	void DrawRGBAImage(PRectangle rc, int width, int height, const unsigned char *) override {
		Record("DrawRGBAImage %s %dx%d", RectText(rc).c_str(), width, height);
	}

	void Ellipse(PRectangle rc, FillStroke) override {
		Record("Ellipse %s", RectText(rc).c_str());
	}

	void Stadium(PRectangle rc, FillStroke, Ends) override {
		Record("Stadium %s", RectText(rc).c_str());
	}

	void Copy(PRectangle rc, Point from, Surface &) override {
		Record("Copy %s from %.1f,%.1f", RectText(rc).c_str(), from.x, from.y);
	}

	std::unique_ptr<IScreenLineLayout> Layout(const IScreenLine *) override {
		// Only bidirectional mode asks for a screen-line layout; the test
		// platform does not implement it.
		RecordInto(UnsupportedRequests(), "Surface::Layout");
		return {};
	}

	void DrawTextNoClip(PRectangle rc, const Font *, XYPOSITION ybase, std::string_view text,
		ColourRGBA fore, ColourRGBA) override {
		RecordText("DrawTextNoClip", rc, ybase, text, fore);
	}

	void DrawTextClipped(PRectangle rc, const Font *, XYPOSITION ybase, std::string_view text,
		ColourRGBA fore, ColourRGBA) override {
		RecordText("DrawTextClipped", rc, ybase, text, fore);
	}

	void DrawTextTransparent(PRectangle rc, const Font *, XYPOSITION ybase, std::string_view text,
		ColourRGBA fore) override {
		RecordText("DrawTextTransparent", rc, ybase, text, fore);
	}

	void MeasureWidths(const Font *, std::string_view text, XYPOSITION *positions) override {
		// Each code point advances testCharWidth. Every byte of a code point
		// reports the code point's end position, matching the real platform
		// layers, so position arrays are filled for all requested bytes.
		XYPOSITION x = 0;
		size_t i = 0;
		while (i < text.length()) {
			size_t bytes = UTF8BytesOfLead[static_cast<unsigned char>(text[i])];
			if (i + bytes > text.length())
				bytes = text.length() - i;
			x += testCharWidth;
			for (size_t b = 0; b < bytes; b++) {
				positions[i + b] = x;
			}
			i += bytes;
		}
	}

	XYPOSITION WidthText(const Font *, std::string_view text) override {
		return testCharWidth * static_cast<XYPOSITION>(CodePointsIn(text));
	}

	XYPOSITION Ascent(const Font *) override {
		return testFontAscent;
	}

	XYPOSITION Descent(const Font *) override {
		return testFontDescent;
	}

	XYPOSITION InternalLeading(const Font *) override {
		return 0;
	}

	XYPOSITION Height(const Font *) override {
		return testFontAscent + testFontDescent;
	}

	XYPOSITION AverageCharWidth(const Font *) override {
		return testCharWidth;
	}

	void SetClip(PRectangle rc) override {
		Record("SetClip %s", RectText(rc).c_str());
	}

	void PopClip() override {
		Record("PopClip");
	}

	void FlushCachedState() override {
	}

	void FlushDrawing() override {
	}

private:
	void Record(const char *format, ...) {
		std::vector<std::string> *destination = DrawCommands();
		if (!destination)
			return;
		char buffer[512];
		va_list args;
		va_start(args, format);
		vsnprintf(buffer, sizeof(buffer), format, args);
		va_end(args);
		destination->emplace_back(buffer);
	}

	void RecordText(const char *operation, PRectangle rc, XYPOSITION ybase, std::string_view text,
		ColourRGBA fore) {
		Record("%s %s ybase=%.1f %s '%.*s'", operation, RectText(rc).c_str(), ybase,
			ColourText(fore).c_str(), static_cast<int>(text.length()), text.data());
	}

	SurfaceMode mode;
	bool initialised = false;
};

} // namespace

// In-memory list box that stores items, selection, geometry, and a delegate
// so autocomplete can run end to end under the test host.
class TestListBox final : public ListBox {
public:
	TestListBox() {
		if (TestHost *host = TestHost::Current())
			host->liveListBox = this;
	}

	~TestListBox() override {
		if (TestHost *host = TestHost::Current()) {
			if (host->liveListBox == this) {
				host->liveListBox = nullptr;
				host->listBoxWindowId = nullptr;
			}
		}
	}

	void SetFont(const Font *) override {
		// Font choice does not affect fixed test metrics.
		PublishState();
	}

	void Create(Window &, int, Point location, int lineHeight_, Scintilla::Technology) override {
		window.rect = PRectangle(location.x, location.y, location.x, location.y);
		window.cursor = Window::Cursor::invalid;
		window.visible = false;
		window.mouseCaptured = false;
		window.invalidations.clear();
		window.invalidateAllCount = 0;
		wid = &window;
		location_ = location;
		lineHeight = lineHeight_ > 0 ? lineHeight_ : static_cast<int>(testFontAscent + testFontDescent);
		createCount++;
		if (TestHost *host = TestHost::Current()) {
			host->listBoxWindowId = wid;
			host->listBox.createCount = createCount;
		}
		PublishState();
	}

	void SetAverageCharWidth(int width) override {
		averageCharWidth = width > 0 ? width : static_cast<int>(testCharWidth);
		PublishState();
	}

	void SetVisibleRows(int rows) override {
		visibleRows = rows > 0 ? rows : 1;
		PublishState();
	}

	int GetVisibleRows() const override {
		return visibleRows;
	}

	PRectangle GetDesiredRect() override {
		const int itemCount = static_cast<int>(items.size());
		const int displayRows = std::min(visibleRows, std::max(itemCount, 1));
		int maxChars = 1;
		for (const TestListBoxItem &item : items) {
			maxChars = std::max(maxChars, static_cast<int>(CodePointsIn(item.text)));
		}
		const int width = maxChars * averageCharWidth + caretFromEdge + averageCharWidth;
		const int height = displayRows * lineHeight;
		// Origin at 0,0; the editor positions the window with SetPositionRelative.
		return PRectangle(0, 0, static_cast<XYPOSITION>(width), static_cast<XYPOSITION>(height));
	}

	int CaretFromEdge() override {
		return caretFromEdge;
	}

	void Clear() noexcept override {
		items.clear();
		selection = -1;
		PublishState();
	}

	void Append(char *s, int type) override {
		TestListBoxItem item;
		item.text = s ? s : "";
		item.type = type;
		items.push_back(std::move(item));
		PublishState();
	}

	int Length() override {
		return static_cast<int>(items.size());
	}

	void Select(int n) override {
		if (n < -1)
			n = -1;
		if (n >= static_cast<int>(items.size()))
			n = static_cast<int>(items.size()) - 1;
		const bool changed = selection != n;
		selection = n;
		PublishState();
		// Real platforms report selection changes to the editor so it can
		// fire AutoCSelectionChange. Mirror that when the index changes.
		if (changed && delegate) {
			ListBoxEvent event(ListBoxEvent::EventType::selectionChange);
			delegate->ListNotify(&event);
		}
	}

	int GetSelection() override {
		return selection;
	}

	int Find(const char *prefix) override {
		if (!prefix)
			return -1;
		const size_t prefixLen = std::strlen(prefix);
		for (size_t i = 0; i < items.size(); i++) {
			if (items[i].text.size() >= prefixLen &&
				items[i].text.compare(0, prefixLen, prefix) == 0) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	std::string GetValue(int n) override {
		if (n < 0 || n >= static_cast<int>(items.size()))
			return {};
		return items[static_cast<size_t>(n)].text;
	}

	void RegisterImage(int, const char *) override {
		imageRegisterCount++;
		PublishState();
	}

	void RegisterRGBAImage(int, int, int, const unsigned char *) override {
		rgbaImageRegisterCount++;
		PublishState();
	}

	void ClearRegisteredImages() override {
		imageRegisterCount = 0;
		rgbaImageRegisterCount = 0;
		PublishState();
	}

	void SetDelegate(IListBoxDelegate *lbDelegate) override {
		delegate = lbDelegate;
		PublishState();
	}

	void SetList(const char *list, char separator, char typesep) override {
		Clear();
		if (!list)
			return;
		// Same split as the GTK and Win32 list boxes: separator splits
		// items, typesep introduces an optional integer type after the text.
		const size_t count = std::strlen(list) + 1;
		std::vector<char> words(list, list + count);
		char *startword = words.data();
		char *numword = nullptr;
		for (size_t i = 0; words[i]; i++) {
			if (words[i] == separator) {
				words[i] = '\0';
				if (numword)
					*numword = '\0';
				Append(startword, numword ? std::atoi(numword + 1) : -1);
				startword = words.data() + i + 1;
				numword = nullptr;
			} else if (words[i] == typesep) {
				numword = words.data() + i;
			}
		}
		if (startword) {
			if (numword)
				*numword = '\0';
			Append(startword, numword ? std::atoi(numword + 1) : -1);
		}
		PublishState();
	}

	void SetOptions(ListOptions options_) override {
		options = options_;
		PublishState();
	}

	void NotifySelectionChange() {
		if (delegate) {
			ListBoxEvent event(ListBoxEvent::EventType::selectionChange);
			delegate->ListNotify(&event);
		}
	}

	void NotifyDoubleClick() {
		if (delegate) {
			ListBoxEvent event(ListBoxEvent::EventType::doubleClick);
			delegate->ListNotify(&event);
		}
	}

	// Refresh host-visible state after Window::Show / SetPosition, which
	// update TestWindow but do not call back into TestListBox.
	void PublishState() noexcept {
		if (TestHost *host = TestHost::Current()) {
			host->listBox.created = Created();
			host->listBox.visible = window.visible;
			host->listBox.rect = window.rect;
			host->listBox.location = location_;
			host->listBox.lineHeight = lineHeight;
			host->listBox.averageCharWidth = averageCharWidth;
			host->listBox.visibleRows = visibleRows;
			host->listBox.selection = selection;
			host->listBox.caretFromEdge = caretFromEdge;
			host->listBox.createCount = createCount;
			host->listBox.imageRegisterCount = imageRegisterCount;
			host->listBox.rgbaImageRegisterCount = rgbaImageRegisterCount;
			host->listBox.items = items;
		}
	}

private:
	TestWindow window;
	std::vector<TestListBoxItem> items;
	IListBoxDelegate *delegate = nullptr;
	ListOptions options;
	Point location_{};
	int lineHeight = static_cast<int>(testFontAscent + testFontDescent);
	int averageCharWidth = static_cast<int>(testCharWidth);
	int visibleRows = 5;
	int selection = -1;
	int caretFromEdge = 0;
	int createCount = 0;
	int imageRegisterCount = 0;
	int rgbaImageRegisterCount = 0;
};

TestHost::TestHost() {
	if (currentHost) {
		fprintf(stderr, "TestPlatform: only one TestHost may be alive at a time\n");
		abort();
	}
	currentHost = this;
}

TestHost::~TestHost() {
	currentHost = nullptr;
}

TestPlatformLog *TestHost::CurrentLog() noexcept {
	return currentHost ? &currentHost->log : nullptr;
}

TestHost *TestHost::Current() noexcept {
	return currentHost;
}

void TestHost::NotifyListBoxSelectionChange() {
	if (liveListBox)
		liveListBox->NotifySelectionChange();
}

void TestHost::NotifyListBoxDoubleClick() {
	if (liveListBox)
		liveListBox->NotifyDoubleClick();
}

std::shared_ptr<Font> Font::Allocate(const FontParameters &) {
	if (TestPlatformLog *log = TestHost::CurrentLog())
		log->fontsAllocated++;
	return std::make_shared<TestFont>();
}

std::unique_ptr<Surface> Surface::Allocate(Scintilla::Technology) {
	if (TestPlatformLog *log = TestHost::CurrentLog())
		log->surfacesAllocated++;
	return std::make_unique<TestSurface>();
}

Window::~Window() noexcept = default;

void Window::Destroy() noexcept {
	if (currentHost) {
		if (wid == &currentHost->callTipWindow) {
			currentHost->callTip.created = false;
			currentHost->callTip.visible = false;
			currentHost->callTipWindow.visible = false;
		}
		if (wid && wid == currentHost->listBoxWindowId) {
			currentHost->listBox.created = false;
			currentHost->listBox.visible = false;
			currentHost->listBoxWindowId = nullptr;
		}
	}
	if (TestWindow *window = WindowData(wid))
		window->visible = false;
	wid = nullptr;
}

PRectangle Window::GetPosition() const {
	const TestWindow *window = WindowData(wid);
	return window ? window->rect : PRectangle();
}

void Window::SetPosition(PRectangle rc) {
	if (TestWindow *window = WindowData(wid)) {
		window->rect = rc;
		if (currentHost) {
			if (wid == &currentHost->callTipWindow) {
				currentHost->callTip.rect = rc;
			}
			if (wid && wid == currentHost->listBoxWindowId && currentHost->liveListBox) {
				currentHost->liveListBox->PublishState();
			}
		}
	}
}

void Window::SetPositionRelative(PRectangle rc, const Window *) {
	SetPosition(rc);
}

PRectangle Window::GetClientPosition() const {
	// Client coordinates: same size as the window, origin at 0,0.
	const TestWindow *window = WindowData(wid);
	return window ? PRectangle(0, 0, window->rect.Width(), window->rect.Height()) : PRectangle();
}

void Window::Show(bool show) {
	if (TestWindow *window = WindowData(wid)) {
		window->visible = show;
		if (currentHost) {
			if (wid == &currentHost->callTipWindow) {
				currentHost->callTip.visible = show;
			}
			if (wid && wid == currentHost->listBoxWindowId && currentHost->liveListBox) {
				currentHost->liveListBox->PublishState();
			}
		}
	}
}

void Window::InvalidateAll() {
	if (TestWindow *window = WindowData(wid)) {
		window->invalidateAllCount++;
		if (currentHost && wid == &currentHost->callTipWindow) {
			currentHost->callTip.invalidateAllCount = window->invalidateAllCount;
		}
	}
}

void Window::InvalidateRectangle(PRectangle rc) {
	if (TestWindow *window = WindowData(wid))
		window->invalidations.push_back(rc);
}

void Window::SetCursor(Cursor curs) {
	cursorLast = curs;
	if (TestWindow *window = WindowData(wid))
		window->cursor = curs;
}

PRectangle Window::GetMonitorRect(Point) {
	return PRectangle(0, 0, 1920, 1080);
}

ListBox::ListBox() noexcept = default;
ListBox::~ListBox() noexcept = default;

std::unique_ptr<ListBox> ListBox::Allocate() {
	if (TestPlatformLog *log = TestHost::CurrentLog())
		log->listBoxesAllocated++;
	return std::make_unique<TestListBox>();
}

Menu::Menu() noexcept : mid{} {
}

void Menu::CreatePopUp() {
	RecordInto(PopupRequests(), "Menu::CreatePopUp");
}

void Menu::Destroy() noexcept {
	mid = nullptr;
}

void Menu::Show(Point pt, const Window &) {
	RecordInto(PopupRequests(), "Menu::Show at %.1f,%.1f", pt.x, pt.y);
}

ColourRGBA Platform::Chrome() {
	return ColourRGBA(0xe0, 0xe0, 0xe0);
}

ColourRGBA Platform::ChromeHighlight() {
	return ColourRGBA(0xff, 0xff, 0xff);
}

const char *Platform::DefaultFont() {
	return "TestFont";
}

int Platform::DefaultFontSize() {
	return 10;
}

unsigned int Platform::DoubleClickTime() {
	return 500;
}

void Platform::DebugDisplay(const char *s) noexcept {
	fprintf(stderr, "%s", s);
}

void Platform::DebugPrintf(const char *format, ...) noexcept {
	char buffer[2000];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	fprintf(stderr, "%s", buffer);
}

bool Platform::ShowAssertionPopUps(bool) noexcept {
	return false;
}

void Platform::Assert(const char *c, const char *file, int line) noexcept {
	fprintf(stderr, "Assertion [%s] failed at %s %d\n", c, file, line);
	abort();
}

}
