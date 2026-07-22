#include <algorithm>
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

	editor.Resize(300, 160);
	CHECK(editor.FrameWidth() == 300);
	CHECK(editor.FrameHeight() == 160);
	CHECK_FALSE(editor.WindowState().invalidatedRectangles.empty());
	editor.RenderFrame();
	CHECK(editor.Scrollbars().changes > 0);
}

TEST_CASE("deferred application services fail visibly") {
	Scalpel::ApplicationEditor editor(200, 100);
	editor.LoadInitialBuffer("clipboard request");
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
