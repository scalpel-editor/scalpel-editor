#include "catch.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

#include "ApplicationAction.h"
#include "ApplicationEditor.h"
#include "DocumentFile.h"
#include "DocumentWorkspace.h"

using Scalpel::ApplicationAction;
using Scalpel::ApplicationActionEnabled;
using Scalpel::ApplicationActionInfo;
using Scalpel::ApplicationEditor;
using Scalpel::ApplicationMenu;
using Scalpel::DispatchApplicationAction;
using Scalpel::DocumentShellRequest;
using Scalpel::DocumentWorkspace;
using Scalpel::InfoFor;
using Scalpel::KeyboardInput;
using Scalpel::MatchApplicationAction;
using Scalpel::UnsavedChoice;
using Scalpel::UnsavedPending;

namespace {

class TempFile {
public:
	explicit TempFile(std::string_view contents) {
		char pattern[] = "/tmp/scalpel-action-XXXXXX";
		const int fd = mkstemp(pattern);
		REQUIRE(fd >= 0);
		path = pattern;
		if (!contents.empty()) {
			const ssize_t written = write(fd, contents.data(), contents.size());
			REQUIRE(written == static_cast<ssize_t>(contents.size()));
		}
		REQUIRE(close(fd) == 0);
	}
	~TempFile() {
		if (!path.empty()) {
			(void)std::remove(path.c_str());
		}
	}
	TempFile(const TempFile &) = delete;
	TempFile &operator=(const TempFile &) = delete;

	std::string path;
};

bool HasRequest(const std::vector<DocumentShellRequest> &requests,
	DocumentShellRequest want) {
	for (DocumentShellRequest request : requests) {
		if (request == want) {
			return true;
		}
	}
	return false;
}

KeyboardInput Press(char key, Scintilla::KeyMod modifiers) {
	return {static_cast<Scintilla::Keys>(key), modifiers, {}, 1, true};
}

KeyboardInput Press(Scintilla::Keys key, Scintilla::KeyMod modifiers) {
	return {key, modifiers, {}, 1, true};
}

void TypeChar(ApplicationEditor &editor, char ch, uint32_t time) {
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>(0),
		Scintilla::KeyMod::Norm, std::string(1, ch), time, true});
}

}

TEST_CASE("application actions table lists File and Edit in menu order") {
	REQUIRE(Scalpel::ApplicationActionCount() == 12);
	const ApplicationActionInfo *table = Scalpel::ApplicationActionTable();
	CHECK(table[0].action == ApplicationAction::NewTab);
	CHECK(table[0].menu == ApplicationMenu::File);
	CHECK(table[5].action == ApplicationAction::Quit);
	CHECK(table[6].action == ApplicationAction::Undo);
	CHECK(table[6].menu == ApplicationMenu::Edit);
	CHECK(table[11].action == ApplicationAction::SelectAll);

	CHECK(InfoFor(ApplicationAction::Open).label == "Open\u2026");
	CHECK(InfoFor(ApplicationAction::SaveAs).shortcutLabel == "Ctrl+Shift+S");
	CHECK(InfoFor(ApplicationAction::Save).separatorBefore);
	CHECK(InfoFor(ApplicationAction::CloseTab).separatorBefore);
	CHECK(InfoFor(ApplicationAction::Quit).separatorBefore);
	CHECK(InfoFor(ApplicationAction::Cut).separatorBefore);
	CHECK(InfoFor(ApplicationAction::SelectAll).separatorBefore);
	CHECK_FALSE(InfoFor(ApplicationAction::NewTab).separatorBefore);
	CHECK_FALSE(InfoFor(ApplicationAction::Undo).separatorBefore);
}

TEST_CASE("application actions match listed shortcuts and ignore releases") {
	const Scintilla::KeyMod ctrl = Scintilla::KeyMod::Ctrl;
	const Scintilla::KeyMod ctrlShift =
		Scintilla::KeyMod::Ctrl | Scintilla::KeyMod::Shift;

	CHECK(MatchApplicationAction(Press('N', ctrl)) == ApplicationAction::NewTab);
	CHECK(MatchApplicationAction(Press('O', ctrl)) == ApplicationAction::Open);
	CHECK(MatchApplicationAction(Press('S', ctrl)) == ApplicationAction::Save);
	CHECK(MatchApplicationAction(Press('S', ctrlShift)) ==
		ApplicationAction::SaveAs);
	CHECK(MatchApplicationAction(Press('W', ctrl)) == ApplicationAction::CloseTab);
	CHECK(MatchApplicationAction(Press('Q', ctrl)) == ApplicationAction::Quit);
	CHECK(MatchApplicationAction(Press('Z', ctrl)) == ApplicationAction::Undo);
	CHECK(MatchApplicationAction(Press('Y', ctrl)) == ApplicationAction::Redo);
	CHECK(MatchApplicationAction(Press('X', ctrl)) == ApplicationAction::Cut);
	CHECK(MatchApplicationAction(Press('C', ctrl)) == ApplicationAction::Copy);
	CHECK(MatchApplicationAction(Press('V', ctrl)) == ApplicationAction::Paste);
	CHECK(MatchApplicationAction(Press('A', ctrl)) == ApplicationAction::SelectAll);

	KeyboardInput release = Press('N', ctrl);
	release.pressed = false;
	CHECK_FALSE(MatchApplicationAction(release).has_value());

	// Tab cycling is not an application menu action.
	CHECK_FALSE(MatchApplicationAction(
		Press(Scintilla::Keys::Tab, ctrl)).has_value());
	CHECK_FALSE(MatchApplicationAction(
		Press(Scintilla::Keys::Tab, ctrlShift)).has_value());
	// Unrelated keys stay unbound.
	CHECK_FALSE(MatchApplicationAction(Press('P', ctrl)).has_value());
}

