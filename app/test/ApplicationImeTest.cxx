#include "ApplicationTest.h"

TEST_CASE("production editor exposes bounded text input state") {
	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("a\xC3\xA9z");

	auto state = editor.TakeTextInputState();
	REQUIRE(state.has_value());
	REQUIRE(state->surroundingText.has_value());
	CHECK(*state->surroundingText == "a\xC3\xA9z");
	CHECK(state->cursor == 0);
	CHECK(state->anchor == 0);
	CHECK(state->cursorRectangle.width == 1);
	CHECK(state->cursorRectangle.height > 0);
	CHECK_FALSE(editor.TakeTextInputState().has_value());

	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>('A'),
		Scintilla::KeyMod::Ctrl, {}, 1, true});
	state = editor.TakeTextInputState();
	REQUIRE(state.has_value());
	REQUIRE(state->surroundingText.has_value());
	CHECK(std::abs(state->cursor - state->anchor) == 4);

	editor.LoadInitialBuffer(std::string(4001, 'x'));
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>('A'),
		Scintilla::KeyMod::Ctrl, {}, 2, true});
	state = editor.TakeTextInputState();
	REQUIRE(state.has_value());
	CHECK_FALSE(state->surroundingText.has_value());
}

TEST_CASE("production editor replaces and cancels tentative IME text") {
	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("base");

	Scalpel::ApplicationTextInputBatch first;
	first.preedit = Scalpel::ApplicationTextInputPreedit{
		"\xC3\xA9", 2, 2};
	editor.HandleTextInputBatch(first);
	CHECK(editor.Text() == "\xC3\xA9" "base");
	CHECK(editor.ImeIndicatorAt(0) != 0);
	const auto surrounding = editor.TakeTextInputState();
	REQUIRE(surrounding.has_value());
	REQUIRE(surrounding->surroundingText.has_value());
	CHECK(*surrounding->surroundingText == "base");
	CHECK(surrounding->cursor == 0);

	Scalpel::ApplicationTextInputBatch replacement;
	replacement.preedit = Scalpel::ApplicationTextInputPreedit{
		"\xE6\x96\x87", 3, 3};
	editor.HandleTextInputBatch(replacement);
	CHECK(editor.Text() == "\xE6\x96\x87" "base");
	CHECK(editor.ImeIndicatorAt(0) != 0);

	Scalpel::ApplicationTextInputBatch cancellation;
	cancellation.cancel = true;
	editor.HandleTextInputBatch(cancellation);
	CHECK(editor.Text() == "base");
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>('Z'),
		Scintilla::KeyMod::Ctrl, {}, 3, true});
	CHECK(editor.Text() == "base");
}

TEST_CASE("production editor applies ordered IME deletion commit and preedit") {
	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("abcde");
	editor.HandleKeyboardInput({Scintilla::Keys::End,
		Scintilla::KeyMod::Norm, {}, 1, true});

	Scalpel::ApplicationTextInputBatch batch;
	batch.deletion = Scalpel::ApplicationTextInputDelete{2, 0};
	batch.commit = "X";
	batch.preedit = Scalpel::ApplicationTextInputPreedit{
		"\xE6\x96\x87", 3, 3};
	editor.HandleTextInputBatch(batch);
	CHECK(editor.Text() == "abcX\xE6\x96\x87");
	CHECK(editor.TakeTextInputState()->changeCause ==
		Scalpel::ApplicationTextChangeCause::InputMethod);

	Scalpel::ApplicationTextInputBatch cancellation;
	cancellation.cancel = true;
	editor.HandleTextInputBatch(cancellation);
	CHECK(editor.Text() == "abcX");
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>('Z'),
		Scintilla::KeyMod::Ctrl, {}, 2, true});
	CHECK(editor.Text() == "abcde");
}

TEST_CASE("production editor rejects IME deletion inside a UTF-8 character") {
	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("a\xC3\xA9");
	editor.HandleKeyboardInput({Scintilla::Keys::End,
		Scintilla::KeyMod::Norm, {}, 1, true});

	Scalpel::ApplicationTextInputBatch batch;
	batch.deletion = Scalpel::ApplicationTextInputDelete{1, 0};
	batch.commit = "X";
	editor.HandleTextInputBatch(batch);
	CHECK(editor.Text() == "a\xC3\xA9");

	editor.SetKeyboardFocus(true);
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>('A'),
		Scintilla::KeyMod::Norm, "a", 2, true});
	CHECK(editor.Text() == "a\xC3\xA9" "a");
}

