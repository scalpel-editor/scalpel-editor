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

TEST_CASE("production editor exposes clipboard requests and unavailable results") {
	Scalpel::ApplicationEditor editor(200, 100);
	editor.LoadInitialBuffer("clipboard request");
	CHECK(editor.UnsupportedRequests().empty());

	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>('A'), Scintilla::KeyMod::Ctrl,
		{}, 1, true});
	editor.RequestClipboardCopy();
	const std::vector<Scalpel::ApplicationClipboardRequest> requests =
		editor.TakeClipboardRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests.front().operation == Scalpel::ApplicationClipboardOperation::Copy);
	CHECK(requests.front().text == "clipboard request");
	editor.HandleClipboardResult(requests.front().id,
		Scalpel::ApplicationClipboardOperation::Copy,
		Scalpel::ApplicationClipboardStatus::Unavailable);

	CHECK_FALSE(editor.ClipboardPasteAvailable());
	CHECK(editor.UnsupportedRequests().empty());
	const std::vector<Scalpel::ApplicationClipboardResult> results =
		editor.TakeClipboardResults();
	REQUIRE(results.size() == 1);
	CHECK(results.front().status ==
		Scalpel::ApplicationClipboardStatus::Unavailable);
	CHECK(editor.ClipboardResults().empty());
}

TEST_CASE("production editor applies a completed asynchronous paste") {
	Scalpel::ApplicationEditor editor(200, 100);
	editor.LoadInitialBuffer("old text");
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>('A'), Scintilla::KeyMod::Ctrl,
		{}, 1, true});
	editor.SetClipboardPasteAvailable(true);
	CHECK(editor.ClipboardPasteAvailable());
	editor.RequestClipboardPaste();

	const std::vector<Scalpel::ApplicationClipboardRequest> requests =
		editor.TakeClipboardRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests.front().operation == Scalpel::ApplicationClipboardOperation::Paste);
	editor.HandleClipboardResult(requests.front().id,
		Scalpel::ApplicationClipboardOperation::Paste,
		Scalpel::ApplicationClipboardStatus::Complete, "new \xE2\x98\x83");

	CHECK(editor.Text() == "new \xE2\x98\x83");
	REQUIRE(editor.ClipboardResults().size() == 1);
	CHECK(editor.ClipboardResults().front().status ==
		Scalpel::ApplicationClipboardStatus::Complete);
}

TEST_CASE("production editor rejects stale asynchronous paste results") {
	Scalpel::ApplicationEditor editor(200, 100);
	editor.LoadInitialBuffer("first");
	editor.RequestClipboardPaste();
	const auto first = editor.TakeClipboardRequests();
	REQUIRE(first.size() == 1);

	editor.LoadInitialBuffer("replacement");
	editor.HandleClipboardResult(first.front().id,
		Scalpel::ApplicationClipboardOperation::Paste,
		Scalpel::ApplicationClipboardStatus::Complete, "stale");
	CHECK(editor.Text() == "replacement");
	CHECK(editor.ClipboardResults().back().status ==
		Scalpel::ApplicationClipboardStatus::Superseded);

	editor.RequestClipboardPaste();
	const auto older = editor.TakeClipboardRequests();
	editor.RequestClipboardPaste();
	const auto current = editor.TakeClipboardRequests();
	REQUIRE(older.size() == 1);
	REQUIRE(current.size() == 1);
	editor.HandleClipboardResult(older.front().id,
		Scalpel::ApplicationClipboardOperation::Paste,
		Scalpel::ApplicationClipboardStatus::Complete, "older");
	CHECK(editor.Text() == "replacement");
	editor.HandleClipboardResult(current.front().id,
		Scalpel::ApplicationClipboardOperation::Paste,
		Scalpel::ApplicationClipboardStatus::Complete, "current");
	CHECK(editor.Text() == "currentreplacement");
}

