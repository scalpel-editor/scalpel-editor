#include "catch.hpp"

#include <cstdio>
#include <string>
#include <unistd.h>

#include "ApplicationEditor.h"
#include "DocumentFile.h"
#include "DocumentWorkspace.h"

using Scalpel::ApplicationEditor;
using Scalpel::DocumentId;
using Scalpel::DocumentShellRequest;
using Scalpel::DocumentWorkspace;
using Scalpel::UnsavedChoice;
using Scalpel::UnsavedPending;

namespace {

class TempFile {
public:
	explicit TempFile(std::string_view contents) {
		char pattern[] = "/tmp/scalpel-workspace-XXXXXX";
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

void DirtyBuffer(ApplicationEditor &editor) {
	editor.HandleKeyboardInput({Scintilla::Keys::End, Scintilla::KeyMod::Norm,
		{}, 1, true});
	editor.HandleKeyboardInput({static_cast<Scintilla::Keys>(0),
		Scintilla::KeyMod::Norm, "x", 2, true});
	REQUIRE(editor.Modified());
}

bool HasRequest(const std::vector<DocumentShellRequest> &requests,
	DocumentShellRequest want) {
	for (DocumentShellRequest request : requests) {
		if (request == want) {
			return true;
		}
	}
	return false;
}

}

TEST_CASE("document workspace single-document clean close accepts") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("clean\n");
	DocumentWorkspace workspace(editor);

	workspace.RequestClose();
	const auto requests = workspace.TakeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0] == DocumentShellRequest::AcceptClose);
	CHECK_FALSE(workspace.PromptActive());
}

TEST_CASE("document workspace single-document dirty close prompts") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("dirty\n");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);
	const DocumentId only = workspace.ActiveTab();

	workspace.RequestClose();
	const auto requests = workspace.TakeRequests();
	CHECK(HasRequest(requests, DocumentShellRequest::PromptBegan));
	CHECK(workspace.PromptActive());
	CHECK(workspace.Pending() == UnsavedPending::CloseWindow);
	CHECK(workspace.PromptTab() == only);
	CHECK_FALSE(HasRequest(requests, DocumentShellRequest::AcceptClose));
}

TEST_CASE("document workspace single-document close cancel dismisses") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("dirty\n");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);
	workspace.RequestClose();
	(void)workspace.TakeRequests();

	workspace.Choose(UnsavedChoice::Cancel);
	CHECK_FALSE(workspace.PromptActive());
	CHECK(workspace.TakeRequests().empty());
	CHECK(editor.Modified());
}

TEST_CASE("document workspace single-document close discard accepts") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("dirty\n");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);
	workspace.RequestClose();
	(void)workspace.TakeRequests();

	workspace.Choose(UnsavedChoice::Discard);
	const auto requests = workspace.TakeRequests();
	CHECK(HasRequest(requests, DocumentShellRequest::AcceptClose));
	CHECK_FALSE(workspace.PromptActive());
}

TEST_CASE("document workspace single-document close save with path") {
	TempFile file("original");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("original");
	DocumentWorkspace workspace(editor);
	// Establish a known path via Save As without a prompt.
	workspace.HandleSaveResult(true, file.path);
	REQUIRE(workspace.Path() == file.path);
	REQUIRE_FALSE(editor.Modified());

	DirtyBuffer(editor);
	workspace.RequestClose();
	(void)workspace.TakeRequests();
	REQUIRE(workspace.PromptActive());

	workspace.Choose(UnsavedChoice::Save);
	const auto requests = workspace.TakeRequests();
	CHECK(HasRequest(requests, DocumentShellRequest::AcceptClose));
	CHECK_FALSE(workspace.PromptActive());
	CHECK_FALSE(editor.Modified());
	const auto read = Scalpel::ReadDocumentFile(file.path);
	REQUIRE(read.has_value());
	CHECK(*read == editor.Text());
}

