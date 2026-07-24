#include "ApplicationTest.h"

TEST_CASE("production editor host constructs and renders its initial buffer") {
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("scalpel-editor\nvisible text\n");

	CHECK(editor.Text() == "scalpel-editor\nvisible text\n");
	CHECK_FALSE(editor.Modified());
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

TEST_CASE("production editor host shows line numbers and a text-left gap") {
	Scalpel::ApplicationEditor editor(320, 180);

	CHECK(editor.TextLeftGap() == 24);
	CHECK(editor.LineNumberMarginWidth() > editor.TextLeftGap());
	CHECK(editor.LineCount() == 1);
	// Gutter is monospace; buffer text stays on the default proportional face.
	CHECK(editor.StyleFontName(static_cast<int>(Scintilla::StylesCommon::LineNumber)) ==
		"monospace");
	CHECK(editor.StyleFontName(0) == "system-ui");
	CHECK(editor.StyleFontName(static_cast<int>(Scintilla::StylesCommon::Default)) ==
		"system-ui");
}

TEST_CASE("production editor line-number margin tracks line-count digits") {
	Scalpel::ApplicationEditor editor(320, 180);
	std::string text;
	for (int line = 1; line < 99; ++line) {
		text += "line\n";
	}
	text += "line";
	editor.LoadInitialBuffer(text);

	REQUIRE(editor.LineCount() == 99);
	const int twoDigitWidth = editor.LineNumberMarginWidth();
	CHECK(twoDigitWidth > editor.TextLeftGap());

	// Grow past the 99/100 digit boundary at the document end.
	editor.HandleKeyboardInput({Scintilla::Keys::End, Scintilla::KeyMod::Ctrl, {}, 1, true});
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>(0), Scintilla::KeyMod::Norm,
		"\nline", 2, true});
	CHECK(editor.LineCount() == 100);
	CHECK(editor.LineNumberMarginWidth() > twoDigitWidth);

	// Shrink back across the boundary. Four character deletions leave a trailing
	// empty line still numbered by the gutter; one more removes that line.
	for (int character = 0; character < 4; ++character) {
		editor.HandleKeyboardInput({Scintilla::Keys::Back, Scintilla::KeyMod::Norm,
			{}, static_cast<unsigned>(3 + character), true});
	}
	CHECK(editor.LineCount() == 100);
	CHECK(editor.LineNumberMarginWidth() > twoDigitWidth);

	editor.HandleKeyboardInput({Scintilla::Keys::Back, Scintilla::KeyMod::Norm,
		{}, 7, true});
	CHECK(editor.LineCount() == 99);
	CHECK(editor.LineNumberMarginWidth() == twoDigitWidth);
}

TEST_CASE("production editor load marks the buffer clean and edits dirty it") {
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("clean");
	CHECK_FALSE(editor.Modified());
	CHECK(editor.Text() == "clean");

	editor.HandleKeyboardInput({Scintilla::Keys::End, Scintilla::KeyMod::Norm,
		{}, 1, true});
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>(0),
		Scintilla::KeyMod::Norm, "x", 2, true});
	CHECK(editor.Modified());
	CHECK(editor.Text() == "cleanx");

	editor.MarkSaved();
	CHECK_FALSE(editor.Modified());
	CHECK(editor.Text() == "cleanx");

	editor.LoadInitialBuffer("replacement");
	CHECK_FALSE(editor.Modified());
	CHECK(editor.Text() == "replacement");
}

TEST_CASE("production editor overlay painter draws after Paint") {
	using Scintilla::Internal::ColourRGBA;
	using Scintilla::Internal::Fill;
	using Scintilla::Internal::PRectangle;
	using Scintilla::Internal::Surface;

	Scalpel::ApplicationEditor editor(80, 60);
	editor.LoadInitialBuffer("base\n");
	// Clear startup damage so the overlay test owns the paint path.
	(void)editor.TakeFrameDamage();

	const ColourRGBA marker(0xff, 0x00, 0x00, 0xff);
	const PRectangle markRect = PRectangle::FromInts(10, 10, 30, 30);
	bool painterCalled = false;
	editor.SetOverlayPainter(
		[&](Surface &surface, int width, int height) {
			painterCalled = true;
			CHECK(width == 80);
			CHECK(height == 60);
			surface.FillRectangle(markRect, Fill(marker));
		});
	editor.RenderFrame({PRectangle::FromInts(0, 0, 80, 60)});
	CHECK(painterCalled);

	const std::vector<uint8_t> pixels = editor.FramePixels();
	REQUIRE(pixels.size() == 80U * 60U * 4U);
	// FramePixels are top-left origin RGBA; sample the filled marker center.
	const size_t sampleX = 20;
	const size_t sampleY = 20;
	const size_t offset = (sampleY * 80U + sampleX) * 4U;
	CHECK(pixels[offset + 0] == 0xff);
	CHECK(pixels[offset + 1] == 0x00);
	CHECK(pixels[offset + 2] == 0x00);
}

