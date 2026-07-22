#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "ApplicationEditor.h"

TEST_CASE("production editor host constructs and renders its initial buffer") {
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("scalpel-editor\nvisible text\n");

	CHECK(editor.Text() == "scalpel-editor\nvisible text\n");
	editor.RenderFrame();

	const std::vector<uint8_t> pixels = editor.FramePixels();
	REQUIRE(pixels.size() == 320U * 180U * 4U);
	bool hasNonBackgroundPixel = false;
	for (size_t offset = 4; offset < pixels.size(); offset += 4) {
		if (!std::equal(pixels.begin(), pixels.begin() + 4, pixels.begin() + offset)) {
			hasNonBackgroundPixel = true;
			break;
		}
	}
	CHECK(hasNonBackgroundPixel);
	CHECK_FALSE(editor.Notifications().empty());
}

TEST_CASE("production editor host rejects presentation without a window surface") {
	Scalpel::ApplicationEditor editor(320, 180);

	CHECK_THROWS_WITH(editor.PresentFrame(),
		"ApplicationEditor::PresentFrame requires a window surface");
	CHECK(editor.FramePixels().empty());
}

TEST_CASE("production editor host exposes shell state") {
	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("one\ntwo\nthree\n");

	editor.SetKeyboardFocus(true);
	CHECK(editor.WindowState().visible);
	CHECK_FALSE(editor.TickerRequests().empty());
	CHECK(editor.Notifications().back() == Scintilla::Notification::FocusIn);

	editor.SetPointerCapture(true);
	CHECK(editor.WindowState().mouseCaptured);
	editor.SetPointerCapture(false);
	CHECK_FALSE(editor.WindowState().mouseCaptured);

	const size_t invalidationsBeforeResize = editor.WindowState().invalidatedRectangles.size();
	editor.Resize(300, 120);
	CHECK(editor.FrameWidth() == 300);
	CHECK(editor.FrameHeight() == 120);
	REQUIRE(editor.WindowState().invalidatedRectangles.size() > invalidationsBeforeResize);
	const Scintilla::Internal::PRectangle resizedClient = editor.WindowState().invalidatedRectangles.back();
	CHECK(resizedClient.left == 0);
	CHECK(resizedClient.top == 0);
	CHECK(resizedClient.right == 300);
	CHECK(resizedClient.bottom == 120);
	editor.RenderFrame();
	CHECK(editor.Scrollbars().changes > 0);
}

TEST_CASE("production editor host schedules tickers against a monotonic clock") {
	using namespace std::chrono_literals;
	Scalpel::ApplicationEditor::Clock::time_point now{};
	Scalpel::ApplicationEditor editor(240, 120, [&now] { return now; });
	editor.LoadInitialBuffer("caret timer\n");
	CHECK(editor.TimeUntilNextWork() == 0ms);
	editor.RunPendingWork();
	CHECK_FALSE(editor.IdleRequested());
	editor.SetKeyboardFocus(true);

	REQUIRE_FALSE(editor.TickerRequests().empty());
	const int period = editor.TickerRequests().back().milliseconds;
	REQUIRE(period > 0);
	CHECK(editor.TimeUntilNextWork() == std::chrono::milliseconds(period));
	editor.RenderFrame();
	CHECK_FALSE(editor.NeedsRedraw());

	now += std::chrono::milliseconds(period - 1);
	editor.RunPendingWork();
	CHECK_FALSE(editor.NeedsRedraw());
	CHECK(editor.TimeUntilNextWork() == 1ms);

	now += 1ms;
	editor.RunPendingWork();
	CHECK(editor.NeedsRedraw());
	CHECK(editor.TimeUntilNextWork() == std::chrono::milliseconds(period));

	editor.RenderFrame();
	now += std::chrono::milliseconds(period * 20);
	editor.RunPendingWork();
	CHECK(editor.WindowState().invalidatedRectangles.size() <= 8);
	CHECK(editor.TimeUntilNextWork() == std::chrono::milliseconds(period));

	editor.SetKeyboardFocus(false);
	CHECK_FALSE(editor.TimeUntilNextWork().has_value());
}