TEST_CASE("document workspace single-document close Save As then accept") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("untitled body");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);
	workspace.RequestClose();
	(void)workspace.TakeRequests();

	workspace.Choose(UnsavedChoice::Save);
	{
		const auto requests = workspace.TakeRequests();
		CHECK(HasRequest(requests, DocumentShellRequest::ShowSaveAs));
		CHECK(workspace.AwaitingSaveAs());
		CHECK(workspace.PromptActive());
	}

	TempFile file("");
	workspace.HandleSaveResult(true, file.path);
	const auto requests = workspace.TakeRequests();
	CHECK(HasRequest(requests, DocumentShellRequest::AcceptClose));
	CHECK_FALSE(workspace.PromptActive());
	CHECK(workspace.Path() == file.path);
	CHECK_FALSE(editor.Modified());
}

TEST_CASE("document workspace single-document Save As cancel keeps prompt") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("untitled");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);
	workspace.RequestClose();
	(void)workspace.TakeRequests();
	workspace.Choose(UnsavedChoice::Save);
	(void)workspace.TakeRequests();
	REQUIRE(workspace.AwaitingSaveAs());

	workspace.HandleSaveResult(false, {});
	CHECK(workspace.PromptActive());
	CHECK(workspace.Pending() == UnsavedPending::CloseWindow);
	CHECK_FALSE(workspace.AwaitingSaveAs());
	CHECK(workspace.TakeRequests().empty());
}

TEST_CASE("document workspace single-document open while clean") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("clean\n");
	DocumentWorkspace workspace(editor);

	workspace.RequestOpen();
	const auto requests = workspace.TakeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0] == DocumentShellRequest::ShowOpen);
	CHECK_FALSE(workspace.PromptActive());
}

TEST_CASE("document workspace single-document open while dirty still opens") {
	// Open no longer prompts; a new tab is created on accept.
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("dirty\n");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);

	workspace.RequestOpen();
	const auto requests = workspace.TakeRequests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0] == DocumentShellRequest::ShowOpen);
	CHECK_FALSE(workspace.PromptActive());
	CHECK(editor.Modified());
}

TEST_CASE("document workspace single-document open result loads path") {
	TempFile file("file contents\n");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);
	const DocumentId startupId = workspace.ActiveTab();

	workspace.HandleOpenResult(true, file.path);
	CHECK(workspace.Path() == file.path);
	CHECK(editor.Text() == "file contents\n");
	CHECK_FALSE(editor.Modified());
	CHECK(workspace.TabCount() == 2);
	CHECK(workspace.ActiveTab() != startupId);
}

TEST_CASE("document workspace single-document open result keeps dirty sibling") {
	TempFile file("other\n");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);
	const std::string dirtyText = editor.Text();
	const DocumentId dirtyId = workspace.ActiveTab();

	workspace.HandleOpenResult(true, file.path);
	CHECK(workspace.Path() == file.path);
	CHECK(editor.Text() == "other\n");
	CHECK(workspace.TabCount() == 2);
	workspace.ActivateTab(dirtyId);
	CHECK(editor.Text() == dirtyText);
	CHECK(editor.Modified());
}

TEST_CASE("document workspace single-document save and save as requests") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("body");
	DocumentWorkspace workspace(editor);

	SECTION("untitled save requests Save As") {
		workspace.RequestSave();
		const auto requests = workspace.TakeRequests();
		REQUIRE(requests.size() == 1);
		CHECK(requests[0] == DocumentShellRequest::ShowSaveAs);
	}
	SECTION("known path save writes") {
		TempFile file("old");
		workspace.HandleSaveResult(true, file.path);
		(void)workspace.TakeRequests();
		DirtyBuffer(editor);
		workspace.RequestSave();
		CHECK_FALSE(HasRequest(workspace.TakeRequests(),
			DocumentShellRequest::ShowSaveAs));
		CHECK_FALSE(editor.Modified());
		const auto read = Scalpel::ReadDocumentFile(file.path);
		REQUIRE(read.has_value());
		CHECK(*read == editor.Text());
	}
	SECTION("explicit Save As always requests dialog") {
		TempFile file("old");
		workspace.HandleSaveResult(true, file.path);
		(void)workspace.TakeRequests();
		workspace.RequestSaveAs();
		const auto requests = workspace.TakeRequests();
		REQUIRE(requests.size() == 1);
		CHECK(requests[0] == DocumentShellRequest::ShowSaveAs);
	}
}

