#include "ApplicationTest.h"

#include "MenuBar.h"

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
	// Resize rebuilds vertical range through ModifyScrollBars.
	CHECK(editor.Scrollbars().vertical.pageSize > 0);
}

TEST_CASE("production editor scrollbar metrics expose both axes") {
	Scalpel::ApplicationEditor editor(240, 80);
	std::string text;
	for (int line = 0; line < 40; line++) {
		text += "line with enough characters to grow the horizontal range\n";
	}
	editor.LoadInitialBuffer(text);
	editor.SetWrapMode(Scintilla::Wrap::None);
	editor.RenderFrame();

	const Scalpel::ScrollMetrics metrics = editor.Scrollbars();
	CHECK(metrics.vertical.visible);
	CHECK(metrics.vertical.pageSize > 0);
	CHECK(metrics.vertical.upperBound > 0);
	CHECK(metrics.vertical.pageIncrement ==
		std::max(Scintilla::Line{1}, metrics.vertical.pageSize - 1));
	CHECK(metrics.vertical.position == 0);

	CHECK(metrics.horizontal.visible);
	CHECK(metrics.horizontal.pageSize > 0);
	CHECK(metrics.horizontal.upperBound >= 0);
	CHECK(metrics.horizontal.pageIncrement ==
		std::max(1, static_cast<int>(metrics.horizontal.pageSize) / 3));
	// Default assumed width is large enough that a narrow client can scroll.
	CHECK(metrics.horizontal.upperBound > 0);

	editor.ScrollVerticalTo(metrics.vertical.upperBound + 50);
	CHECK(editor.Scrollbars().vertical.position ==
		editor.Scrollbars().vertical.upperBound);

	editor.ScrollHorizontalTo(
		static_cast<int>(editor.Scrollbars().horizontal.upperBound) + 500);
	CHECK(editor.Scrollbars().horizontal.position ==
		editor.Scrollbars().horizontal.upperBound);
	CHECK(editor.GetXOffset() ==
		static_cast<int>(editor.Scrollbars().horizontal.upperBound));

	editor.ScrollVerticalTo(0);
	editor.ScrollHorizontalTo(0);
	CHECK(editor.Scrollbars().vertical.position == 0);
	CHECK(editor.Scrollbars().horizontal.position == 0);

	// Wrapping clears horizontal range and hides that bar.
	editor.SetWrapMode(Scintilla::Wrap::Word);
	editor.RenderFrame();
	CHECK_FALSE(editor.Scrollbars().horizontal.visible);
	CHECK(editor.Scrollbars().horizontal.upperBound == 0);
	CHECK(editor.Scrollbars().vertical.visible);

	editor.SetWrapMode(Scintilla::Wrap::None);
	editor.RenderFrame();
	CHECK(editor.Scrollbars().horizontal.visible);
	CHECK(editor.Scrollbars().horizontal.upperBound > 0);
}