TEST_CASE("production editor publishes and clears primary selection") {
	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("primary selection");

	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>('A'),
		Scintilla::KeyMod::Ctrl, {}, 1, true});
	std::vector<Scalpel::ApplicationPrimarySelectionRequest> requests =
		editor.TakePrimarySelectionRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests.front().operation ==
		Scalpel::ApplicationPrimarySelectionOperation::Publish);
	REQUIRE(requests.front().text);
	CHECK(*requests.front().text == "primary selection");

	editor.HandleKeyboardInput({Scintilla::Keys::Right, Scintilla::KeyMod::Norm,
		{}, 2, true});
	requests = editor.TakePrimarySelectionRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests.front().operation ==
		Scalpel::ApplicationPrimarySelectionOperation::Publish);
	CHECK_FALSE(requests.front().text.has_value());
}

TEST_CASE("production editor forgets rejected primary selection claims") {
	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("primary selection");

	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>('A'),
		Scintilla::KeyMod::Ctrl, {}, 1, true});
	const std::vector<Scalpel::ApplicationPrimarySelectionRequest> requests =
		editor.TakePrimarySelectionRequests();
	REQUIRE(requests.size() == 1);
	editor.HandlePrimarySelectionResult(requests.front().id,
		Scalpel::ApplicationPrimarySelectionOperation::Publish,
		Scalpel::ApplicationPrimarySelectionStatus::Unavailable);

	editor.HandleKeyboardInput({Scintilla::Keys::Right, Scintilla::KeyMod::Norm,
		{}, 2, true});
	CHECK(editor.TakePrimarySelectionRequests().empty());
	CHECK(editor.PrimarySelectionResults().back().status ==
		Scalpel::ApplicationPrimarySelectionStatus::Unavailable);
}

TEST_CASE("production editor ignores stale primary claim failures") {
	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("primary selection");

	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>('A'),
		Scintilla::KeyMod::Ctrl, {}, 1, true});
	const std::vector<Scalpel::ApplicationPrimarySelectionRequest> first =
		editor.TakePrimarySelectionRequests();
	REQUIRE(first.size() == 1);
	editor.HandleKeyboardInput({Scintilla::Keys::Right, Scintilla::KeyMod::Norm,
		{}, 2, true});
	REQUIRE(editor.TakePrimarySelectionRequests().size() == 1);
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>('A'),
		Scintilla::KeyMod::Ctrl, {}, 3, true});
	REQUIRE(editor.TakePrimarySelectionRequests().size() == 1);

	editor.HandlePrimarySelectionResult(first.front().id,
		Scalpel::ApplicationPrimarySelectionOperation::Publish,
		Scalpel::ApplicationPrimarySelectionStatus::Cancelled);
	editor.HandleKeyboardInput({Scintilla::Keys::Right, Scintilla::KeyMod::Norm,
		{}, 4, true});
	CHECK(editor.TakePrimarySelectionRequests().size() == 1);
}

TEST_CASE("production editor defers pointer selection ownership until release") {
	Scalpel::ApplicationEditor editor(320, 120);
	editor.LoadInitialBuffer("alpha beta gamma");
	editor.RenderFrame();

	editor.HandlePointerInput({Scalpel::PointerAction::Move,
		Scintilla::KeyMod::Norm, 20, 8, 0, 0, 10, -1});
	editor.HandlePointerInput({Scalpel::PointerAction::Press,
		Scintilla::KeyMod::Norm, 20, 8, 0, 0, 11, 0});
	editor.HandlePointerInput({Scalpel::PointerAction::Move,
		Scintilla::KeyMod::Norm, 100, 8, 0, 0, 12, -1});
	CHECK(editor.TakePrimarySelectionRequests().empty());

	editor.HandlePointerInput({Scalpel::PointerAction::Release,
		Scintilla::KeyMod::Norm, 100, 8, 0, 0, 13, 0});
	const std::vector<Scalpel::ApplicationPrimarySelectionRequest> requests =
		editor.TakePrimarySelectionRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests.front().operation ==
		Scalpel::ApplicationPrimarySelectionOperation::Publish);
	REQUIRE(requests.front().text);
	CHECK_FALSE(requests.front().text->empty());
}