TEST_CASE("document workspace single-document close while prompt is no-op") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("dirty\n");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);
	workspace.RequestClose();
	(void)workspace.TakeRequests();
	REQUIRE(workspace.PromptActive());

	workspace.RequestClose();
	CHECK(workspace.TakeRequests().empty());
	CHECK(workspace.PromptActive());
}

TEST_CASE("document workspace tabs start with one untitled") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);

	REQUIRE(workspace.TabCount() == 1);
	const auto tabs = workspace.Tabs();
	REQUIRE(tabs.size() == 1);
	CHECK(tabs[0].id == workspace.ActiveTab());
	CHECK(tabs[0].path.empty());
	CHECK(tabs[0].untitledNumber == 1);
	CHECK(tabs[0].label == "Untitled 1");
	CHECK_FALSE(tabs[0].dirty);
	CHECK(tabs[0].active);
	CHECK(editor.Text() == "startup\n");
}

TEST_CASE("document workspace tabs new activate and cycle") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("first\n");
	DocumentWorkspace workspace(editor);
	const DocumentId first = workspace.ActiveTab();

	workspace.NewTab();
	const DocumentId second = workspace.ActiveTab();
	CHECK(second != first);
	CHECK(workspace.TabCount() == 2);
	CHECK(editor.Text().empty());
	CHECK_FALSE(editor.Modified());
	{
		const auto tabs = workspace.Tabs();
		REQUIRE(tabs.size() == 2);
		CHECK(tabs[0].id == first);
		CHECK(tabs[0].label == "Untitled 1");
		CHECK_FALSE(tabs[0].active);
		CHECK(tabs[1].id == second);
		CHECK(tabs[1].untitledNumber == 2);
		CHECK(tabs[1].label == "Untitled 2");
		CHECK(tabs[1].active);
	}
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::RefreshTabs));

	workspace.ActivateTab(first);
	CHECK(workspace.ActiveTab() == first);
	CHECK(editor.Text() == "first\n");

	workspace.CycleTab(1);
	CHECK(workspace.ActiveTab() == second);
	workspace.CycleTab(1);
	CHECK(workspace.ActiveTab() == first);
	workspace.CycleTab(-1);
	CHECK(workspace.ActiveTab() == second);
}

TEST_CASE("document workspace tabs dirty labels") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("body");
	DocumentWorkspace workspace(editor);

	CHECK(workspace.Tabs()[0].label == "Untitled 1");
	DirtyBuffer(editor);
	CHECK(workspace.Tabs()[0].dirty);
	CHECK(workspace.Tabs()[0].label == "Untitled 1 *");

	TempFile file("saved");
	workspace.HandleSaveResult(true, file.path);
	(void)workspace.TakeRequests();
	CHECK_FALSE(workspace.Tabs()[0].dirty);
	CHECK(workspace.Tabs()[0].label == Scalpel::DocumentBaseName(file.path));
	CHECK(workspace.Tabs()[0].untitledNumber == 0);

	DirtyBuffer(editor);
	CHECK(workspace.Tabs()[0].label ==
		Scalpel::DocumentBaseName(file.path) + " *");
}

TEST_CASE("document workspace tabs open creates tab not replace") {
	TempFile file("opened body\n");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("keep me\n");
	DocumentWorkspace workspace(editor);
	const DocumentId original = workspace.ActiveTab();

	workspace.HandleOpenResult(true, file.path);
	CHECK(workspace.TabCount() == 2);
	CHECK(workspace.ActiveTab() != original);
	CHECK(editor.Text() == "opened body\n");
	CHECK(workspace.Path() == file.path);

	workspace.ActivateTab(original);
	CHECK(editor.Text() == "keep me\n");
	CHECK(workspace.Path().empty());
}

TEST_CASE("document workspace tabs open selects existing path") {
	TempFile file("once\n");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);

	workspace.HandleOpenResult(true, file.path);
	const DocumentId opened = workspace.ActiveTab();
	CHECK(workspace.TabCount() == 2);
	workspace.NewTab();
	CHECK(workspace.TabCount() == 3);
	CHECK(workspace.ActiveTab() != opened);

	workspace.HandleOpenResult(true, file.path);
	CHECK(workspace.ActiveTab() == opened);
	CHECK(workspace.TabCount() == 3);
	CHECK(editor.Text() == "once\n");
}