TEST_CASE("production editor document switching keeps independent text and save points") {
	Scalpel::ApplicationEditor editor(320, 180);
	const Scalpel::DocumentId first = editor.ActiveDocument();
	REQUIRE(first != 0);
	CHECK(editor.HasDocument(first));
	editor.LoadInitialBuffer("first body");
	CHECK_FALSE(editor.Modified());

	editor.HandleKeyboardInput({Scintilla::Keys::End, Scintilla::KeyMod::Norm,
		{}, 1, true});
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>(0),
		Scintilla::KeyMod::Norm, "!", 2, true});
	CHECK(editor.Modified());
	CHECK(editor.Text() == "first body!");

	const Scalpel::DocumentId second = editor.CreateDocument();
	REQUIRE(second != first);
	CHECK(editor.ActiveDocument() == first);
	CHECK(editor.HasDocument(second));
	CHECK(editor.Text(second).empty());
	CHECK_FALSE(editor.Modified(second));

	editor.ActivateDocument(second);
	CHECK(editor.ActiveDocument() == second);
	CHECK(editor.Text().empty());
	CHECK_FALSE(editor.Modified());
	// By-ID reads still see the inactive first document.
	CHECK(editor.Text(first) == "first body!");
	CHECK(editor.Modified(first));

	editor.LoadInitialBuffer("second body");
	CHECK_FALSE(editor.Modified());
	editor.HandleKeyboardInput({Scintilla::Keys::End, Scintilla::KeyMod::Norm,
		{}, 3, true});
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>(0),
		Scintilla::KeyMod::Norm, "?", 4, true});
	CHECK(editor.Text() == "second body?");
	CHECK(editor.Modified());

	// Mark the inactive first document saved without activating it.
	editor.MarkSaved(first);
	CHECK_FALSE(editor.Modified(first));
	CHECK(editor.Modified()); // active second still dirty

	editor.ActivateDocument(first);
	CHECK(editor.ActiveDocument() == first);
	CHECK(editor.Text() == "first body!");
	CHECK_FALSE(editor.Modified());
	CHECK(editor.Text(second) == "second body?");
	CHECK(editor.Modified(second));
}

TEST_CASE("production editor document switching keeps independent undo histories") {
	Scalpel::ApplicationEditor editor(320, 180);
	const Scalpel::DocumentId first = editor.ActiveDocument();
	editor.LoadInitialBuffer("alpha");
	editor.HandleKeyboardInput({Scintilla::Keys::End, Scintilla::KeyMod::Norm,
		{}, 1, true});
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>(0),
		Scintilla::KeyMod::Norm, "1", 2, true});
	CHECK(editor.Text() == "alpha1");

	const Scalpel::DocumentId second = editor.CreateDocument();
	editor.ActivateDocument(second);
	editor.LoadInitialBuffer("beta");
	editor.HandleKeyboardInput({Scintilla::Keys::End, Scintilla::KeyMod::Norm,
		{}, 3, true});
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>(0),
		Scintilla::KeyMod::Norm, "2", 4, true});
	CHECK(editor.Text() == "beta2");

	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>('Z'),
		Scintilla::KeyMod::Ctrl, {}, 5, true});
	CHECK(editor.Text() == "beta");
	CHECK(editor.Text(first) == "alpha1");

	editor.ActivateDocument(first);
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>('Z'),
		Scintilla::KeyMod::Ctrl, {}, 6, true});
	CHECK(editor.Text() == "alpha");
	CHECK(editor.Text(second) == "beta");
}

TEST_CASE("production editor document switching restores selection and scroll") {
	Scalpel::ApplicationEditor editor(240, 100);
	const Scalpel::DocumentId first = editor.ActiveDocument();
	std::string manyLines;
	for (int line = 0; line < 40; ++line) {
		manyLines += "line " + std::to_string(line) + " horizontal overflow text\n";
	}
	editor.LoadInitialBuffer(manyLines);
	editor.RenderFrame();
	editor.SetSel(5, 12);
	editor.SetFirstVisibleLine(8);
	editor.SetXOffset(40);
	CHECK(editor.GetSelectionStart() == 5);
	CHECK(editor.GetSelectionEnd() == 12);
	CHECK(editor.GetFirstVisibleLine() == 8);
	CHECK(editor.GetXOffset() == 40);

	const Scalpel::DocumentId second = editor.CreateDocument();
	editor.ActivateDocument(second);
	editor.LoadInitialBuffer("other\nsecond\nthird\n");
	editor.RenderFrame();
	editor.SetSel(0, 5);
	editor.SetFirstVisibleLine(1);
	editor.SetXOffset(0);
	CHECK(editor.GetSelectionStart() == 0);
	CHECK(editor.GetSelectionEnd() == 5);
	CHECK(editor.GetFirstVisibleLine() == 1);

	editor.ActivateDocument(first);
	CHECK(editor.GetSelectionStart() == 5);
	CHECK(editor.GetSelectionEnd() == 12);
	CHECK(editor.GetFirstVisibleLine() == 8);
	CHECK(editor.GetXOffset() == 40);

	editor.ActivateDocument(second);
	CHECK(editor.GetSelectionStart() == 0);
	CHECK(editor.GetSelectionEnd() == 5);
	CHECK(editor.GetFirstVisibleLine() == 1);
	CHECK(editor.GetXOffset() == 0);
}

