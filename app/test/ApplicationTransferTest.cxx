#include "ApplicationTest.h"

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
	(void)editor.TakePrimarySelectionRequests();
	const double textX = editor.LineNumberMarginWidth() + 10;

	editor.HandlePointerInput({Scalpel::PointerAction::Move,
		Scintilla::KeyMod::Norm, textX, 8, 0, 0, 10, -1});
	editor.HandlePointerInput({Scalpel::PointerAction::Press,
		Scintilla::KeyMod::Norm, textX, 8, 0, 0, 11, 0});
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

#include "ApplicationUi.h"
#include "DocumentWorkspace.h"
#include "RecentFiles.h"
#include "MenuBar.h"
#include "TabStrip.h"

TEST_CASE("find bar clipboard copy and paste route through ApplicationUi") {
	Scalpel::ApplicationEditor editor(400, 240);
	editor.LoadInitialBuffer("body");
	const int inset =
		Scalpel::MenuBarHeight() + Scalpel::TabStripHeight();
	editor.SetTopChromeInset(inset);
	Scalpel::DocumentWorkspace workspace(editor);
	Scalpel::RecentFiles recent;
	Scalpel::ApplicationUi ui(editor, workspace, recent, "");
	ui.OpenFindBar();
	// Replace query with "needle".
	ui.FindModel().SelectAll();
	(void)ui.HandleKeyboard({static_cast<Scintilla::Keys>(0),
		Scintilla::KeyMod::Norm, "needle", 1, true});
	ui.FindModel().SelectAll();

	// Ctrl+C emits a shell-facing copy request with the query text.
	(void)ui.HandleKeyboard({static_cast<Scintilla::Keys>('C'),
		Scintilla::KeyMod::Ctrl, {}, 2, true});
	auto requests = ui.TakeClipboardRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests.front().operation ==
		Scalpel::ApplicationClipboardOperation::Copy);
	CHECK(requests.front().text == "needle");
	const uint64_t copyId = requests.front().id;

	ui.HandleClipboardResult(copyId, Scalpel::ApplicationClipboardOperation::Copy,
		Scalpel::ApplicationClipboardStatus::Complete);
	auto results = ui.TakeClipboardResults();
	REQUIRE_FALSE(results.empty());
	CHECK(results.back().status == Scalpel::ApplicationClipboardStatus::Complete);

	// Paste into the field.
	ui.SetClipboardPasteAvailable(true);
	ui.FindModel().SelectAll();
	(void)ui.HandleKeyboard({static_cast<Scintilla::Keys>('V'),
		Scintilla::KeyMod::Ctrl, {}, 3, true});
	requests = ui.TakeClipboardRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests.front().operation ==
		Scalpel::ApplicationClipboardOperation::Paste);
	const uint64_t pasteId = requests.front().id;
	ui.HandleClipboardResult(pasteId, Scalpel::ApplicationClipboardOperation::Paste,
		Scalpel::ApplicationClipboardStatus::Complete, "pasted");
	CHECK(ui.FindModel().query == "pasted");
	// Document body is unchanged.
	CHECK(editor.Text() == "body");
}

TEST_CASE("find bar clipboard paste is superseded after focus loss") {
	Scalpel::ApplicationEditor editor(400, 200);
	editor.LoadInitialBuffer("x");
	editor.SetTopChromeInset(
		Scalpel::MenuBarHeight() + Scalpel::TabStripHeight());
	Scalpel::DocumentWorkspace workspace(editor);
	Scalpel::RecentFiles recent;
	Scalpel::ApplicationUi ui(editor, workspace, recent, "");
	ui.OpenFindBar();
	ui.SetClipboardPasteAvailable(true);
	(void)ui.HandleKeyboard({static_cast<Scintilla::Keys>('V'),
		Scintilla::KeyMod::Ctrl, {}, 1, true});
	auto requests = ui.TakeClipboardRequests();
	REQUIRE(requests.size() == 1);
	const uint64_t pasteId = requests.front().id;

	ui.CloseFindBar();
	ui.HandleClipboardResult(pasteId, Scalpel::ApplicationClipboardOperation::Paste,
		Scalpel::ApplicationClipboardStatus::Complete, "late");
	auto results = ui.TakeClipboardResults();
	REQUIRE_FALSE(results.empty());
	CHECK(results.back().status == Scalpel::ApplicationClipboardStatus::Superseded);
	CHECK(ui.FindModel().query != "late");
	CHECK(editor.Text() == "x");
}

TEST_CASE("find bar clipboard paste is superseded after menu focus cycle") {
	Scalpel::ApplicationEditor editor(400, 200);
	editor.LoadInitialBuffer("body");
	Scalpel::DocumentWorkspace workspace(editor);
	Scalpel::RecentFiles recent;
	Scalpel::ApplicationUi ui(editor, workspace, recent, "");
	ui.OpenFindBar();
	ui.SetClipboardPasteAvailable(true);
	(void)ui.HandleKeyboard({static_cast<Scintilla::Keys>('V'),
		Scintilla::KeyMod::Ctrl, {}, 1, true});
	auto requests = ui.TakeClipboardRequests();
	REQUIRE(requests.size() == 1);
	const uint64_t pasteId = requests.front().id;

	// Opening the menu blurs the field. Dismiss it, then refocus by pointer.
	(void)ui.HandleKeyboard({static_cast<Scintilla::Keys>('F'),
		Scintilla::KeyMod::Alt, {}, 2, true});
	REQUIRE_FALSE(ui.FindBarFocused());
	(void)ui.HandleKeyboard({Scintilla::Keys::Escape,
		Scintilla::KeyMod::Norm, {}, 3, true});
	const auto field = ui.Layout().find.field;
	Scalpel::PointerInput pointer;
	pointer.action = Scalpel::PointerAction::Press;
	pointer.x = (field.left + field.right) / 2.0;
	pointer.y = (field.top + field.bottom) / 2.0;
	pointer.button = 0;
	(void)ui.HandlePointer(pointer);
	REQUIRE(ui.FindBarFocused());

	ui.HandleClipboardResult(pasteId,
		Scalpel::ApplicationClipboardOperation::Paste,
		Scalpel::ApplicationClipboardStatus::Complete, "late");
	const auto results = ui.TakeClipboardResults();
	REQUIRE_FALSE(results.empty());
	CHECK(results.back().status ==
		Scalpel::ApplicationClipboardStatus::Superseded);
	CHECK(ui.FindModel().query != "late");
	CHECK(editor.Text() == "body");
}