TEST_CASE("document workspace tabs close clean tab") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("first\n");
	DocumentWorkspace workspace(editor);
	const DocumentId first = workspace.ActiveTab();
	workspace.NewTab();
	const DocumentId second = workspace.ActiveTab();
	(void)workspace.TakeRequests();

	workspace.CloseTab(first);
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.ActiveTab() == second);
	CHECK_FALSE(editor.HasDocument(first));
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::RefreshTabs));
}

TEST_CASE("document workspace tabs close last clean creates untitled") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("only\n");
	DocumentWorkspace workspace(editor);
	const DocumentId only = workspace.ActiveTab();
	(void)workspace.TakeRequests();

	workspace.CloseTab(only);
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.ActiveTab() != only);
	CHECK_FALSE(editor.HasDocument(only));
	CHECK(editor.Text().empty());
	CHECK(workspace.Path().empty());
	const auto tabs = workspace.Tabs();
	REQUIRE(tabs.size() == 1);
	CHECK(tabs[0].untitledNumber == 2);
	CHECK(tabs[0].label == "Untitled 2");
	CHECK_FALSE(HasRequest(workspace.TakeRequests(),
		DocumentShellRequest::AcceptClose));
}

TEST_CASE("document workspace tabs close dirty prompts then discard") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("keep\n");
	DocumentWorkspace workspace(editor);
	const DocumentId first = workspace.ActiveTab();
	workspace.NewTab();
	const DocumentId second = workspace.ActiveTab();
	DirtyBuffer(editor);
	(void)workspace.TakeRequests();

	workspace.ActivateTab(first);
	(void)workspace.TakeRequests();
	workspace.CloseTab(second);
	CHECK(workspace.ActiveTab() == second);
	CHECK(workspace.PromptActive());
	CHECK(workspace.Pending() == UnsavedPending::CloseTab);
	CHECK(workspace.PromptTab() == second);
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::PromptBegan));

	workspace.Choose(UnsavedChoice::Discard);
	CHECK_FALSE(workspace.PromptActive());
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.ActiveTab() == first);
	CHECK_FALSE(editor.HasDocument(second));
	CHECK_FALSE(HasRequest(workspace.TakeRequests(),
		DocumentShellRequest::AcceptClose));
}

TEST_CASE("document workspace tabs close dirty last then discard") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("only\n");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);
	const DocumentId only = workspace.ActiveTab();

	workspace.CloseTab(only);
	REQUIRE(workspace.PromptActive());
	workspace.Choose(UnsavedChoice::Discard);
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.ActiveTab() != only);
	CHECK_FALSE(editor.HasDocument(only));
	CHECK(editor.Text().empty());
	CHECK_FALSE(HasRequest(workspace.TakeRequests(),
		DocumentShellRequest::AcceptClose));
}

TEST_CASE("document workspace tabs close dirty save with path") {
	TempFile file("original");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("original");
	DocumentWorkspace workspace(editor);
	workspace.HandleSaveResult(true, file.path);
	(void)workspace.TakeRequests();
	workspace.NewTab();
	const DocumentId other = workspace.ActiveTab();
	workspace.ActivateTab(workspace.Tabs()[0].id);
	DirtyBuffer(editor);
	const DocumentId dirty = workspace.ActiveTab();
	(void)workspace.TakeRequests();

	workspace.CloseTab(dirty);
	REQUIRE(workspace.PromptActive());
	workspace.Choose(UnsavedChoice::Save);
	CHECK_FALSE(workspace.PromptActive());
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.ActiveTab() == other);
	const auto read = Scalpel::ReadDocumentFile(file.path);
	REQUIRE(read.has_value());
	CHECK(read->find('x') != std::string::npos);
}

TEST_CASE("document workspace tabs untitled numbers stay stable") {
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	workspace.NewTab();
	workspace.NewTab();
	const auto before = workspace.Tabs();
	REQUIRE(before.size() == 3);
	CHECK(before[0].untitledNumber == 1);
	CHECK(before[1].untitledNumber == 2);
	CHECK(before[2].untitledNumber == 3);

	workspace.CloseTab(before[1].id);
	workspace.NewTab();
	const auto after = workspace.Tabs();
	REQUIRE(after.size() == 3);
	CHECK(after[0].untitledNumber == 1);
	CHECK(after[1].untitledNumber == 3);
	CHECK(after[2].untitledNumber == 4);
}