TEST_CASE("production editor document switching restores wrapped scroll") {
	Scalpel::ApplicationEditor editor(160, 100);
	editor.SetWrapMode(Scintilla::Wrap::Word);
	const Scalpel::DocumentId first = editor.ActiveDocument();
	std::string wrappedLine;
	for (int word = 0; word < 80; ++word) {
		wrappedLine += "wrapped ";
	}
	editor.LoadInitialBuffer(wrappedLine);
	editor.RenderFrame();
	editor.SetFirstVisibleLine(6);
	REQUIRE(editor.GetFirstVisibleLine() == 6);

	const Scalpel::DocumentId second = editor.CreateDocument();
	editor.ActivateDocument(second);
	editor.LoadInitialBuffer("other");
	editor.RenderFrame();

	editor.ActivateDocument(first);
	editor.RenderFrame();
	CHECK(editor.GetFirstVisibleLine() == 6);
}

TEST_CASE("production editor document switching releases closed references") {
	Scalpel::ApplicationEditor editor(200, 100);
	const Scalpel::DocumentId first = editor.ActiveDocument();
	editor.LoadInitialBuffer("keep");
	const Scalpel::DocumentId second = editor.CreateDocument();
	editor.ActivateDocument(second);
	editor.LoadInitialBuffer("drop me");
	const Scalpel::DocumentId third = editor.CreateDocument();
	editor.ActivateDocument(third);
	editor.LoadInitialBuffer("active");

	editor.CloseDocument(second);
	CHECK_FALSE(editor.HasDocument(second));
	CHECK_THROWS_WITH(editor.Text(second),
		"ApplicationEditor::Text requires a retained document");
	CHECK_THROWS_WITH(editor.CloseDocument(third),
		"ApplicationEditor::CloseDocument cannot close the active document");
	CHECK_THROWS_WITH(editor.ActivateDocument(second),
		"ApplicationEditor::ActivateDocument requires a retained document");

	editor.ActivateDocument(first);
	editor.CloseDocument(third);
	CHECK_FALSE(editor.HasDocument(third));
	CHECK(editor.Text() == "keep");
	CHECK(editor.HasDocument(first));
}

TEST_CASE("production editor document switching supersedes asynchronous pastes") {
	Scalpel::ApplicationEditor editor(200, 100);
	const Scalpel::DocumentId first = editor.ActiveDocument();
	editor.LoadInitialBuffer("first");
	editor.RequestClipboardPaste();
	const auto pasteRequests = editor.TakeClipboardRequests();
	REQUIRE(pasteRequests.size() == 1);

	editor.RenderFrame();
	editor.HandlePointerInput({Scalpel::PointerAction::Press,
		Scintilla::KeyMod::Norm, 20, 8, 0, 0, 1, 2});
	const auto primaryRequests = editor.TakePrimarySelectionRequests();
	REQUIRE(primaryRequests.size() == 1);

	const Scalpel::DocumentId second = editor.CreateDocument();
	editor.ActivateDocument(second);
	editor.LoadInitialBuffer("second");

	editor.HandleClipboardResult(pasteRequests.front().id,
		Scalpel::ApplicationClipboardOperation::Paste,
		Scalpel::ApplicationClipboardStatus::Complete, "stale-clip");
	CHECK(editor.Text() == "second");
	CHECK(editor.Text(first) == "first");
	CHECK(editor.ClipboardResults().back().status ==
		Scalpel::ApplicationClipboardStatus::Superseded);

	editor.HandlePrimarySelectionResult(primaryRequests.front().id,
		Scalpel::ApplicationPrimarySelectionOperation::Paste,
		Scalpel::ApplicationPrimarySelectionStatus::Complete, "stale-primary");
	CHECK(editor.Text() == "second");
	CHECK(editor.Text(first) == "first");
	CHECK(editor.PrimarySelectionResults().back().status ==
		Scalpel::ApplicationPrimarySelectionStatus::Superseded);
}

