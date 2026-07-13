// scalpel-editor test code
/** @file TestPlatform.cxx
 ** Deterministic test implementation of the Platform.h contracts.
 ** See TestPlatform.h for the design.
 **/

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <memory>

#include "ScintillaTypes.h"

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

size_t CodePointsIn(std::string_view text, bool utf8) noexcept {
	if (!utf8)
		return text.length();
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

	void MeasureWidths(const Font *font_, std::string_view text, XYPOSITION *positions) override {
		if (mode.codePage == Scintilla::CpUtf8) {
			MeasureWidthsUTF8(font_, text, positions);
			return;
		}
		// Outside UTF-8 mode every byte is one character.
		XYPOSITION x = 0;
		for (size_t i = 0; i < text.length(); i++) {
			x += testCharWidth;
			positions[i] = x;
		}
	}

	XYPOSITION WidthText(const Font *, std::string_view text) override {
		return testCharWidth * static_cast<XYPOSITION>(CodePointsIn(text, mode.codePage == Scintilla::CpUtf8));
	}

	void DrawTextNoClipUTF8(PRectangle rc, const Font *, XYPOSITION ybase, std::string_view text,
		ColourRGBA fore, ColourRGBA) override {
		RecordText("DrawTextNoClipUTF8", rc, ybase, text, fore);
	}

	void DrawTextClippedUTF8(PRectangle rc, const Font *, XYPOSITION ybase, std::string_view text,
		ColourRGBA fore, ColourRGBA) override {
		RecordText("DrawTextClippedUTF8", rc, ybase, text, fore);
	}

	void DrawTextTransparentUTF8(PRectangle rc, const Font *, XYPOSITION ybase, std::string_view text,
		ColourRGBA fore) override {
		RecordText("DrawTextTransparentUTF8", rc, ybase, text, fore);
	}

	void MeasureWidthsUTF8(const Font *, std::string_view text, XYPOSITION *positions) override {
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

	XYPOSITION WidthTextUTF8(const Font *, std::string_view text) override {
		return testCharWidth * static_cast<XYPOSITION>(CodePointsIn(text, true));
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

// A list box that records every call. It reports emptiness from all getters:
// autocomplete display is not a feature the test platform implements, and a
// test that reaches it should see the requests, not fake success.
class TestListBox final : public ListBox {
public:
	void SetFont(const Font *) override {
		RecordInto(UnsupportedRequests(), "ListBox::SetFont");
	}
	void Create(Window &, int, Point location, int lineHeight_, bool, Scintilla::Technology) override {
		RecordInto(UnsupportedRequests(), "ListBox::Create at %.1f,%.1f lineHeight=%d",
			location.x, location.y, lineHeight_);
	}
	void SetAverageCharWidth(int width) override {
		RecordInto(UnsupportedRequests(), "ListBox::SetAverageCharWidth %d", width);
	}
	void SetVisibleRows(int rows) override {
		RecordInto(UnsupportedRequests(), "ListBox::SetVisibleRows %d", rows);
	}
	int GetVisibleRows() const override {
		RecordInto(UnsupportedRequests(), "ListBox::GetVisibleRows");
		return 0;
	}
	PRectangle GetDesiredRect() override {
		RecordInto(UnsupportedRequests(), "ListBox::GetDesiredRect");
		return PRectangle();
	}
	int CaretFromEdge() override {
		RecordInto(UnsupportedRequests(), "ListBox::CaretFromEdge");
		return 0;
	}
	void Clear() noexcept override {
		RecordInto(UnsupportedRequests(), "ListBox::Clear");
	}
	void Append(char *s, int type) override {
		RecordInto(UnsupportedRequests(), "ListBox::Append '%s' type=%d", s ? s : "", type);
	}
	int Length() override {
		RecordInto(UnsupportedRequests(), "ListBox::Length");
		return 0;
	}
	void Select(int n) override {
		RecordInto(UnsupportedRequests(), "ListBox::Select %d", n);
	}
	int GetSelection() override {
		RecordInto(UnsupportedRequests(), "ListBox::GetSelection");
		return -1;
	}
	int Find(const char *prefix) override {
		RecordInto(UnsupportedRequests(), "ListBox::Find '%s'", prefix ? prefix : "");
		return -1;
	}
	std::string GetValue(int n) override {
		RecordInto(UnsupportedRequests(), "ListBox::GetValue %d", n);
		return std::string();
	}
	void RegisterImage(int, const char *) override {
		RecordInto(UnsupportedRequests(), "ListBox::RegisterImage");
	}
	void RegisterRGBAImage(int, int, int, const unsigned char *) override {
		RecordInto(UnsupportedRequests(), "ListBox::RegisterRGBAImage");
	}
	void ClearRegisteredImages() override {
		RecordInto(UnsupportedRequests(), "ListBox::ClearRegisteredImages");
	}
	void SetDelegate(IListBoxDelegate *) override {
		RecordInto(UnsupportedRequests(), "ListBox::SetDelegate");
	}
	void SetList(const char *list, char, char) override {
		RecordInto(UnsupportedRequests(), "ListBox::SetList '%s'", list ? list : "");
	}
	void SetOptions(ListOptions) override {
		RecordInto(UnsupportedRequests(), "ListBox::SetOptions");
	}
};

}

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
	wid = nullptr;
}

PRectangle Window::GetPosition() const {
	const TestWindow *window = WindowData(wid);
	return window ? window->rect : PRectangle();
}

void Window::SetPosition(PRectangle rc) {
	if (TestWindow *window = WindowData(wid))
		window->rect = rc;
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
	if (TestWindow *window = WindowData(wid))
		window->visible = show;
}

void Window::InvalidateAll() {
	if (TestWindow *window = WindowData(wid))
		window->invalidateAllCount++;
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