TEST_CASE("document workspace tabs independent text after switch") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("alpha");
	DocumentWorkspace workspace(editor);
	const DocumentId first = workspace.ActiveTab();
	workspace.NewTab();
	editor.LoadInitialBuffer("beta");
	const DocumentId second = workspace.ActiveTab();

	workspace.ActivateTab(first);
	CHECK(editor.Text() == "alpha");
	workspace.ActivateTab(second);
	CHECK(editor.Text() == "beta");
}

TEST_CASE("document workspace portal routing open registers request ID") {
	TempFile a("alpha\n");
	TempFile b("beta\n");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);
	const DocumentId startup = workspace.ActiveTab();

	workspace.RequestOpen();
	REQUIRE(HasRequest(workspace.TakeRequests(), DocumentShellRequest::ShowOpen));
	workspace.RegisterOpenRequest(41);

	workspace.HandlePortalResult(41, true, {a.path, b.path});
	CHECK(workspace.TabCount() == 3);
	CHECK(workspace.ActiveTab() != startup);
	// Last accepted path is active.
	CHECK(editor.Text() == "beta\n");
	CHECK(workspace.Path() == b.path);
	workspace.ActivateTab(startup);
	CHECK(editor.Text() == "startup\n");
	// The first opened file remains as its own tab.
	bool foundA = false;
	for (const auto &tab : workspace.Tabs()) {
		if (tab.path == a.path) {
			foundA = true;
			workspace.ActivateTab(tab.id);
			CHECK(editor.Text() == "alpha\n");
		}
	}
	CHECK(foundA);
}

TEST_CASE("document workspace portal routing ignores unknown request ID") {
	TempFile file("ignored\n");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);

	workspace.HandlePortalResult(99, true, {file.path});
	CHECK(workspace.TabCount() == 1);
	CHECK(editor.Text() == "startup\n");
	CHECK(workspace.Path().empty());
}

TEST_CASE("document workspace portal routing open cancel is a no-op") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);
	workspace.RegisterOpenRequest(7);
	workspace.HandlePortalResult(7, false, {});
	CHECK(workspace.TabCount() == 1);
	CHECK(editor.Text() == "startup\n");
}

TEST_CASE("document workspace portal routing Save As targets initiating tab") {
	TempFile file("");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("keep me");
	DocumentWorkspace workspace(editor);
	const DocumentId first = workspace.ActiveTab();
	DirtyBuffer(editor);
	const std::string dirtyText = editor.Text();

	workspace.RequestSaveAs();
	REQUIRE(HasRequest(workspace.TakeRequests(), DocumentShellRequest::ShowSaveAs));
	workspace.RegisterSaveAsRequest(12);

	// Switch away while the portal is open; the result must still hit first.
	workspace.NewTab();
	editor.LoadInitialBuffer("other");
	const DocumentId second = workspace.ActiveTab();
	REQUIRE(second != first);

	workspace.HandlePortalResult(12, true, {file.path});
	CHECK(workspace.ActiveTab() == second);
	CHECK(editor.Text() == "other");
	workspace.ActivateTab(first);
	CHECK(workspace.Path() == file.path);
	CHECK(editor.Text() == dirtyText);
	CHECK_FALSE(editor.Modified());
	const auto read = Scalpel::ReadDocumentFile(file.path);
	REQUIRE(read.has_value());
	CHECK(*read == dirtyText);
}

TEST_CASE("document workspace portal routing Save As ignores closed tab") {
	TempFile file("");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("gone");
	DocumentWorkspace workspace(editor);
	const DocumentId first = workspace.ActiveTab();
	workspace.RequestSaveAs();
	(void)workspace.TakeRequests();
	workspace.RegisterSaveAsRequest(20);

	workspace.NewTab();
	const DocumentId second = workspace.ActiveTab();
	workspace.CloseTab(first);
	REQUIRE_FALSE(editor.HasDocument(first));
	REQUIRE(workspace.ActiveTab() == second);

	workspace.HandlePortalResult(20, true, {file.path});
	CHECK(workspace.Path().empty());
	CHECK(workspace.TabCount() == 1);
	const auto read = Scalpel::ReadDocumentFile(file.path);
	// No write should have occurred for a tab that no longer exists.
	REQUIRE(read.has_value());
	CHECK(read->empty());
}

