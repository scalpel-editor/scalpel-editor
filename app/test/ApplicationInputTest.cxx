#include "ApplicationTest.h"

#include "ScrollBar.h"

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
	editor.SetWrapMode(Scintilla::Wrap::None);
	editor.RenderFrame();
	const Scintilla::Line verticalBefore = editor.Scrollbars().vertical.position;

	editor.HandlePointerInput({Scalpel::PointerAction::Scroll, Scintilla::KeyMod::Norm,
		0, 0, 0, 10, 20, -1});
	CHECK(editor.Scrollbars().vertical.position > verticalBefore);
	editor.HandlePointerInput({Scalpel::PointerAction::Scroll, Scintilla::KeyMod::Norm,
		0, 0, 10, 0, 21, -1});
	CHECK(editor.Scrollbars().horizontal.position > 0);
}

namespace {

Scalpel::ScrollBarLayout CurrentScrollBars(Scalpel::ApplicationEditor &editor) {
	return Scalpel::LayoutScrollBars(editor.FrameWidth(), editor.FrameHeight(),
		editor.TopChromeInset(), editor.Scrollbars().vertical,
		editor.Scrollbars().horizontal);
}

void ApplyScrollBarRequest(Scalpel::ApplicationEditor &editor,
	const Scalpel::ScrollBarRequest &request) {
	if (request.kind == Scalpel::ScrollBarRequestKind::SetVertical) {
		editor.ScrollVerticalTo(request.position);
	} else if (request.kind == Scalpel::ScrollBarRequestKind::SetHorizontal) {
		editor.ScrollHorizontalTo(static_cast<int>(request.position));
	}
}

}

TEST_CASE("production editor scrollbar input pages and drags without selecting") {
	Scalpel::ApplicationEditor editor(240, 120);
	std::string text;
	for (int line = 0; line < 60; line++) {
		text += "scrollbar input line with some horizontal width\n";
	}
	editor.LoadInitialBuffer(text);
	editor.SetWrapMode(Scintilla::Wrap::None);
	editor.RenderFrame();
	editor.SetSel(0, 0);

	Scalpel::ScrollBarInteraction interaction;
	Scalpel::ScrollBarLayout layout = CurrentScrollBars(editor);
	REQUIRE(layout.vertical.enabled);

	const int trackX = static_cast<int>(
		(layout.vertical.track.left + layout.vertical.track.right) / 2);
	const int afterY = static_cast<int>(layout.vertical.track.bottom - 2);
	const Scalpel::ScrollBarPointerResult page =
		Scalpel::HandleScrollBarPointer(interaction, layout,
			{Scalpel::PointerAction::Press, Scintilla::KeyMod::Norm,
				static_cast<double>(trackX), static_cast<double>(afterY),
				0, 0, 1, 0});
	CHECK(page.consumed);
	ApplyScrollBarRequest(editor, page.request);
	CHECK(editor.Scrollbars().vertical.position > 0);
	// Track press must not move the caret or start a selection.
	CHECK(editor.GetSelectionStart() == 0);
	CHECK(editor.GetSelectionEnd() == 0);
	CHECK_FALSE(editor.WindowState().mouseCaptured);

	layout = CurrentScrollBars(editor);
	Scalpel::CancelScrollBarInteraction(interaction);
	const int thumbY = static_cast<int>(
		(layout.vertical.thumb.top + layout.vertical.thumb.bottom) / 2);
	const Scalpel::ScrollBarPointerResult press =
		Scalpel::HandleScrollBarPointer(interaction, layout,
			{Scalpel::PointerAction::Press, Scintilla::KeyMod::Norm,
				static_cast<double>(trackX), static_cast<double>(thumbY),
				0, 0, 2, 0});
	CHECK(press.consumed);
	CHECK(interaction.dragging);

	const Scalpel::ScrollBarPointerResult drag =
		Scalpel::HandleScrollBarPointer(interaction, layout,
			{Scalpel::PointerAction::Move, Scintilla::KeyMod::Norm,
				static_cast<double>(trackX),
				static_cast<double>(layout.vertical.track.bottom + 20),
				0, 0, 3, -1});
	CHECK(drag.consumed);
	ApplyScrollBarRequest(editor, drag.request);
	CHECK(editor.Scrollbars().vertical.position ==
		editor.Scrollbars().vertical.upperBound);

	// Active editor selection drag is not stolen: when no scrollbar drag is
	// active, a press in the client leaves the scrollbar model unconsumed.
	Scalpel::CancelScrollBarInteraction(interaction);
	layout = CurrentScrollBars(editor);
	const Scalpel::ScrollBarPointerResult client =
		Scalpel::HandleScrollBarPointer(interaction, layout,
			{Scalpel::PointerAction::Press, Scintilla::KeyMod::Norm,
				20, static_cast<double>(editor.EditorClientRectangle().top + 8),
				0, 0, 4, 0});
	CHECK_FALSE(client.consumed);
	editor.HandlePointerInput({Scalpel::PointerAction::Press, Scintilla::KeyMod::Norm,
		20, editor.EditorClientRectangle().top + 8, 0, 0, 4, 0});
	CHECK(editor.WindowState().mouseCaptured);
}

TEST_CASE("production editor scrollbar input wheel over bar uses that axis") {
	Scalpel::ApplicationEditor editor(240, 100);
	std::string text;
	for (int line = 0; line < 40; line++) {
		text += "wheel over bar line with horizontal room\n";
	}
	editor.LoadInitialBuffer(text);
	editor.SetWrapMode(Scintilla::Wrap::None);
	editor.RenderFrame();

	Scalpel::ScrollBarInteraction interaction;
	const Scalpel::ScrollBarLayout layout = CurrentScrollBars(editor);
	const int hX = static_cast<int>(
		(layout.horizontal.track.left + layout.horizontal.track.right) / 2);
	const int hY = static_cast<int>(
		(layout.horizontal.track.top + layout.horizontal.track.bottom) / 2);
	const Scintilla::Line verticalBefore = editor.Scrollbars().vertical.position;
	const Scalpel::ScrollBarPointerResult wheel =
		Scalpel::HandleScrollBarPointer(interaction, layout,
			{Scalpel::PointerAction::Scroll, Scintilla::KeyMod::Norm,
				static_cast<double>(hX), static_cast<double>(hY),
				0, 12, 1, -1});
	CHECK(wheel.consumed);
	ApplyScrollBarRequest(editor, wheel.request);
	CHECK(editor.Scrollbars().horizontal.position > 0);
	CHECK(editor.Scrollbars().vertical.position == verticalBefore);
}

TEST_CASE("production editor pointer wheel clamps horizontal to upper bound") {
	Scalpel::ApplicationEditor editor(240, 80);
	editor.LoadInitialBuffer("a long line that can scroll horizontally\n");
	editor.SetWrapMode(Scintilla::Wrap::None);
	editor.RenderFrame();
	const Scintilla::Line upper = editor.Scrollbars().horizontal.upperBound;
	REQUIRE(upper > 0);

	// Large horizontal deltas must stop at the advertised assumed width.
	for (int step = 0; step < 40; step++) {
		editor.HandlePointerInput({Scalpel::PointerAction::Scroll, Scintilla::KeyMod::Norm,
			0, 0, 100, 0, static_cast<uint32_t>(100 + step), -1});
	}
	CHECK(editor.Scrollbars().horizontal.position == upper);
	CHECK(editor.GetXOffset() == static_cast<int>(upper));
}