TEST_CASE("production editor pastes primary text at the middle click") {
	Scalpel::ApplicationEditor editor(320, 120);
	editor.LoadInitialBuffer("alpha beta gamma");
	editor.RenderFrame();

	editor.HandlePointerInput({Scalpel::PointerAction::Press,
		Scintilla::KeyMod::Norm, 20, 8, 0, 0, 20, 2});
	const std::vector<Scalpel::ApplicationPrimarySelectionRequest> requests =
		editor.TakePrimarySelectionRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests.front().operation ==
		Scalpel::ApplicationPrimarySelectionOperation::Paste);

	editor.HandleKeyboardInput({Scintilla::Keys::End, Scintilla::KeyMod::Ctrl,
		{}, 21, true});
	editor.HandlePrimarySelectionResult(requests.front().id,
		Scalpel::ApplicationPrimarySelectionOperation::Paste,
		Scalpel::ApplicationPrimarySelectionStatus::Complete, "PRIMARY");
	CHECK(editor.Text().find("PRIMARY") < editor.Text().size() - 7);
	CHECK(editor.PrimarySelectionResults().back().status ==
		Scalpel::ApplicationPrimarySelectionStatus::Complete);
}

TEST_CASE("production editor reports primary text that was not pasted") {
	Scalpel::ApplicationEditor editor(320, 120);
	editor.LoadInitialBuffer("alpha beta gamma");
	editor.RenderFrame();

	editor.HandlePointerInput({Scalpel::PointerAction::Press,
		Scintilla::KeyMod::Norm, 20, 8, 0, 0, 20, 2});
	std::vector<Scalpel::ApplicationPrimarySelectionRequest> requests =
		editor.TakePrimarySelectionRequests();
	REQUIRE(requests.size() == 1);
	editor.HandlePrimarySelectionResult(requests.front().id,
		Scalpel::ApplicationPrimarySelectionOperation::Paste,
		Scalpel::ApplicationPrimarySelectionStatus::Complete, {});
	CHECK(editor.Text() == "alpha beta gamma");
	CHECK(editor.PrimarySelectionResults().back().status ==
		Scalpel::ApplicationPrimarySelectionStatus::NoText);

	editor.SetReadOnly(true);
	editor.HandlePointerInput({Scalpel::PointerAction::Press,
		Scintilla::KeyMod::Norm, 20, 8, 0, 0, 21, 2});
	requests = editor.TakePrimarySelectionRequests();
	REQUIRE(requests.size() == 1);
	editor.HandlePrimarySelectionResult(requests.front().id,
		Scalpel::ApplicationPrimarySelectionOperation::Paste,
		Scalpel::ApplicationPrimarySelectionStatus::Complete, "PRIMARY");
	CHECK(editor.Text() == "alpha beta gamma");
	CHECK(editor.PrimarySelectionResults().back().status ==
		Scalpel::ApplicationPrimarySelectionStatus::NotApplied);
}

TEST_CASE("production editor rejects stale primary paste results") {
	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("first");
	editor.RenderFrame();
	editor.HandlePointerInput({Scalpel::PointerAction::Press,
		Scintilla::KeyMod::Norm, 20, 8, 0, 0, 30, 2});
	const auto requests = editor.TakePrimarySelectionRequests();
	REQUIRE(requests.size() == 1);

	editor.LoadInitialBuffer("replacement");
	editor.HandlePrimarySelectionResult(requests.front().id,
		Scalpel::ApplicationPrimarySelectionOperation::Paste,
		Scalpel::ApplicationPrimarySelectionStatus::Complete, "stale");
	CHECK(editor.Text() == "replacement");
	CHECK(editor.PrimarySelectionResults().back().status ==
		Scalpel::ApplicationPrimarySelectionStatus::Superseded);

	editor.RenderFrame();
	editor.HandlePointerInput({Scalpel::PointerAction::Press,
		Scintilla::KeyMod::Norm, 20, 8, 0, 0, 31, 2});
	const auto editedRequest = editor.TakePrimarySelectionRequests();
	REQUIRE(editedRequest.size() == 1);
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>('X'),
		Scintilla::KeyMod::Norm, "x", 32, true});
	const std::string edited = editor.Text();
	editor.HandlePrimarySelectionResult(editedRequest.front().id,
		Scalpel::ApplicationPrimarySelectionOperation::Paste,
		Scalpel::ApplicationPrimarySelectionStatus::Complete, "late");
	CHECK(editor.Text() == edited);
	CHECK(editor.PrimarySelectionResults().back().status ==
		Scalpel::ApplicationPrimarySelectionStatus::Superseded);
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
