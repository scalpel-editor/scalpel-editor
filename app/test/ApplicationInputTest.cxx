#include "ApplicationTest.h"

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
	const Scintilla::Line verticalBefore = editor.Scrollbars().verticalPosition;

	editor.HandlePointerInput({Scalpel::PointerAction::Scroll, Scintilla::KeyMod::Norm,
		0, 0, 0, 10, 20, -1});
	CHECK(editor.Scrollbars().verticalPosition > verticalBefore);
	editor.HandlePointerInput({Scalpel::PointerAction::Scroll, Scintilla::KeyMod::Norm,
		0, 0, 10, 0, 21, -1});
	CHECK(editor.Scrollbars().horizontalPosition > 0);
}