TEST_CASE("application actions enablement follows edit state") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("abc");
	DocumentWorkspace workspace(editor);

	CHECK(ApplicationActionEnabled(ApplicationAction::NewTab, editor));
	CHECK(ApplicationActionEnabled(ApplicationAction::Quit, editor));
	CHECK(ApplicationActionEnabled(ApplicationAction::SelectAll, editor));
	CHECK_FALSE(ApplicationActionEnabled(ApplicationAction::Undo, editor));
	CHECK_FALSE(ApplicationActionEnabled(ApplicationAction::Redo, editor));
	CHECK_FALSE(ApplicationActionEnabled(ApplicationAction::Cut, editor));
	CHECK_FALSE(ApplicationActionEnabled(ApplicationAction::Copy, editor));
	CHECK_FALSE(ApplicationActionEnabled(ApplicationAction::Paste, editor));

	editor.HandleKeyboardInput(
		Press(Scintilla::Keys::End, Scintilla::KeyMod::Norm));
	TypeChar(editor, 'x', 2);
	CHECK(ApplicationActionEnabled(ApplicationAction::Undo, editor));
	CHECK_FALSE(ApplicationActionEnabled(ApplicationAction::Redo, editor));

	DispatchApplicationAction(ApplicationAction::Undo, workspace, editor);
	CHECK(editor.Text() == "abc");
	CHECK_FALSE(ApplicationActionEnabled(ApplicationAction::Undo, editor));
	CHECK(ApplicationActionEnabled(ApplicationAction::Redo, editor));
}

TEST_CASE("application actions dispatch file workspace commands") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("file actions\n");
	DocumentWorkspace workspace(editor);

	DispatchApplicationAction(ApplicationAction::NewTab, workspace, editor);
	CHECK(workspace.TabCount() == 2);
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::RefreshTabs));

	DispatchApplicationAction(ApplicationAction::Open, workspace, editor);
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::ShowOpen));

	DispatchApplicationAction(ApplicationAction::SaveAs, workspace, editor);
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::ShowSaveAs));

	// Untitled Save routes to Save As.
	DispatchApplicationAction(ApplicationAction::Save, workspace, editor);
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::ShowSaveAs));

	TempFile file("saved\n");
	workspace.HandleSaveResult(true, file.path);
	(void)workspace.TakeRequests();
	TypeChar(editor, '!', 3);
	REQUIRE(editor.Modified());
	DispatchApplicationAction(ApplicationAction::Save, workspace, editor);
	CHECK_FALSE(editor.Modified());
	// Successful write refreshes the tab label dirty state.
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::RefreshTabs));

	const auto active = workspace.ActiveTab();
	DispatchApplicationAction(ApplicationAction::CloseTab, workspace, editor);
	// Two tabs; closing the saved active tab leaves the other.
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.ActiveTab() != active);
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::RefreshTabs));
}

TEST_CASE("application actions quit is dirty-aware") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("quit\n");
	DocumentWorkspace workspace(editor);

	DispatchApplicationAction(ApplicationAction::Quit, workspace, editor);
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::AcceptClose));
	CHECK_FALSE(workspace.PromptActive());

	// Fresh workspace after accept is not available; rebuild dirty case.
	ApplicationEditor dirtyEditor(320, 180);
	dirtyEditor.LoadInitialBuffer("dirty quit\n");
	TypeChar(dirtyEditor, 'x', 1);
	DocumentWorkspace dirtyWorkspace(dirtyEditor);
	DispatchApplicationAction(ApplicationAction::Quit, dirtyWorkspace, dirtyEditor);
	CHECK(dirtyWorkspace.PromptActive());
	CHECK(dirtyWorkspace.Pending() == UnsavedPending::CloseWindow);
	CHECK(HasRequest(dirtyWorkspace.TakeRequests(),
		DocumentShellRequest::PromptBegan));
	CHECK_FALSE(HasRequest(dirtyWorkspace.TakeRequests(),
		DocumentShellRequest::AcceptClose));

	dirtyWorkspace.Choose(UnsavedChoice::Cancel);
	CHECK_FALSE(dirtyWorkspace.PromptActive());
}