TEST_CASE("production editor overlay expands partial damage to full client") {
	using Scintilla::Internal::PRectangle;
	using Scintilla::Internal::Surface;

	Scalpel::ApplicationEditor editor(80, 60);
	editor.LoadInitialBuffer("base\n");
	(void)editor.TakeFrameDamage();

	bool painterCalled = false;
	editor.SetOverlayPainter(
		[&](Surface &, int, int) {
			painterCalled = true;
		});
	// Partial damage must still paint the full client while an overlay is set.
	editor.RenderFrame({PRectangle::FromInts(10, 10, 20, 20)});
	CHECK(painterCalled);
	CHECK(editor.LastPaintRectangle() ==
		PRectangle::FromInts(0, 0, 80, 60));
}

TEST_CASE("production editor without overlay painter is unchanged") {
	Scalpel::ApplicationEditor editor(80, 60);
	editor.LoadInitialBuffer("plain\n");
	editor.RenderFrame();
	const std::vector<uint8_t> pixels = editor.FramePixels();
	REQUIRE(pixels.size() == 80U * 60U * 4U);
	bool hasNonBackgroundPixel = false;
	for (size_t offset = 4; offset < pixels.size(); offset += 4) {
		if (!std::equal(pixels.begin(), pixels.begin() + 4, pixels.begin() + offset)) {
			hasNonBackgroundPixel = true;
			break;
		}
	}
	CHECK(hasNonBackgroundPixel);
}

TEST_CASE("production editor InvalidateClient marks the full client") {
	Scalpel::ApplicationEditor editor(100, 50);
	(void)editor.TakeFrameDamage();
	CHECK_FALSE(editor.NeedsRedraw());
	editor.InvalidateClient();
	CHECK(editor.NeedsRedraw());
	const auto damage = editor.TakeFrameDamage();
	REQUIRE_FALSE(damage.empty());
	CHECK(std::find(damage.begin(), damage.end(),
		Scintilla::Internal::PRectangle::FromInts(0, 0, 100, 50)) !=
		damage.end());
}

TEST_CASE("production editor host rejects presentation without a window surface") {
	Scalpel::ApplicationEditor editor(320, 180);

	CHECK_THROWS_WITH(editor.PresentFrame(),
		"ApplicationEditor::PresentFrame requires a window surface");
	CHECK(editor.FramePixels().empty());
}

TEST_CASE("production editor captures damage before bounded painting") {
	Scalpel::ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("damage\n");
	const auto captured = editor.TakeFrameDamage();
	REQUIRE_FALSE(captured.empty());
	CHECK_FALSE(editor.NeedsRedraw());

	editor.Resize(321, 180);
	CHECK(editor.NeedsRedraw());
	const Scintilla::Internal::PRectangle selected =
		Scintilla::Internal::PRectangle::FromInts(10, 15, 40, 35);
	editor.RenderFrame({selected});
	CHECK(editor.LastPaintRectangle() == selected);
	CHECK(editor.NeedsRedraw());

	const auto preserved = editor.TakeFrameDamage();
	REQUIRE_FALSE(preserved.empty());
	CHECK(std::find(preserved.begin(), preserved.end(),
		Scintilla::Internal::PRectangle::FromInts(0, 0, 321, 180)) !=
		preserved.end());
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

	editor.SetFrameBufferSize(360, 180);
	const size_t invalidationsBeforeResize = editor.WindowState().invalidatedRectangles.size();
	editor.Resize(300, 120);
	CHECK(editor.FrameWidth() == 300);
	CHECK(editor.FrameHeight() == 120);
	CHECK(editor.BufferWidth() == 360);
	CHECK(editor.BufferHeight() == 180);
	editor.SetFrameBufferSize(450, 180);
	CHECK(editor.FrameWidth() == 300);
	CHECK(editor.FrameHeight() == 120);
	CHECK(editor.BufferWidth() == 450);
	CHECK(editor.BufferHeight() == 180);
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