TEST_CASE("production editor cancels preedit after rejected IME deletion") {
	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("a\xC3\xA9");
	editor.HandleKeyboardInput({Scintilla::Keys::End,
		Scintilla::KeyMod::Norm, {}, 1, true});

	Scalpel::ApplicationTextInputBatch preedit;
	preedit.preedit = Scalpel::ApplicationTextInputPreedit{
		"\xE6\x96\x87", 3, 3};
	editor.HandleTextInputBatch(preedit);
	(void)editor.TakeTextInputState();

	Scalpel::ApplicationTextInputBatch rejected;
	rejected.deletion = Scalpel::ApplicationTextInputDelete{1, 0};
	rejected.commit = "X";
	editor.HandleTextInputBatch(rejected);
	CHECK(editor.Text() == "a\xC3\xA9");
	CHECK(editor.ImeIndicatorAt(3) == 0);
	const auto state = editor.TakeTextInputState();
	REQUIRE(state.has_value());
	REQUIRE(state->surroundingText.has_value());
	CHECK(*state->surroundingText == "a\xC3\xA9");
	CHECK(state->changeCause == Scalpel::ApplicationTextChangeCause::Other);
}

TEST_CASE("production editor reports empty IME refresh as other") {
	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("base");
	(void)editor.TakeTextInputState();

	Scalpel::ApplicationTextInputBatch refresh;
	refresh.refreshState = true;
	editor.HandleTextInputBatch(refresh);
	const auto state = editor.TakeTextInputState();
	REQUIRE(state.has_value());
	CHECK(state->changeCause == Scalpel::ApplicationTextChangeCause::Other);
}

TEST_CASE("production editor empty IME done clears preedit") {
	// text-input-v3 treats a done with no preedit_string as applying the
	// initial empty preedit, so an empty batch must drop tentative text.
	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("base");

	Scalpel::ApplicationTextInputBatch preedit;
	preedit.preedit = Scalpel::ApplicationTextInputPreedit{"x", 1, 1};
	editor.HandleTextInputBatch(preedit);
	CHECK(editor.Text() == "xbase");
	CHECK(editor.ImeIndicatorAt(0) != 0);

	Scalpel::ApplicationTextInputBatch emptyDone;
	editor.HandleTextInputBatch(emptyDone);
	CHECK(editor.Text() == "base");
	CHECK(editor.ImeIndicatorAt(0) == 0);
}

TEST_CASE("production editor IME deletion excludes the selection") {
	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("abcde");
	// Select "cd" (bytes 2..4); before=1 deletes 'b', after=1 deletes 'e'.
	editor.SetSel(2, 4);

	Scalpel::ApplicationTextInputBatch batch;
	batch.deletion = Scalpel::ApplicationTextInputDelete{1, 1};
	batch.commit = "X";
	editor.HandleTextInputBatch(batch);
	CHECK(editor.Text() == "aX");
}

TEST_CASE("production editor preserves IME cause across pointer motion") {
	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("base");
	(void)editor.TakeTextInputState();

	Scalpel::ApplicationTextInputBatch preedit;
	preedit.preedit = Scalpel::ApplicationTextInputPreedit{"x", 1, 1};
	editor.HandleTextInputBatch(preedit);
	editor.HandlePointerInput({Scalpel::PointerAction::Move,
		Scintilla::KeyMod::Norm, 20, 8});
	const auto state = editor.TakeTextInputState();
	REQUIRE(state.has_value());
	CHECK(state->changeCause ==
		Scalpel::ApplicationTextChangeCause::InputMethod);

	editor.HandlePointerInput({Scalpel::PointerAction::Leave});
	CHECK_FALSE(editor.TakeTextInputState().has_value());
}

TEST_CASE("production editor cancels preedit before direct input") {
	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("base");
	editor.HandleKeyboardInput({Scintilla::Keys::End,
		Scintilla::KeyMod::Norm, {}, 1, true});

	Scalpel::ApplicationTextInputBatch preedit;
	preedit.preedit = Scalpel::ApplicationTextInputPreedit{"x", 1, 1};
	editor.HandleTextInputBatch(preedit);
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>('Y'),
		Scintilla::KeyMod::Norm, "y", 2, true});
	CHECK(editor.Text() == "basey");

	editor.HandleTextInputBatch(preedit);
	editor.HandlePointerInput({Scalpel::PointerAction::Press,
		Scintilla::KeyMod::Norm, 20, 8, 0, 0, 3, 0});
	CHECK(editor.Text() == "basey");
	CHECK(editor.ImeIndicatorAt(5) == 0);
}

TEST_CASE("production editor renders the preedit cursor range") {
	Scalpel::ApplicationEditor editor(240, 120);
	editor.LoadInitialBuffer("base");
	editor.SetKeyboardFocus(true);

	Scalpel::ApplicationTextInputBatch selection;
	selection.preedit = Scalpel::ApplicationTextInputPreedit{
		"\xE6\x96\x87x", 0, 3};
	editor.HandleTextInputBatch(selection);
	CHECK(editor.GetCurrentPos() == 3);
	CHECK(editor.GetAnchor() == 0);

	Scalpel::ApplicationTextInputBatch hidden;
	hidden.preedit = Scalpel::ApplicationTextInputPreedit{"x", -1, -1};
	editor.HandleTextInputBatch(hidden);
	editor.RunPendingWork();
	CHECK_FALSE(editor.TimeUntilNextWork().has_value());
}