TEST_CASE("document workspace portal routing delayed Save As continues prompt") {
	TempFile file("");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("untitled body");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);
	const DocumentId dirty = workspace.ActiveTab();
	workspace.CloseTab(dirty);
	REQUIRE(workspace.PromptActive());
	workspace.Choose(UnsavedChoice::Save);
	REQUIRE(HasRequest(workspace.TakeRequests(), DocumentShellRequest::ShowSaveAs));
	REQUIRE(workspace.AwaitingSaveAs());
	workspace.RegisterSaveAsRequest(33);

	workspace.HandlePortalResult(33, true, {file.path});
	CHECK_FALSE(workspace.PromptActive());
	CHECK_FALSE(editor.HasDocument(dirty));
	CHECK(workspace.TabCount() == 1);
	const auto read = Scalpel::ReadDocumentFile(file.path);
	REQUIRE(read.has_value());
	CHECK(read->find('x') != std::string::npos);
}

TEST_CASE("document workspace portal routing Save As cancel keeps prompt") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("untitled");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);
	workspace.RequestClose();
	(void)workspace.TakeRequests();
	workspace.Choose(UnsavedChoice::Save);
	(void)workspace.TakeRequests();
	REQUIRE(workspace.AwaitingSaveAs());
	workspace.RegisterSaveAsRequest(44);

	workspace.HandlePortalResult(44, false, {});
	CHECK(workspace.PromptActive());
	CHECK(workspace.Pending() == UnsavedPending::CloseWindow);
	CHECK_FALSE(workspace.AwaitingSaveAs());
}

TEST_CASE("document workspace portal routing stale Save As keeps newer prompt") {
	TempFile file("");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("untitled");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);

	workspace.RequestClose();
	(void)workspace.TakeRequests();
	workspace.Choose(UnsavedChoice::Save);
	(void)workspace.TakeRequests();
	workspace.RegisterSaveAsRequest(45);

	// Cancelling the card abandons this close decision, but the portal request
	// may still deliver a result after another close decision has begun.
	workspace.Choose(UnsavedChoice::Cancel);
	REQUIRE_FALSE(workspace.PromptActive());
	workspace.RequestClose();
	(void)workspace.TakeRequests();
	REQUIRE(workspace.PromptActive());
	REQUIRE_FALSE(workspace.AwaitingSaveAs());

	workspace.HandlePortalResult(45, true, {file.path});
	CHECK(workspace.PromptActive());
	CHECK(workspace.Pending() == UnsavedPending::CloseWindow);
	CHECK_FALSE(HasRequest(workspace.TakeRequests(),
		DocumentShellRequest::AcceptClose));
	CHECK(workspace.Path() == file.path);
	CHECK_FALSE(editor.Modified());
}

TEST_CASE("document workspace portal routing open selects existing among many") {
	TempFile existing("once\n");
	TempFile extra("twice\n");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);

	workspace.HandleOpenResult(true, existing.path);
	const DocumentId opened = workspace.ActiveTab();
	workspace.NewTab();
	REQUIRE(workspace.TabCount() == 3);

	workspace.RegisterOpenRequest(55);
	workspace.HandlePortalResult(55, true, {existing.path, extra.path});
	// Last path is active; existing path did not duplicate.
	CHECK(workspace.TabCount() == 4);
	CHECK(workspace.Path() == extra.path);
	CHECK(editor.Text() == "twice\n");
	int existingCount = 0;
	for (const auto &tab : workspace.Tabs()) {
		if (tab.path == existing.path) {
			++existingCount;
			CHECK(tab.id == opened);
		}
	}
	CHECK(existingCount == 1);
}

TEST_CASE("document workspace portal routing note Save As dialog failed") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("untitled");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);
	workspace.RequestClose();
	(void)workspace.TakeRequests();
	workspace.Choose(UnsavedChoice::Save);
	(void)workspace.TakeRequests();
	REQUIRE(workspace.AwaitingSaveAs());

	workspace.NoteSaveAsDialogFailed();
	CHECK(workspace.PromptActive());
	CHECK_FALSE(workspace.AwaitingSaveAs());
}