TEST_CASE("production editor document switching cancels tentative IME") {
	Scalpel::ApplicationEditor editor(240, 120);
	const Scalpel::DocumentId first = editor.ActiveDocument();
	editor.LoadInitialBuffer("base");

	Scalpel::ApplicationTextInputBatch preedit;
	preedit.preedit = Scalpel::ApplicationTextInputPreedit{
		"\xC3\xA9", 2, 2};
	editor.HandleTextInputBatch(preedit);
	CHECK(editor.Text() == "\xC3\xA9" "base");
	CHECK(editor.ImeIndicatorAt(0) != 0);

	const Scalpel::DocumentId second = editor.CreateDocument();
	editor.ActivateDocument(second);
	CHECK(editor.Text(first) == "base");

	editor.ActivateDocument(first);
	CHECK(editor.Text() == "base");
	CHECK(editor.ImeIndicatorAt(0) == 0);
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

TEST_CASE("production editor top chrome insets the client and keeps frame origin") {
	using Scintilla::Internal::PRectangle;

	Scalpel::ApplicationEditor editor(200, 100);
	editor.LoadInitialBuffer("line one\nline two\nline three\n");
	(void)editor.TakeFrameDamage();

	CHECK(editor.TopChromeInset() == 0);
	CHECK(editor.FrameRectangle() == PRectangle::FromInts(0, 0, 200, 100));

	const int inset = 28;
	editor.SetTopChromeInset(inset);
	CHECK(editor.TopChromeInset() == inset);
	CHECK(editor.TopChromeRectangle() == PRectangle::FromInts(0, 0, 200, inset));
	CHECK(editor.FrameRectangle() == PRectangle::FromInts(0, 0, 200, 100));

	// Partial editor damage is clipped to the inset client, not the chrome band.
	const PRectangle editorDamage = PRectangle::FromInts(10, inset + 5, 40, inset + 25);
	editor.RenderFrame({editorDamage});
	CHECK(editor.LastPaintRectangle() == editorDamage);

	// Chrome-only damage does not expand editor paint into the strip.
	(void)editor.TakeFrameDamage();
	editor.RenderFrame({PRectangle::FromInts(0, 0, 200, inset)});
	CHECK(editor.LastPaintRectangle().Width() == 0);
	CHECK(editor.LastPaintRectangle().Height() == 0);
}

TEST_CASE("production editor top chrome hit testing uses frame coordinates") {
	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("abcdef\nghijkl\n");
	const int inset = 28;
	editor.SetTopChromeInset(inset);
	editor.SetKeyboardFocus(true);
	editor.SetSel(0, 0);
	editor.RenderFrame();

	// Caret geometry for the first character is below the chrome band.
	const auto firstCaret = editor.TakeTextInputState();
	REQUIRE(firstCaret.has_value());
	CHECK(firstCaret->cursorRectangle.y >= inset);
	REQUIRE(firstCaret->cursorRectangle.height > 0);
	const int lineHeight = firstCaret->cursorRectangle.height;

	// Click using full-frame y at the first-line caret (no shell-side translation).
	const int textX = firstCaret->cursorRectangle.x + 2;
	const int firstLineY = firstCaret->cursorRectangle.y + lineHeight / 2;
	editor.HandlePointerInput({Scalpel::PointerAction::Press,
		Scintilla::KeyMod::Norm, static_cast<double>(textX),
		static_cast<double>(firstLineY), 0, 0, 1, 0});
	editor.HandlePointerInput({Scalpel::PointerAction::Release,
		Scintilla::KeyMod::Norm, static_cast<double>(textX),
		static_cast<double>(firstLineY), 0, 0, 2, 0});
	CHECK(editor.GetCurrentPos() < 6);
	CHECK(editor.GetCurrentPos() >= 0);

	// Second display line is one lineHeight below the first in frame coordinates.
	const int secondLineY = firstCaret->cursorRectangle.y + lineHeight +
		lineHeight / 2;
	editor.HandlePointerInput({Scalpel::PointerAction::Press,
		Scintilla::KeyMod::Norm, static_cast<double>(textX),
		static_cast<double>(secondLineY), 0, 0, 3, 0});
	editor.HandlePointerInput({Scalpel::PointerAction::Release,
		Scintilla::KeyMod::Norm, static_cast<double>(textX),
		static_cast<double>(secondLineY), 0, 0, 4, 0});
	CHECK(editor.GetCurrentPos() >= 7);
}

TEST_CASE("production editor top chrome caret geometry is below the inset") {
	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("caret\n");
	const int inset = 28;
	editor.SetTopChromeInset(inset);
	editor.SetKeyboardFocus(true);
	editor.SetSel(0, 0);
	editor.RenderFrame();

	const auto state = editor.TakeTextInputState();
	REQUIRE(state.has_value());
	CHECK(state->cursorRectangle.y >= inset);
	CHECK(state->cursorRectangle.height > 0);
}

TEST_CASE("production editor top chrome offsets range invalidation") {
	using Scintilla::Internal::PRectangle;

	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("one\ntwo\nthree\n");
	const int inset = 28;
	editor.SetTopChromeInset(inset);
	editor.RenderFrame();
	(void)editor.TakeFrameDamage();

	editor.SetSel(0, 1);
	const auto damage = editor.TakeFrameDamage();
	REQUIRE_FALSE(damage.empty());
	CHECK(std::any_of(damage.begin(), damage.end(),
		[inset](const PRectangle &rectangle) {
			return rectangle.top == inset && rectangle.bottom > inset;
		}));
}

TEST_CASE("production editor top chrome permanent painter is damage-aware") {
	using Scintilla::Internal::ColourRGBA;
	using Scintilla::Internal::Fill;
	using Scintilla::Internal::PRectangle;
	using Scintilla::Internal::Surface;

	Scalpel::ApplicationEditor editor(120, 80);
	editor.LoadInitialBuffer("base\n");
	const int inset = 20;
	editor.SetTopChromeInset(inset);
	(void)editor.TakeFrameDamage();

	int chromePaints = 0;
	editor.SetPermanentChromePainter(
		[&](Surface &surface, int width, int height) {
			++chromePaints;
			CHECK(width == 120);
			CHECK(height == 80);
			surface.FillRectangle(PRectangle::FromInts(0, 0, width, inset),
				Fill(ColourRGBA(0x20, 0x40, 0x80, 0xff)));
		});

	// Editor-only damage: chrome painter must not run.
	editor.RenderFrame({PRectangle::FromInts(10, inset + 5, 30, inset + 20)});
	CHECK(chromePaints == 0);

	// Chrome damage: chrome painter runs; editor paint is not expanded.
	editor.RenderFrame({PRectangle::FromInts(0, 0, 120, inset)});
	CHECK(chromePaints == 1);
	CHECK(editor.LastPaintRectangle().Width() == 0);

	const auto pixels = editor.FramePixels();
	REQUIRE(pixels.size() == 120U * 80U * 4U);
	const size_t chromeSample = (static_cast<size_t>(inset / 2) * 120U + 10U) * 4U;
	CHECK(pixels[chromeSample + 0] == 0x20);
	CHECK(pixels[chromeSample + 1] == 0x40);
	CHECK(pixels[chromeSample + 2] == 0x80);

	// Full invalidation still paints both bands.
	editor.InvalidateClient();
	editor.RenderFrame();
	CHECK(chromePaints == 2);
}

TEST_CASE("production editor top chrome overlay paints above tabs and forces full client") {
	using Scintilla::Internal::ColourRGBA;
	using Scintilla::Internal::Fill;
	using Scintilla::Internal::PRectangle;
	using Scintilla::Internal::Surface;

	Scalpel::ApplicationEditor editor(100, 60);
	editor.LoadInitialBuffer("under\n");
	const int inset = 16;
	editor.SetTopChromeInset(inset);
	(void)editor.TakeFrameDamage();

	int chromePaints = 0;
	int overlayPaints = 0;
	editor.SetPermanentChromePainter(
		[&](Surface &surface, int width, int) {
			++chromePaints;
			surface.FillRectangle(PRectangle::FromInts(0, 0, width, inset),
				Fill(ColourRGBA(0x00, 0xff, 0x00, 0xff)));
		});
	editor.SetOverlayPainter(
		[&](Surface &surface, int width, int height) {
			++overlayPaints;
			CHECK(width == 100);
			CHECK(height == 60);
			// Opaque mark over the strip center so overlay is above chrome.
			surface.FillRectangle(PRectangle::FromInts(40, 2, 60, 14),
				Fill(ColourRGBA(0xff, 0x00, 0x00, 0xff)));
		});

	// Partial editor damage expands to the full editor client under an overlay.
	editor.RenderFrame({PRectangle::FromInts(10, inset + 5, 20, inset + 15)});
	CHECK(chromePaints == 1);
	CHECK(overlayPaints == 1);
	CHECK(editor.LastPaintRectangle() ==
		PRectangle::FromInts(0, inset, 100, 60));

	const auto pixels = editor.FramePixels();
	REQUIRE(pixels.size() == 100U * 60U * 4U);
	// Overlay red marker at strip y=8.
	const size_t overlaySample = (8U * 100U + 50U) * 4U;
	CHECK(pixels[overlaySample + 0] == 0xff);
	CHECK(pixels[overlaySample + 1] == 0x00);
	CHECK(pixels[overlaySample + 2] == 0x00);
	// Chrome green remains where overlay did not cover (x=5, y=8).
	const size_t chromeSample = (8U * 100U + 5U) * 4U;
	CHECK(pixels[chromeSample + 0] == 0x00);
	CHECK(pixels[chromeSample + 1] == 0xff);
	CHECK(pixels[chromeSample + 2] == 0x00);
}

TEST_CASE("production editor top chrome respects framebuffer scale") {
	using Scintilla::Internal::ColourRGBA;
	using Scintilla::Internal::Fill;
	using Scintilla::Internal::PRectangle;
	using Scintilla::Internal::Surface;

	Scalpel::ApplicationEditor editor(100, 50);
	editor.LoadInitialBuffer("scale\n");
	const int inset = 10;
	editor.SetTopChromeInset(inset);
	editor.SetFrameBufferSize(200, 100);
	(void)editor.TakeFrameDamage();

	editor.SetPermanentChromePainter(
		[&](Surface &surface, int width, int height) {
			CHECK(width == 100);
			CHECK(height == 50);
			surface.FillRectangle(PRectangle::FromInts(0, 0, width, inset),
				Fill(ColourRGBA(0x11, 0x22, 0x33, 0xff)));
		});
	editor.RenderFrame({PRectangle::FromInts(0, 0, 100, 50)});

	// Logical FramePixels size is unchanged by buffer scale.
	const auto pixels = editor.FramePixels();
	REQUIRE(pixels.size() == 100U * 50U * 4U);
	const size_t sample = (4U * 100U + 4U) * 4U;
	CHECK(pixels[sample + 0] == 0x11);
	CHECK(editor.BufferWidth() == 200);
	CHECK(editor.BufferHeight() == 100);
}

TEST_CASE("production editor top chrome InvalidateTopChrome damages only the band") {
	Scalpel::ApplicationEditor editor(160, 90);
	const int inset = 24;
	editor.SetTopChromeInset(inset);
	(void)editor.TakeFrameDamage();
	CHECK_FALSE(editor.NeedsRedraw());

	editor.InvalidateTopChrome();
	CHECK(editor.NeedsRedraw());
	const auto damage = editor.TakeFrameDamage();
	REQUIRE_FALSE(damage.empty());
	CHECK(std::find(damage.begin(), damage.end(),
		Scintilla::Internal::PRectangle::FromInts(0, 0, 160, inset)) !=
		damage.end());
	// No full-frame invalidation from InvalidateTopChrome alone.
	CHECK(std::find(damage.begin(), damage.end(),
		Scintilla::Internal::PRectangle::FromInts(0, 0, 160, 90)) ==
		damage.end());
}

TEST_CASE("production editor menu overlay expands damage and paints dropdown above chrome") {
	using Scintilla::Internal::PRectangle;
	using Scintilla::Internal::Surface;
	using Scalpel::LayoutMenuBar;
	using Scalpel::MenuBarHeight;
	using Scalpel::MenuBarLayout;
	using Scalpel::MenuBarModel;
	using Scalpel::MenuBarPainter;
	using Scalpel::ApplicationMenu;

	const int menuH = MenuBarHeight();
	const int stripH = 28;
	const int inset = menuH + stripH;
	Scalpel::ApplicationEditor editor(300, 180);
	editor.LoadInitialBuffer("menu overlay\n");
	editor.SetTopChromeInset(inset);
	(void)editor.TakeFrameDamage();

	MenuBarPainter menuPainter;
	MenuBarModel menuModel;
	menuModel.openMenu = ApplicationMenu::File;

	int chromePaints = 0;
	int overlayPaints = 0;
	editor.SetPermanentChromePainter(
		[&](Surface &surface, int width, int height) {
			++chromePaints;
			const MenuBarLayout layout =
				LayoutMenuBar(width, height, menuModel);
			menuPainter.PaintBar(surface, layout, menuModel);
			// Opaque tab-band stand-in so the dropdown must cover it.
			surface.FillRectangle(
				PRectangle::FromInts(0, menuH, width, inset),
				Scintilla::Internal::Fill(
					Scintilla::Internal::ColourRGBA(0x00, 0xaa, 0x00, 0xff)));
			(void)height;
		});
	editor.SetOverlayPainter(
		[&](Surface &surface, int width, int height) {
			++overlayPaints;
			const MenuBarLayout layout =
				LayoutMenuBar(width, height, menuModel);
			menuPainter.PaintDropdown(surface, layout, menuModel);
			CHECK(width == 300);
			CHECK(height == 180);
		});

	// Partial editor damage expands to the full client while the menu overlay is set.
	editor.RenderFrame({PRectangle::FromInts(12, inset + 8, 24, inset + 20)});
	CHECK(chromePaints == 1);
	CHECK(overlayPaints == 1);
	CHECK(editor.LastPaintRectangle() ==
		PRectangle::FromInts(0, inset, 300, 180));

	const MenuBarLayout layout = LayoutMenuBar(300, 180, menuModel);
	REQUIRE(layout.dropdown.Width() > 0);
	REQUIRE(layout.dropdown.Height() > 0);
	const int sampleX =
		static_cast<int>((layout.dropdown.left + layout.dropdown.right) / 2);
	const int sampleY =
		static_cast<int>((layout.dropdown.top + layout.dropdown.bottom) / 2);
	// Dropdown starts below the menu bar and covers the tab-band stand-in.
	REQUIRE(sampleY >= menuH);

	const auto pixels = editor.FramePixels();
	REQUIRE(pixels.size() == 300U * 180U * 4U);
	const size_t offset =
		(static_cast<size_t>(sampleY) * 300U + static_cast<size_t>(sampleX)) *
		4U;
	// Dropdown is not the green tab stand-in.
	const bool isGreenStandIn = pixels[offset + 0] == 0x00 &&
		pixels[offset + 1] == 0xaa && pixels[offset + 2] == 0x00;
	CHECK_FALSE(isGreenStandIn);
	CHECK(pixels[offset + 3] == 0xff);
}

TEST_CASE("production editor menu overlay clear removes dropdown without stale pixels") {
	using Scintilla::Internal::PRectangle;
	using Scintilla::Internal::Surface;
	using Scalpel::LayoutMenuBar;
	using Scalpel::MenuBarHeight;
	using Scalpel::MenuBarLayout;
	using Scalpel::MenuBarModel;
	using Scalpel::MenuBarPainter;
	using Scalpel::ApplicationMenu;

	const int menuH = MenuBarHeight();
	const int inset = menuH + 24;
	Scalpel::ApplicationEditor editor(280, 160);
	editor.LoadInitialBuffer("clear menu\n");
	editor.SetTopChromeInset(inset);
	(void)editor.TakeFrameDamage();

	MenuBarPainter menuPainter;
	MenuBarModel menuModel;
	menuModel.openMenu = ApplicationMenu::Edit;

	editor.SetPermanentChromePainter(
		[&](Surface &surface, int width, int height) {
			const MenuBarLayout layout =
				LayoutMenuBar(width, height, menuModel);
			menuPainter.PaintBar(surface, layout, menuModel);
			surface.FillRectangle(
				PRectangle::FromInts(0, menuH, width, inset),
				Scintilla::Internal::Fill(
					Scintilla::Internal::ColourRGBA(0x20, 0x20, 0x20, 0xff)));
			(void)height;
		});

	const MenuBarLayout openLayout = LayoutMenuBar(280, 160, menuModel);
	REQUIRE(openLayout.dropdown.Width() > 0);
	const int sampleX = static_cast<int>(
		(openLayout.dropdown.left + openLayout.dropdown.right) / 2);
	const int sampleY = static_cast<int>(
		(openLayout.dropdown.top + openLayout.dropdown.bottom) / 2);

	editor.SetOverlayPainter(
		[&](Surface &surface, int width, int height) {
			const MenuBarLayout layout =
				LayoutMenuBar(width, height, menuModel);
			menuPainter.PaintDropdown(surface, layout, menuModel);
			(void)height;
		});
	editor.RenderFrame({PRectangle::FromInts(0, 0, 280, 160)});
	const auto withMenu = editor.FramePixels();
	const size_t offset =
		(static_cast<size_t>(sampleY) * 280U + static_cast<size_t>(sampleX)) *
		4U;
	REQUIRE(offset + 3 < withMenu.size());
	const uint8_t menuR = withMenu[offset];
	const uint8_t menuG = withMenu[offset + 1];
	const uint8_t menuB = withMenu[offset + 2];

	// Close the menu and clear the overlay; queued full-frame damage replaces
	// both the part over the tab band and the part over the editor client.
	menuModel.openMenu.reset();
	editor.SetOverlayPainter(nullptr);
	editor.InvalidateFrame();
	const auto clearDamage = editor.TakeFrameDamage();
	REQUIRE(clearDamage.size() == 1);
	CHECK(clearDamage.front() == PRectangle::FromInts(0, 0, 280, 160));
	editor.RenderFrame(clearDamage);
	const auto cleared = editor.FramePixels();
	REQUIRE(offset + 3 < cleared.size());
	// The sample is no longer the open-dropdown fill.
	const bool changed = cleared[offset] != menuR ||
		cleared[offset + 1] != menuG || cleared[offset + 2] != menuB;
	CHECK(changed);
}