TEST_CASE("deferred application services fail visibly") {
	Scalpel::ApplicationEditor editor(200, 100);
	editor.LoadInitialBuffer("clipboard request");
	CHECK(editor.UnsupportedRequests().empty());
	const size_t requestsBeforeExplicitClipboardUse = editor.UnsupportedRequests().size();

	editor.SetKeyboardFocus(true);
	editor.SetKeyboardFocus(false);
	editor.RequestClipboardCopy();

	CHECK(editor.Notifications().back() == Scintilla::Notification::FocusOut);
	CHECK_FALSE(editor.ClipboardPasteAvailable());
	REQUIRE(editor.UnsupportedRequests().size() == requestsBeforeExplicitClipboardUse + 2);
	CHECK(editor.UnsupportedRequests()[requestsBeforeExplicitClipboardUse] == "clipboard copy");
	CHECK(editor.UnsupportedRequests()[requestsBeforeExplicitClipboardUse + 1] == "clipboard paste availability");
}

TEST_CASE("production editor keyboard runs commands and inserts UTF-8 text") {
	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("ab");

	editor.HandleKeyboardInput({Scintilla::Keys::End, Scintilla::KeyMod::Ctrl, {}, 1, true});
	editor.HandleKeyboardInput({Scintilla::Keys::Back, Scintilla::KeyMod::Norm, {}, 2, false});
	CHECK(editor.Text() == "ab");
	editor.HandleKeyboardInput({Scintilla::Keys::Back, Scintilla::KeyMod::Norm, {}, 3, true});
	CHECK(editor.Text() == "a");
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>(0), Scintilla::KeyMod::Norm,
		"\xE2\x98\x83", 4, true});
	CHECK(editor.Text() == "a\xE2\x98\x83");

	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>('A'), Scintilla::KeyMod::Ctrl,
		"\x01", 5, true});
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>('Z'), Scintilla::KeyMod::Norm,
		"z", 6, true});
	CHECK(editor.Text() == "z");
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>('Q'), Scintilla::KeyMod::Ctrl,
		"q", 7, true});
	CHECK(editor.Text() == "z");
}

TEST_CASE("production editor pointer clicks drags and releases capture") {
	Scalpel::ApplicationEditor editor(320, 120);
	editor.LoadInitialBuffer("alpha beta gamma");
	editor.RenderFrame();

	editor.HandlePointerInput({Scalpel::PointerAction::Move, Scintilla::KeyMod::Norm,
		20, 8, 0, 0, 10, -1});
	editor.HandlePointerInput({Scalpel::PointerAction::Press, Scintilla::KeyMod::Norm,
		20, 8, 0, 0, 11, 0});
	CHECK(editor.WindowState().mouseCaptured);
	editor.HandlePointerInput({Scalpel::PointerAction::Move, Scintilla::KeyMod::Norm,
		100, 8, 0, 0, 12, -1});
	CHECK(editor.WindowState().mouseCaptured);
	editor.HandlePointerInput({Scalpel::PointerAction::Release, Scintilla::KeyMod::Norm,
		100, 8, 0, 0, 13, 0});
	CHECK_FALSE(editor.WindowState().mouseCaptured);
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>('Z'), Scintilla::KeyMod::Norm,
		"z", 14, true});
	CHECK(editor.Text().find('z') != std::string::npos);
	CHECK(editor.Text().size() < std::string("alpha beta gamma").size() + 1);
}

TEST_CASE("production editor pointer wheel scrolls both axes") {
	Scalpel::ApplicationEditor editor(240, 80);
	std::string text;
	for (int line = 0; line < 40; line++) {
		text += "a long line that can scroll horizontally and vertically\n";
	}
	editor.LoadInitialBuffer(text);
	editor.RenderFrame();
	const Scintilla::Line verticalBefore = editor.Scrollbars().verticalPosition;

	editor.HandlePointerInput({Scalpel::PointerAction::Scroll, Scintilla::KeyMod::Norm,
		0, 0, 0, 10, 20, -1});
	CHECK(editor.Scrollbars().verticalPosition > verticalBefore);
	editor.HandlePointerInput({Scalpel::PointerAction::Scroll, Scintilla::KeyMod::Norm,
		0, 0, 10, 0, 21, -1});
	CHECK(editor.Scrollbars().horizontalPosition > 0);
}