TEST_CASE("document workspace window close multi dirty advances in order") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("first\n");
	DocumentWorkspace workspace(editor);
	const DocumentId first = workspace.ActiveTab();
	DirtyBuffer(editor);

	workspace.NewTab();
	editor.LoadInitialBuffer("second\n");
	const DocumentId second = workspace.ActiveTab();
	DirtyBuffer(editor);

	workspace.NewTab();
	editor.LoadInitialBuffer("clean\n");
	const DocumentId clean = workspace.ActiveTab();
	REQUIRE_FALSE(editor.Modified());
	(void)workspace.TakeRequests();

	// Active is clean; window close must still find the first dirty tab.
	workspace.RequestClose();
	CHECK(workspace.PromptActive());
	CHECK(workspace.Pending() == UnsavedPending::CloseWindow);
	CHECK(workspace.PromptTab() == first);
	CHECK(workspace.ActiveTab() == first);
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::PromptBegan));

	workspace.Choose(UnsavedChoice::Discard);
	CHECK(workspace.PromptActive());
	CHECK(workspace.Pending() == UnsavedPending::CloseWindow);
	CHECK(workspace.PromptTab() == second);
	CHECK(workspace.ActiveTab() == second);
	CHECK(workspace.TabCount() == 3);
	CHECK(editor.HasDocument(first));
	CHECK(editor.HasDocument(second));
	CHECK(editor.HasDocument(clean));
	CHECK_FALSE(HasRequest(workspace.TakeRequests(),
		DocumentShellRequest::AcceptClose));

	workspace.Choose(UnsavedChoice::Discard);
	CHECK_FALSE(workspace.PromptActive());
	CHECK(workspace.TabCount() == 3);
	const auto requests = workspace.TakeRequests();
	CHECK(HasRequest(requests, DocumentShellRequest::AcceptClose));
}

TEST_CASE("document workspace window close cancel keeps all tabs") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("first\n");
	DocumentWorkspace workspace(editor);
	const DocumentId first = workspace.ActiveTab();
	DirtyBuffer(editor);
	const std::string firstText = editor.Text();

	workspace.NewTab();
	editor.LoadInitialBuffer("second\n");
	const DocumentId second = workspace.ActiveTab();
	DirtyBuffer(editor);
	const std::string secondText = editor.Text();
	(void)workspace.TakeRequests();

	workspace.RequestClose();
	REQUIRE(workspace.PromptTab() == first);
	workspace.Choose(UnsavedChoice::Discard);
	REQUIRE(workspace.PromptTab() == second);

	workspace.Choose(UnsavedChoice::Cancel);
	CHECK_FALSE(workspace.PromptActive());
	CHECK(workspace.TabCount() == 2);
	CHECK(editor.HasDocument(first));
	CHECK(editor.HasDocument(second));
	CHECK_FALSE(HasRequest(workspace.TakeRequests(),
		DocumentShellRequest::AcceptClose));

	// Both tabs remain dirty; cancel did not finish the walk.
	workspace.ActivateTab(first);
	CHECK(editor.Text() == firstText);
	CHECK(editor.Modified(first));
	workspace.ActivateTab(second);
	CHECK(editor.Text() == secondText);
	CHECK(editor.Modified(second));
}

TEST_CASE("document workspace window close cancel after save keeps remaining dirty") {
	TempFile file("original");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("original");
	DocumentWorkspace workspace(editor);
	workspace.HandleSaveResult(true, file.path);
	(void)workspace.TakeRequests();
	const DocumentId first = workspace.ActiveTab();
	DirtyBuffer(editor);

	workspace.NewTab();
	editor.LoadInitialBuffer("second\n");
	const DocumentId second = workspace.ActiveTab();
	DirtyBuffer(editor);
	(void)workspace.TakeRequests();

	workspace.RequestClose();
	REQUIRE(workspace.PromptTab() == first);
	workspace.Choose(UnsavedChoice::Save);
	REQUIRE(workspace.PromptTab() == second);
	CHECK_FALSE(editor.Modified(first));

	workspace.Choose(UnsavedChoice::Cancel);
	CHECK_FALSE(workspace.PromptActive());
	CHECK(workspace.TabCount() == 2);
	CHECK_FALSE(HasRequest(workspace.TakeRequests(),
		DocumentShellRequest::AcceptClose));
	CHECK(editor.Modified(second));
	CHECK_FALSE(editor.Modified(first));
}