TEST_CASE("application actions dispatch edit operations") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("edit me");
	DocumentWorkspace workspace(editor);

	DispatchApplicationAction(ApplicationAction::SelectAll, workspace, editor);
	CHECK(editor.HasSelection());
	CHECK(ApplicationActionEnabled(ApplicationAction::Copy, editor));
	CHECK(ApplicationActionEnabled(ApplicationAction::Cut, editor));

	DispatchApplicationAction(ApplicationAction::Copy, workspace, editor);
	const auto copyRequests = editor.TakeClipboardRequests();
	REQUIRE(copyRequests.size() == 1);
	CHECK(copyRequests.front().text == "edit me");

	editor.SetClipboardPasteAvailable(true);
	CHECK(ApplicationActionEnabled(ApplicationAction::Paste, editor));
	// Replace selection with paste.
	DispatchApplicationAction(ApplicationAction::Paste, workspace, editor);
	const auto pasteRequests = editor.TakeClipboardRequests();
	REQUIRE(pasteRequests.size() == 1);
	editor.HandleClipboardResult(pasteRequests.front().id,
		Scalpel::ApplicationClipboardOperation::Paste,
		Scalpel::ApplicationClipboardStatus::Complete, "new");
	CHECK(editor.Text() == "new");

	DispatchApplicationAction(ApplicationAction::Undo, workspace, editor);
	CHECK(editor.Text() == "edit me");
	CHECK(ApplicationActionEnabled(ApplicationAction::Redo, editor));
	DispatchApplicationAction(ApplicationAction::Redo, workspace, editor);
	CHECK(editor.Text() == "new");

	DispatchApplicationAction(ApplicationAction::SelectAll, workspace, editor);
	DispatchApplicationAction(ApplicationAction::Cut, workspace, editor);
	CHECK(editor.Text().empty());
	const auto cutRequests = editor.TakeClipboardRequests();
	REQUIRE(cutRequests.size() == 1);
	CHECK(cutRequests.front().text == "new");
}

TEST_CASE("application actions cancel tentative input before editing") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("base");
	DocumentWorkspace workspace(editor);

	Scalpel::ApplicationTextInputBatch preedit;
	preedit.preedit = Scalpel::ApplicationTextInputPreedit{"x", 1, 1};
	editor.HandleTextInputBatch(preedit);
	(void)editor.TakeTextInputState();
	REQUIRE(editor.Text() == "xbase");

	DispatchApplicationAction(ApplicationAction::Undo, workspace, editor);
	CHECK(editor.Text() == "base");
	const auto state = editor.TakeTextInputState();
	REQUIRE(state.has_value());
	REQUIRE(state->surroundingText.has_value());
	CHECK(*state->surroundingText == "base");
	CHECK(state->cursor == 0);
	CHECK(state->anchor == 0);
}

TEST_CASE("application actions report selection changes to text input") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("select");
	DocumentWorkspace workspace(editor);
	(void)editor.TakeTextInputState();

	DispatchApplicationAction(ApplicationAction::SelectAll, workspace, editor);
	const auto state = editor.TakeTextInputState();
	REQUIRE(state.has_value());
	REQUIRE(state->surroundingText.has_value());
	CHECK(*state->surroundingText == "select");
	CHECK(std::abs(state->cursor - state->anchor) == 6);
}

TEST_CASE("application actions reject disabled edit dispatch") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("keep");
	DocumentWorkspace workspace(editor);

	// No selection: cut and copy must not run.
	DispatchApplicationAction(ApplicationAction::Cut, workspace, editor);
	DispatchApplicationAction(ApplicationAction::Copy, workspace, editor);
	CHECK(editor.TakeClipboardRequests().empty());
	CHECK(editor.Text() == "keep");

	// No clipboard offer: paste must not queue.
	DispatchApplicationAction(ApplicationAction::Paste, workspace, editor);
	CHECK(editor.TakeClipboardRequests().empty());

	// Empty buffer after clear: select all disabled.
	DispatchApplicationAction(ApplicationAction::SelectAll, workspace, editor);
	DispatchApplicationAction(ApplicationAction::Cut, workspace, editor);
	REQUIRE(editor.Text().empty());
	CHECK_FALSE(ApplicationActionEnabled(ApplicationAction::SelectAll, editor));
	DispatchApplicationAction(ApplicationAction::SelectAll, workspace, editor);
	CHECK_FALSE(editor.HasSelection());
}

TEST_CASE("application actions shortcuts match dispatch for file and edit") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("shortcut");
	DocumentWorkspace workspace(editor);
	const Scintilla::KeyMod ctrl = Scintilla::KeyMod::Ctrl;

	const auto action = MatchApplicationAction(Press('A', ctrl));
	REQUIRE(action);
	DispatchApplicationAction(*action, workspace, editor);
	CHECK(editor.HasSelection());

	const auto quit = MatchApplicationAction(Press('Q', ctrl));
	REQUIRE(quit == ApplicationAction::Quit);
	DispatchApplicationAction(*quit, workspace, editor);
	// Dirty after select-all does not mark modified; buffer still clean.
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::AcceptClose));
}