TEST_CASE("document workspace window close skips clean tabs") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("clean first\n");
	DocumentWorkspace workspace(editor);
	const DocumentId clean = workspace.ActiveTab();

	workspace.NewTab();
	editor.LoadInitialBuffer("dirty\n");
	const DocumentId dirty = workspace.ActiveTab();
	DirtyBuffer(editor);

	workspace.NewTab();
	editor.LoadInitialBuffer("also clean\n");
	(void)workspace.TakeRequests();

	workspace.RequestClose();
	CHECK(workspace.PromptTab() == dirty);
	CHECK(workspace.ActiveTab() == dirty);
	CHECK(workspace.Pending() == UnsavedPending::CloseWindow);

	workspace.Choose(UnsavedChoice::Discard);
	CHECK_FALSE(workspace.PromptActive());
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::AcceptClose));
	CHECK(workspace.TabCount() == 3);
	CHECK(editor.HasDocument(clean));
	CHECK(editor.HasDocument(dirty));
}

TEST_CASE("document workspace window close Save As failure keeps same tab prompt") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("first\n");
	DocumentWorkspace workspace(editor);
	const DocumentId first = workspace.ActiveTab();
	DirtyBuffer(editor);
	workspace.NewTab();
	editor.LoadInitialBuffer("second\n");
	const DocumentId second = workspace.ActiveTab();
	DirtyBuffer(editor);
	(void)workspace.TakeRequests();

	workspace.RequestClose();
	REQUIRE(workspace.PromptTab() == first);
	workspace.Choose(UnsavedChoice::Save);
	REQUIRE(workspace.AwaitingSaveAs());
	REQUIRE(HasRequest(workspace.TakeRequests(), DocumentShellRequest::ShowSaveAs));

	workspace.HandleSaveResult(false, {});
	CHECK(workspace.PromptActive());
	CHECK(workspace.Pending() == UnsavedPending::CloseWindow);
	CHECK(workspace.PromptTab() == first);
	CHECK_FALSE(workspace.AwaitingSaveAs());
	CHECK(workspace.TabCount() == 2);
	CHECK(editor.HasDocument(second));
	CHECK_FALSE(HasRequest(workspace.TakeRequests(),
		DocumentShellRequest::AcceptClose));
}

TEST_CASE("document workspace window close delayed Save As advances walk") {
	TempFile file("");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("first\n");
	DocumentWorkspace workspace(editor);
	const DocumentId first = workspace.ActiveTab();
	DirtyBuffer(editor);
	workspace.NewTab();
	editor.LoadInitialBuffer("second\n");
	const DocumentId second = workspace.ActiveTab();
	DirtyBuffer(editor);
	(void)workspace.TakeRequests();

	workspace.RequestClose();
	REQUIRE(workspace.PromptTab() == first);
	workspace.Choose(UnsavedChoice::Save);
	REQUIRE(HasRequest(workspace.TakeRequests(), DocumentShellRequest::ShowSaveAs));
	workspace.RegisterSaveAsRequest(77);

	workspace.HandlePortalResult(77, true, {file.path});
	CHECK(workspace.PromptActive());
	CHECK(workspace.Pending() == UnsavedPending::CloseWindow);
	CHECK(workspace.PromptTab() == second);
	CHECK(workspace.ActiveTab() == second);
	CHECK_FALSE(editor.Modified(first));
	CHECK(editor.Modified(second));
	// Prompt blocks tab switches; the card still names the next dirty tab.
	workspace.ActivateTab(first);
	CHECK(workspace.ActiveTab() == second);
	CHECK_FALSE(HasRequest(workspace.TakeRequests(),
		DocumentShellRequest::AcceptClose));
	const auto read = Scalpel::ReadDocumentFile(file.path);
	REQUIRE(read.has_value());
	CHECK(read->find('x') != std::string::npos);
}
