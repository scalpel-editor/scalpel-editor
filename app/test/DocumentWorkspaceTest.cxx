#include "catch.hpp"

#include <cstdio>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ApplicationEditor.h"
#include "DocumentFile.h"
#include "DocumentWorkspace.h"

using Scalpel::ApplicationEditor;
using Scalpel::DocumentFileErrorReason;
using Scalpel::DocumentFileOperation;
using Scalpel::DocumentId;
using Scalpel::DocumentShellRequest;
using Scalpel::DocumentWorkspace;
using Scalpel::ExternalChangeChoice;
using Scalpel::LargeFileChoice;
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

/** Sparse regular file of the given reported size; does not allocate payload. */
class SparseTempFile {
public:
	explicit SparseTempFile(off_t size) {
		char pattern[] = "/tmp/scalpel-workspace-sparse-XXXXXX";
		const int fd = mkstemp(pattern);
		REQUIRE(fd >= 0);
		path = pattern;
		REQUIRE(ftruncate(fd, size) == 0);
		REQUIRE(close(fd) == 0);
	}
	~SparseTempFile() {
		if (!path.empty()) {
			(void)std::remove(path.c_str());
		}
	}
	SparseTempFile(const SparseTempFile &) = delete;
	SparseTempFile &operator=(const SparseTempFile &) = delete;

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

/** In-place rewrite that changes size so save stamps always diverge. */
void ExternallyRewrite(const std::string &path, std::string_view contents) {
	const int fd = ::open(path.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC);
	REQUIRE(fd >= 0);
	const ssize_t written = write(fd, contents.data(), contents.size());
	REQUIRE(written == static_cast<ssize_t>(contents.size()));
	REQUIRE(close(fd) == 0);
}

/** Open a known path, dirty the buffer, and change the file on disk. */
void OpenDirtyAndConflict(DocumentWorkspace &workspace,
	ApplicationEditor &editor, const std::string &path,
	std::string_view externalContents = "external-on-disk") {
	REQUIRE(workspace.OpenPath(path));
	(void)workspace.TakeRequests();
	(void)workspace.TakeRecentPaths();
	DirtyBuffer(editor);
	ExternallyRewrite(path, externalContents);
}

}

TEST_CASE("document workspace external change save detects conflict without overwrite") {
	TempFile file("baseline");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	OpenDirtyAndConflict(workspace, editor, file.path);
	const std::string editorText = editor.Text();

	workspace.RequestSave();
	CHECK(workspace.ExternalChangePromptActive());
	CHECK(workspace.ExternalChangePromptPath() == file.path);
	CHECK(workspace.ExternalChangePromptTab() == workspace.ActiveTab());
	CHECK(editor.Modified());
	CHECK(editor.Text() == editorText);
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::PromptBegan));

	const auto onDisk =
		Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(onDisk.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(onDisk.bytes == "external-on-disk");
	CHECK(workspace.TakeFileErrors().empty());
}

TEST_CASE("document workspace external change overwrite writes without expected stamp") {
	TempFile file("baseline");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	OpenDirtyAndConflict(workspace, editor, file.path);
	const std::string editorText = editor.Text();

	workspace.RequestSave();
	REQUIRE(workspace.ExternalChangePromptActive());
	(void)workspace.TakeRequests();

	workspace.ChooseExternalChange(ExternalChangeChoice::Overwrite);
	CHECK_FALSE(workspace.ExternalChangePromptActive());
	CHECK_FALSE(editor.Modified());
	CHECK(editor.Text() == editorText);

	const auto onDisk =
		Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(onDisk.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(onDisk.bytes == editorText);
}

TEST_CASE("document workspace external change reload replaces buffer from disk") {
	TempFile file("baseline");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	OpenDirtyAndConflict(workspace, editor, file.path, "fresh-external");

	workspace.RequestSave();
	REQUIRE(workspace.ExternalChangePromptActive());
	(void)workspace.TakeRequests();

	workspace.ChooseExternalChange(ExternalChangeChoice::Reload);
	CHECK_FALSE(workspace.ExternalChangePromptActive());
	CHECK_FALSE(editor.Modified());
	CHECK(editor.Text() == "fresh-external");
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::RefreshTabs));
}

TEST_CASE("document workspace external change cancel leaves buffer and disk") {
	TempFile file("baseline");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	OpenDirtyAndConflict(workspace, editor, file.path);
	const std::string editorText = editor.Text();

	workspace.RequestSave();
	REQUIRE(workspace.ExternalChangePromptActive());
	(void)workspace.TakeRequests();

	workspace.ChooseExternalChange(ExternalChangeChoice::Cancel);
	CHECK_FALSE(workspace.ExternalChangePromptActive());
	CHECK(editor.Modified());
	CHECK(editor.Text() == editorText);

	const auto onDisk =
		Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(onDisk.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(onDisk.bytes == "external-on-disk");
}

TEST_CASE("document workspace external change Save As to another path") {
	TempFile file("baseline");
	TempFile other("");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	OpenDirtyAndConflict(workspace, editor, file.path);
	const std::string editorText = editor.Text();

	workspace.RequestSave();
	REQUIRE(workspace.ExternalChangePromptActive());
	(void)workspace.TakeRequests();

	workspace.ChooseExternalChange(ExternalChangeChoice::SaveAs);
	CHECK_FALSE(workspace.ExternalChangePromptActive());
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::ShowSaveAs));

	workspace.HandleSaveResult(true, other.path);
	CHECK(workspace.Path() == other.path);
	CHECK_FALSE(editor.Modified());
	CHECK(editor.Text() == editorText);

	const auto original =
		Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(original.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(original.bytes == "external-on-disk");
	const auto saved =
		Scalpel::ReadDocumentFile(other.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(saved.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(saved.bytes == editorText);
}

TEST_CASE("document workspace external change Save As back to the guarded path") {
	TempFile file("baseline");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	OpenDirtyAndConflict(workspace, editor, file.path);
	const std::string editorText = editor.Text();

	workspace.RequestSave();
	REQUIRE(workspace.ExternalChangePromptActive());
	(void)workspace.TakeRequests();

	workspace.ChooseExternalChange(ExternalChangeChoice::SaveAs);
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::ShowSaveAs));

	// Same bound path is still guarded.
	workspace.HandleSaveResult(true, file.path);
	CHECK(workspace.ExternalChangePromptActive());
	CHECK(workspace.ExternalChangePromptPath() == file.path);
	CHECK(editor.Modified());
	CHECK(editor.Text() == editorText);

	const auto onDisk =
		Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(onDisk.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(onDisk.bytes == "external-on-disk");
}

TEST_CASE("document workspace external change second change while card is open") {
	TempFile file("baseline");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	OpenDirtyAndConflict(workspace, editor, file.path, "first-external");

	workspace.RequestSave();
	REQUIRE(workspace.ExternalChangePromptActive());
	(void)workspace.TakeRequests();

	// Disk changes again while the user is deciding.
	ExternallyRewrite(file.path, "second-external");

	workspace.ChooseExternalChange(ExternalChangeChoice::Reload);
	CHECK_FALSE(workspace.ExternalChangePromptActive());
	CHECK(editor.Text() == "second-external");
	CHECK_FALSE(editor.Modified());
}

TEST_CASE("document workspace external change dirty-close overwrite continues close") {
	TempFile file("baseline");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	OpenDirtyAndConflict(workspace, editor, file.path);
	const std::string editorText = editor.Text();

	workspace.RequestClose();
	REQUIRE(workspace.PromptActive());
	(void)workspace.TakeRequests();

	workspace.Choose(UnsavedChoice::Save);
	REQUIRE(workspace.ExternalChangePromptActive());
	CHECK(workspace.PromptActive());
	(void)workspace.TakeRequests();

	workspace.ChooseExternalChange(ExternalChangeChoice::Overwrite);
	CHECK_FALSE(workspace.ExternalChangePromptActive());
	CHECK_FALSE(workspace.PromptActive());
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::AcceptClose));

	const auto onDisk =
		Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(onDisk.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(onDisk.bytes == editorText);
}

TEST_CASE("document workspace external change dirty-close reload continues close") {
	TempFile file("baseline");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	REQUIRE(workspace.OpenPath(file.path));
	(void)workspace.TakeRecentPaths();
	DirtyBuffer(editor);
	ExternallyRewrite(file.path, "disk-wins");

	workspace.CloseTab(workspace.ActiveTab());
	REQUIRE(workspace.PromptActive());
	REQUIRE(workspace.Pending() == UnsavedPending::CloseTab);
	(void)workspace.TakeRequests();

	workspace.Choose(UnsavedChoice::Save);
	REQUIRE(workspace.ExternalChangePromptActive());
	(void)workspace.TakeRequests();

	workspace.ChooseExternalChange(ExternalChangeChoice::Reload);
	CHECK_FALSE(workspace.ExternalChangePromptActive());
	CHECK_FALSE(workspace.PromptActive());
	// CloseTab after reload removes the tab (last tab becomes untitled).
	CHECK(workspace.Path().empty());
	CHECK_FALSE(editor.Modified());
}

TEST_CASE("document workspace external change dirty-close cancel keeps close prompt") {
	TempFile file("baseline");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	OpenDirtyAndConflict(workspace, editor, file.path);

	workspace.RequestClose();
	REQUIRE(workspace.PromptActive());
	(void)workspace.TakeRequests();

	workspace.Choose(UnsavedChoice::Save);
	REQUIRE(workspace.ExternalChangePromptActive());
	(void)workspace.TakeRequests();

	workspace.ChooseExternalChange(ExternalChangeChoice::Cancel);
	CHECK_FALSE(workspace.ExternalChangePromptActive());
	CHECK(workspace.PromptActive());
	CHECK(workspace.Pending() == UnsavedPending::CloseWindow);
	CHECK(editor.Modified());
}

TEST_CASE("document workspace external change dirty-close Save As continues close") {
	TempFile file("baseline");
	TempFile other("");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	OpenDirtyAndConflict(workspace, editor, file.path);
	const std::string editorText = editor.Text();

	workspace.RequestClose();
	REQUIRE(workspace.PromptActive());
	(void)workspace.TakeRequests();

	workspace.Choose(UnsavedChoice::Save);
	REQUIRE(workspace.ExternalChangePromptActive());
	(void)workspace.TakeRequests();

	workspace.ChooseExternalChange(ExternalChangeChoice::SaveAs);
	CHECK_FALSE(workspace.ExternalChangePromptActive());
	CHECK(workspace.AwaitingSaveAs());
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::ShowSaveAs));

	workspace.HandleSaveResult(true, other.path);
	CHECK_FALSE(workspace.PromptActive());
	CHECK(workspace.Path() == other.path);
	CHECK_FALSE(editor.Modified());
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::AcceptClose));
	CHECK(editor.Text() == editorText);
}

TEST_CASE("document workspace external change delayed Save As targets captured tab") {
	TempFile first("first-body");
	TempFile second("second-body");
	TempFile saveAsTarget("");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	REQUIRE(workspace.OpenPath(first.path));
	REQUIRE(workspace.OpenPath(second.path));
	(void)workspace.TakeRecentPaths();
	(void)workspace.TakeRequests();

	DocumentId firstId = 0;
	for (const auto &tab : workspace.Tabs()) {
		if (tab.path == first.path) {
			firstId = tab.id;
		}
	}
	REQUIRE(firstId != 0);
	// second remains open so path-already-open rules stay realistic.
	CHECK(workspace.TabCount() >= 2);
	(void)second;

	workspace.ActivateTab(firstId);
	DirtyBuffer(editor);
	ExternallyRewrite(first.path, "first-external-long");

	workspace.RequestSave();
	REQUIRE(workspace.ExternalChangePromptActive());
	CHECK(workspace.ExternalChangePromptTab() == firstId);
	(void)workspace.TakeRequests();

	workspace.ChooseExternalChange(ExternalChangeChoice::SaveAs);
	CHECK_FALSE(workspace.ExternalChangePromptActive());
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::ShowSaveAs));

	// Capture the intent before any other shell work so the dialog still names
	// the conflicted tab even if focus would otherwise move later.
	const auto intent = workspace.BeginSaveAsDialog();
	CHECK(intent.documentPath == first.path);

	workspace.HandleDialogResult(intent.id, true, {saveAsTarget.path});
	bool foundFirst = false;
	for (const auto &tab : workspace.Tabs()) {
		if (tab.id == firstId) {
			foundFirst = true;
			CHECK(tab.path == saveAsTarget.path);
			CHECK_FALSE(tab.dirty);
		}
	}
	CHECK(foundFirst);

	const auto saved = Scalpel::ReadDocumentFile(saveAsTarget.path,
		Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(saved.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(saved.bytes.find('x') != std::string::npos);
}

TEST_CASE("document workspace external change drops a stale decision after Save As") {
	TempFile original("baseline");
	TempFile saveAsTarget("");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	REQUIRE(workspace.OpenPath(original.path));
	(void)workspace.TakeRecentPaths();
	(void)workspace.TakeRequests();
	DirtyBuffer(editor);
	const std::string editorText = editor.Text();

	// A portal result may still arrive after another save discovers a conflict.
	workspace.RequestSaveAs();
	REQUIRE(HasRequest(workspace.TakeRequests(),
		DocumentShellRequest::ShowSaveAs));
	const auto delayedSaveAs = workspace.BeginSaveAsDialog();
	ExternallyRewrite(original.path, "external-on-disk");
	workspace.RequestSave();
	REQUIRE(workspace.ExternalChangePromptActive());
	(void)workspace.TakeRequests();

	workspace.HandleDialogResult(
		delayedSaveAs.id, true, {saveAsTarget.path});
	REQUIRE(workspace.Path() == saveAsTarget.path);
	REQUIRE_FALSE(editor.Modified());
	CHECK_FALSE(workspace.ExternalChangePromptActive());

	// The delayed result superseded the decision, so its old action is inert.
	workspace.ChooseExternalChange(ExternalChangeChoice::Overwrite);
	CHECK(workspace.Path() == saveAsTarget.path);
	CHECK(editor.Text() == editorText);

	const auto originalText = Scalpel::ReadDocumentFile(
		original.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(originalText.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(originalText.bytes == "external-on-disk");
	const auto savedText = Scalpel::ReadDocumentFile(
		saveAsTarget.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(savedText.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(savedText.bytes == editorText);
}

TEST_CASE("document workspace external change missing destination") {
	TempFile file("baseline");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	REQUIRE(workspace.OpenPath(file.path));
	(void)workspace.TakeRecentPaths();
	DirtyBuffer(editor);
	REQUIRE(std::remove(file.path.c_str()) == 0);
	file.path.clear();

	// Path string still bound on the tab from OpenPath.
	const std::string bound = workspace.Path();
	REQUIRE_FALSE(bound.empty());
	workspace.RequestSave();
	CHECK(workspace.ExternalChangePromptActive());
	CHECK(workspace.ExternalChangePromptPath() == bound);
	CHECK(editor.Modified());
	CHECK(access(bound.c_str(), F_OK) != 0);
}

TEST_CASE("document workspace external change unreadable destination") {
	TempFile file("baseline");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	REQUIRE(workspace.OpenPath(file.path));
	(void)workspace.TakeRecentPaths();
	DirtyBuffer(editor);

	// Replace the regular file with a directory so inspection fails the stamp match.
	REQUIRE(std::remove(file.path.c_str()) == 0);
	REQUIRE(mkdir(file.path.c_str(), 0755) == 0);

	workspace.RequestSave();
	CHECK(workspace.ExternalChangePromptActive());
	CHECK(workspace.ExternalChangePromptPath() == file.path);
	CHECK(editor.Modified());

	REQUIRE(rmdir(file.path.c_str()) == 0);
	file.path.clear();
}

TEST_CASE("document workspace external change blocks other decisions") {
	TempFile file("baseline");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	OpenDirtyAndConflict(workspace, editor, file.path);

	workspace.RequestSave();
	REQUIRE(workspace.ExternalChangePromptActive());
	(void)workspace.TakeRequests();

	const std::size_t tabCount = workspace.TabCount();
	const DocumentId active = workspace.ActiveTab();
	workspace.NewTab();
	CHECK(workspace.TabCount() == tabCount);
	CHECK(workspace.ActiveTab() == active);
	workspace.RequestOpen();
	CHECK(workspace.TakeRequests().empty());
	workspace.RequestClose();
	CHECK_FALSE(HasRequest(workspace.TakeRequests(),
		DocumentShellRequest::AcceptClose));
	CHECK(workspace.ExternalChangePromptActive());
}

TEST_CASE("document workspace external change startup stamps guard later saves") {
	TempFile file("startup-body");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	REQUIRE(workspace.LoadStartupFiles({file.path}));
	(void)workspace.TakeRequests();

	DirtyBuffer(editor);
	ExternallyRewrite(file.path, "changed-after-startup");
	workspace.RequestSave();
	CHECK(workspace.ExternalChangePromptActive());
	CHECK(workspace.ExternalChangePromptPath() == file.path);

	const auto onDisk =
		Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(onDisk.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(onDisk.bytes == "changed-after-startup");
}

TEST_CASE("document workspace external change failed reload keeps decision") {
	TempFile file("baseline");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	OpenDirtyAndConflict(workspace, editor, file.path);

	workspace.RequestSave();
	REQUIRE(workspace.ExternalChangePromptActive());
	(void)workspace.TakeRequests();

	REQUIRE(std::remove(file.path.c_str()) == 0);
	file.path.clear();

	workspace.ChooseExternalChange(ExternalChangeChoice::Reload);
	CHECK(workspace.ExternalChangePromptActive());
	const auto errors = workspace.TakeFileErrors();
	REQUIRE(errors.size() == 1);
	CHECK(errors[0].operation == DocumentFileOperation::Open);
	CHECK(editor.Modified());
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
	const auto read = Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(read.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(read.bytes == editor.Text());
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

TEST_CASE("document workspace preserves invalid UTF-8 file bytes through open edit and save") {
	// Stray continuation, truncated lead, and overlong sequence mixed with valid UTF-8.
	const std::string fixture =
		"ok\x80" "mid\xC2" "\xC0\x80" "end";
	TempFile file(fixture);
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);

	REQUIRE(workspace.OpenPath(file.path));
	CHECK(workspace.Path() == file.path);
	CHECK(editor.Text() == fixture);
	CHECK_FALSE(editor.Modified());
	CHECK_FALSE(workspace.BufferModified());
	{
		const std::vector<std::string> recent = workspace.TakeRecentPaths();
		REQUIRE(recent.size() == 1);
		CHECK(recent[0] == file.path);
	}
	CHECK(workspace.TakeFileErrors().empty());

	// Unchanged save must reproduce the fixture bytes exactly.
	workspace.RequestSave();
	CHECK_FALSE(editor.Modified());
	CHECK(workspace.TakeFileErrors().empty());
	{
		const auto read = Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
		REQUIRE(read.status == Scalpel::DocumentFileReadStatus::Success);
		CHECK(read.bytes == fixture);
	}

	// Normal text edit adjacent to the trailing malformed range; untouched bytes stay exact.
	DirtyBuffer(editor);
	const std::string afterEdit = fixture + "x";
	CHECK(editor.Text() == afterEdit);
	workspace.RequestSave();
	CHECK_FALSE(editor.Modified());
	CHECK(workspace.TakeFileErrors().empty());
	{
		const auto read = Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
		REQUIRE(read.status == Scalpel::DocumentFileReadStatus::Success);
		CHECK(read.bytes == afterEdit);
	}
	CHECK(workspace.TakeRecentPaths().empty());
}

TEST_CASE("document workspace recent paths report only successful file use") {
	TempFile first("first\n");
	TempFile second("second\n");
	const std::string missing =
		"/tmp/scalpel-missing-" + std::to_string(getpid());
	(void)std::remove(missing.c_str());
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);

	workspace.HandleOpenResult(true, {first.path, missing, second.path});
	const std::vector<std::string> opened = workspace.TakeRecentPaths();
	REQUIRE(opened.size() == 2);
	CHECK(opened[0] == first.path);
	CHECK(opened[1] == second.path);

	CHECK(workspace.OpenPath(first.path));
	REQUIRE(workspace.TakeRecentPaths().size() == 1);

	TempFile saveTarget("");
	workspace.HandleSaveResult(true, saveTarget.path);
	const std::vector<std::string> saved = workspace.TakeRecentPaths();
	REQUIRE(saved.size() == 1);
	CHECK(saved.front() == saveTarget.path);
}

TEST_CASE("document workspace file errors report failed opens and saves") {
	const std::string missing =
		"/tmp/scalpel-missing-" + std::to_string(getpid());
	(void)std::remove(missing.c_str());
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("body\n");
	DocumentWorkspace workspace(editor);

	CHECK_FALSE(workspace.OpenPath(missing));
	std::vector<Scalpel::DocumentFileError> errors = workspace.TakeFileErrors();
	REQUIRE(errors.size() == 1);
	CHECK(errors[0].operation == DocumentFileOperation::Open);
	CHECK(errors[0].path == missing);

	const std::string unwritable = "/no-such-directory/scalpel/file.txt";
	workspace.HandleSaveResult(true, unwritable);
	errors = workspace.TakeFileErrors();
	REQUIRE(errors.size() == 1);
	CHECK(errors[0].operation == DocumentFileOperation::Save);
	CHECK(errors[0].path == unwritable);
	CHECK(workspace.TakeFileErrors().empty());
}

TEST_CASE("document workspace open rejects a directory path without throwing") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);
	const DocumentId startup = workspace.ActiveTab();
	const std::string startupText = editor.Text();

	CHECK_FALSE(workspace.OpenPath("/tmp"));
	CHECK(workspace.ActiveTab() == startup);
	CHECK(editor.Text() == startupText);
	CHECK(workspace.TabCount() == 1);
	const std::vector<Scalpel::DocumentFileError> errors =
		workspace.TakeFileErrors();
	REQUIRE(errors.size() == 1);
	CHECK(errors[0].operation == DocumentFileOperation::Open);
	CHECK(errors[0].path == "/tmp");
	CHECK(workspace.TakeRecentPaths().empty());
}

TEST_CASE("document workspace open path is no-op while dirty prompt is active") {
	TempFile file("other\n");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("dirty\n");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);
	const DocumentId dirtyId = workspace.ActiveTab();

	workspace.RequestClose();
	REQUIRE(workspace.PromptActive());
	(void)workspace.TakeRequests();

	CHECK_FALSE(workspace.OpenPath(file.path));
	CHECK(workspace.PromptActive());
	CHECK(workspace.ActiveTab() == dirtyId);
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.TakeRecentPaths().empty());
	CHECK(workspace.TakeFileErrors().empty());

	// Portal open results share ApplyOpenPaths and must also refuse.
	workspace.HandleOpenResult(true, file.path);
	CHECK(workspace.PromptActive());
	CHECK(workspace.ActiveTab() == dirtyId);
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.TakeRecentPaths().empty());
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
		const auto read = Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
		REQUIRE(read.status == Scalpel::DocumentFileReadStatus::Success);
		CHECK(read.bytes == editor.Text());
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

TEST_CASE("document workspace Save As refuses a path already open in another tab") {
	TempFile first("first body\n");
	TempFile second("second body\n");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);

	REQUIRE(workspace.OpenPath(first.path));
	const DocumentId firstId = workspace.ActiveTab();
	REQUIRE(workspace.OpenPath(second.path));
	const DocumentId secondId = workspace.ActiveTab();
	REQUIRE(firstId != secondId);
	DirtyBuffer(editor);
	const std::string dirtySecond = editor.Text();
	(void)workspace.TakeRecentPaths();
	(void)workspace.TakeFileErrors();
	(void)workspace.TakeRequests();

	// Save the dirty second tab under the first tab's path must not rebind or
	// write: path uniqueness matches OpenPath, and first's on-disk bytes stay.
	workspace.RequestSaveAs();
	REQUIRE(HasRequest(workspace.TakeRequests(), DocumentShellRequest::ShowSaveAs));
	const auto dialog = workspace.BeginSaveAsDialog();
	workspace.HandleDialogResult(dialog.id, true, {first.path});

	const std::vector<Scalpel::DocumentFileError> errors =
		workspace.TakeFileErrors();
	REQUIRE(errors.size() == 1);
	CHECK(errors[0].operation == DocumentFileOperation::Save);
	CHECK(errors[0].path == first.path);
	CHECK(workspace.TakeRecentPaths().empty());
	CHECK(workspace.ActiveTab() == secondId);
	CHECK(workspace.Path() == second.path);
	CHECK(editor.Text() == dirtySecond);
	CHECK(editor.Modified());

	workspace.ActivateTab(firstId);
	CHECK(workspace.Path() == first.path);
	CHECK(editor.Text() == "first body\n");
	CHECK_FALSE(editor.Modified());
	const auto onDisk = Scalpel::ReadDocumentFile(first.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(onDisk.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(onDisk.bytes == "first body\n");
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
	const auto read = Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(read.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(read.bytes.find('x') != std::string::npos);
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

TEST_CASE("document workspace dialog routing open captures identity") {
	TempFile a("alpha\n");
	TempFile b("beta\n");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);
	const DocumentId startup = workspace.ActiveTab();

	workspace.RequestOpen();
	REQUIRE(HasRequest(workspace.TakeRequests(), DocumentShellRequest::ShowOpen));
	const auto dialog = workspace.BeginOpenDialog();

	workspace.HandleDialogResult(dialog.id, true, {a.path, b.path});
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

TEST_CASE("document workspace dialog routing ignores unknown identity") {
	TempFile file("ignored\n");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);

	workspace.HandleDialogResult({99}, true, {file.path});
	CHECK(workspace.TabCount() == 1);
	CHECK(editor.Text() == "startup\n");
	CHECK(workspace.Path().empty());
}

TEST_CASE("document workspace dialog routing open cancel is a no-op") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);
	const auto dialog = workspace.BeginOpenDialog();
	workspace.HandleDialogResult(dialog.id, false, {});
	CHECK(workspace.TabCount() == 1);
	CHECK(editor.Text() == "startup\n");
}

TEST_CASE("document workspace dialog routing Save As targets initiating tab") {
	TempFile file("");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("keep me");
	DocumentWorkspace workspace(editor);
	const DocumentId first = workspace.ActiveTab();
	DirtyBuffer(editor);
	const std::string dirtyText = editor.Text();

	workspace.RequestSaveAs();
	REQUIRE(HasRequest(workspace.TakeRequests(), DocumentShellRequest::ShowSaveAs));
	const auto dialog = workspace.BeginSaveAsDialog();

	// Switch away while the portal is open; the result must still hit first.
	workspace.NewTab();
	editor.LoadInitialBuffer("other");
	const DocumentId second = workspace.ActiveTab();
	REQUIRE(second != first);

	workspace.HandleDialogResult(dialog.id, true, {file.path});
	CHECK(workspace.ActiveTab() == second);
	CHECK(editor.Text() == "other");
	workspace.ActivateTab(first);
	CHECK(workspace.Path() == file.path);
	CHECK(editor.Text() == dirtyText);
	CHECK_FALSE(editor.Modified());
	const auto read = Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(read.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(read.bytes == dirtyText);
}

TEST_CASE("document workspace dialog routing Save As ignores closed tab") {
	TempFile file("");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("gone");
	DocumentWorkspace workspace(editor);
	const DocumentId first = workspace.ActiveTab();
	workspace.RequestSaveAs();
	(void)workspace.TakeRequests();
	const auto dialog = workspace.BeginSaveAsDialog();

	workspace.NewTab();
	const DocumentId second = workspace.ActiveTab();
	workspace.CloseTab(first);
	REQUIRE_FALSE(editor.HasDocument(first));
	REQUIRE(workspace.ActiveTab() == second);

	workspace.HandleDialogResult(dialog.id, true, {file.path});
	CHECK(workspace.Path().empty());
	CHECK(workspace.TabCount() == 1);
	const auto read = Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
	// No write should have occurred for a tab that no longer exists.
	REQUIRE(read.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(read.bytes.empty());
}

TEST_CASE("document workspace dialog routing delayed Save As continues prompt") {
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
	const auto dialog = workspace.BeginSaveAsDialog();

	workspace.HandleDialogResult(dialog.id, true, {file.path});
	CHECK_FALSE(workspace.PromptActive());
	CHECK_FALSE(editor.HasDocument(dirty));
	CHECK(workspace.TabCount() == 1);
	const auto read = Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(read.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(read.bytes.find('x') != std::string::npos);
}

TEST_CASE("document workspace dialog routing Save As cancel keeps prompt") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("untitled");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);
	workspace.RequestClose();
	(void)workspace.TakeRequests();
	workspace.Choose(UnsavedChoice::Save);
	(void)workspace.TakeRequests();
	REQUIRE(workspace.AwaitingSaveAs());
	const auto dialog = workspace.BeginSaveAsDialog();

	workspace.HandleDialogResult(dialog.id, false, {});
	CHECK(workspace.PromptActive());
	CHECK(workspace.Pending() == UnsavedPending::CloseWindow);
	CHECK_FALSE(workspace.AwaitingSaveAs());
}

TEST_CASE("document workspace dialog routing stale Save As keeps newer prompt") {
	TempFile file("");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("untitled");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);

	workspace.RequestClose();
	(void)workspace.TakeRequests();
	workspace.Choose(UnsavedChoice::Save);
	(void)workspace.TakeRequests();
	const auto dialog = workspace.BeginSaveAsDialog();

	// Cancelling the card abandons this close decision, but the portal request
	// may still deliver a result after another close decision has begun.
	workspace.Choose(UnsavedChoice::Cancel);
	REQUIRE_FALSE(workspace.PromptActive());
	workspace.RequestClose();
	(void)workspace.TakeRequests();
	REQUIRE(workspace.PromptActive());
	REQUIRE_FALSE(workspace.AwaitingSaveAs());

	workspace.HandleDialogResult(dialog.id, true, {file.path});
	CHECK(workspace.PromptActive());
	CHECK(workspace.Pending() == UnsavedPending::CloseWindow);
	CHECK_FALSE(HasRequest(workspace.TakeRequests(),
		DocumentShellRequest::AcceptClose));
	CHECK(workspace.Path() == file.path);
	CHECK_FALSE(editor.Modified());
}

TEST_CASE("document workspace dialog routing open selects existing among many") {
	TempFile existing("once\n");
	TempFile extra("twice\n");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);

	workspace.HandleOpenResult(true, existing.path);
	const DocumentId opened = workspace.ActiveTab();
	workspace.NewTab();
	REQUIRE(workspace.TabCount() == 3);

	const auto dialog = workspace.BeginOpenDialog();
	workspace.HandleDialogResult(
		dialog.id, true, {existing.path, extra.path});
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

TEST_CASE("document workspace dialog routing abandons failed Save As") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("untitled");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);
	workspace.RequestClose();
	(void)workspace.TakeRequests();
	workspace.Choose(UnsavedChoice::Save);
	(void)workspace.TakeRequests();
	REQUIRE(workspace.AwaitingSaveAs());

	const auto dialog = workspace.BeginSaveAsDialog();
	workspace.AbandonDialog(dialog.id);
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
	const auto dialog = workspace.BeginSaveAsDialog();

	workspace.HandleDialogResult(dialog.id, true, {file.path});
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
	const auto read = Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(read.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(read.bytes.find('x') != std::string::npos);
}

// Combined multi-document flows exercise several operations in one session,
// matching the product paths wired through main rather than isolated units.

TEST_CASE("document workspace multi-document open-many edit switch and inactive Save As") {
	TempFile alpha("alpha body\n");
	TempFile beta("beta body\n");
	TempFile saveAsTarget("");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);
	const DocumentId startup = workspace.ActiveTab();
	const std::string alphaAlias =
		"/tmp/./" + Scalpel::DocumentBaseName(alpha.path);

	// Open many: every distinct normalized path becomes its own tab.
	const auto openDialog = workspace.BeginOpenDialog();
	workspace.HandleDialogResult(
		openDialog.id, true, {alpha.path, alphaAlias, beta.path});
	REQUIRE(workspace.TabCount() == 3);
	CHECK(workspace.Path() == beta.path);
	CHECK(editor.Text() == "beta body\n");
	CHECK(HasRequest(workspace.TakeRequests(), DocumentShellRequest::RefreshTabs));

	// Edit and switch: each document keeps independent text and dirty state.
	DirtyBuffer(editor);
	const std::string dirtyBeta = editor.Text();
	const DocumentId betaId = workspace.ActiveTab();
	workspace.ActivateTab(startup);
	CHECK(editor.Text() == "startup\n");
	CHECK_FALSE(editor.Modified());
	workspace.ActivateTab(betaId);
	CHECK(editor.Text() == dirtyBeta);
	CHECK(editor.Modified());

	// Save As was started on beta; finishing it while another tab is active
	// still writes beta and leaves the visible tab alone.
	workspace.RequestSaveAs();
	REQUIRE(HasRequest(workspace.TakeRequests(), DocumentShellRequest::ShowSaveAs));
	const auto saveDialog = workspace.BeginSaveAsDialog();
	workspace.ActivateTab(startup);
	REQUIRE(workspace.ActiveTab() == startup);
	workspace.HandleDialogResult(saveDialog.id, true, {saveAsTarget.path});
	CHECK(workspace.ActiveTab() == startup);
	CHECK(editor.Text() == "startup\n");
	workspace.ActivateTab(betaId);
	CHECK(workspace.Path() == saveAsTarget.path);
	CHECK_FALSE(editor.Modified());
	const auto saved = Scalpel::ReadDocumentFile(saveAsTarget.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(saved.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(saved.bytes == dirtyBeta);

	// Alpha remains as opened, still clean.
	bool foundAlpha = false;
	for (const auto &tab : workspace.Tabs()) {
		if (tab.path == alpha.path) {
			foundAlpha = true;
			workspace.ActivateTab(tab.id);
			CHECK(editor.Text() == "alpha body\n");
			CHECK_FALSE(editor.Modified());
		}
	}
	CHECK(foundAlpha);
}

TEST_CASE("document workspace multi-document close dirty inactive cancel window close and last tab") {
	TempFile known("known\n");
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
	(void)workspace.TakeRequests();

	// Close a dirty inactive tab: it becomes active and receives the card.
	workspace.ActivateTab(first);
	(void)workspace.TakeRequests();
	workspace.CloseTab(second);
	CHECK(workspace.ActiveTab() == second);
	CHECK(workspace.PromptActive());
	CHECK(workspace.Pending() == UnsavedPending::CloseTab);
	CHECK(workspace.PromptTab() == second);
	workspace.Choose(UnsavedChoice::Discard);
	CHECK_FALSE(workspace.PromptActive());
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.ActiveTab() == first);
	CHECK_FALSE(editor.HasDocument(second));
	CHECK(editor.Text() == firstText);

	// Rebuild two dirty tabs, then cancel a multi-dirty window close.
	workspace.NewTab();
	editor.LoadInitialBuffer("third\n");
	const DocumentId third = workspace.ActiveTab();
	DirtyBuffer(editor);
	const std::string thirdText = editor.Text();
	(void)workspace.TakeRequests();

	workspace.RequestClose();
	REQUIRE(workspace.PromptTab() == first);
	workspace.Choose(UnsavedChoice::Discard);
	REQUIRE(workspace.PromptTab() == third);
	workspace.Choose(UnsavedChoice::Cancel);
	CHECK_FALSE(workspace.PromptActive());
	CHECK(workspace.TabCount() == 2);
	CHECK(editor.HasDocument(first));
	CHECK(editor.HasDocument(third));
	CHECK_FALSE(HasRequest(workspace.TakeRequests(),
		DocumentShellRequest::AcceptClose));
	workspace.ActivateTab(first);
	CHECK(editor.Text() == firstText);
	CHECK(editor.Modified(first));
	workspace.ActivateTab(third);
	CHECK(editor.Text() == thirdText);
	CHECK(editor.Modified(third));

	// Closing the last clean tab creates a fresh untitled instead of quitting.
	workspace.ActivateTab(first);
	workspace.HandleSaveResult(true, known.path);
	(void)workspace.TakeRequests();
	REQUIRE_FALSE(editor.Modified(first));
	workspace.CloseTab(third);
	REQUIRE(workspace.PromptActive());
	workspace.Choose(UnsavedChoice::Discard);
	REQUIRE(workspace.TabCount() == 1);
	REQUIRE(workspace.ActiveTab() == first);
	workspace.CloseTab(first);
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.ActiveTab() != first);
	CHECK_FALSE(editor.HasDocument(first));
	CHECK(editor.Text().empty());
	CHECK(workspace.Path().empty());
	// first was Untitled 1, second 2, third 3; next free number is 4.
	CHECK(workspace.Tabs()[0].label == "Untitled 4");
	CHECK_FALSE(HasRequest(workspace.TakeRequests(),
		DocumentShellRequest::AcceptClose));
}

TEST_CASE("document workspace multi-document portal failure preserves tabs") {
	TempFile file("on disk\n");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);
	const DocumentId startup = workspace.ActiveTab();
	DirtyBuffer(editor);
	const std::string dirtyText = editor.Text();

	// Portal open failure (cancel or error) leaves tabs and dirty state alone.
	const auto failedOpen = workspace.BeginOpenDialog();
	workspace.HandleDialogResult(failedOpen.id, false, {});
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.ActiveTab() == startup);
	CHECK(editor.Text() == dirtyText);
	CHECK(editor.Modified());

	// Save As dialog failure while a close prompt awaits keeps the card.
	workspace.RequestClose();
	REQUIRE(workspace.PromptActive());
	workspace.Choose(UnsavedChoice::Save);
	REQUIRE(HasRequest(workspace.TakeRequests(), DocumentShellRequest::ShowSaveAs));
	REQUIRE(workspace.AwaitingSaveAs());
	const auto failedSave = workspace.BeginSaveAsDialog();
	workspace.AbandonDialog(failedSave.id);
	CHECK(workspace.PromptActive());
	CHECK(workspace.Pending() == UnsavedPending::CloseWindow);
	CHECK_FALSE(workspace.AwaitingSaveAs());
	CHECK_FALSE(HasRequest(workspace.TakeRequests(),
		DocumentShellRequest::AcceptClose));

	// A later successful open still creates a sibling tab.
	workspace.Choose(UnsavedChoice::Cancel);
	REQUIRE_FALSE(workspace.PromptActive());
	const auto laterOpen = workspace.BeginOpenDialog();
	workspace.HandleDialogResult(laterOpen.id, true, {file.path});
	CHECK(workspace.TabCount() == 2);
	CHECK(editor.Text() == "on disk\n");
	workspace.ActivateTab(startup);
	CHECK(editor.Text() == dirtyText);
	CHECK(editor.Modified(startup));
}

TEST_CASE("document workspace startup files load raw bytes into the sole tab") {
	// Stray continuation and truncated lead mixed with valid UTF-8.
	const std::string fixture = "msg\x80" "mid\xC2" "end\n";
	TempFile file(fixture);
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	const DocumentId only = workspace.ActiveTab();

	REQUIRE(workspace.LoadStartupFiles({file.path}));
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.ActiveTab() == only);
	CHECK(workspace.Path() == file.path);
	CHECK(editor.Text() == fixture);
	CHECK_FALSE(editor.Modified());
	CHECK_FALSE(workspace.BufferModified());
	CHECK(workspace.TakeRecentPaths().empty());
	CHECK(workspace.TakeFileErrors().empty());
	const auto requests = workspace.TakeRequests();
	CHECK(HasRequest(requests, DocumentShellRequest::RefreshTabs));
	const auto tabs = workspace.Tabs();
	REQUIRE(tabs.size() == 1);
	CHECK(tabs[0].label == Scalpel::DocumentBaseName(file.path));
	CHECK_FALSE(tabs[0].dirty);
}

TEST_CASE("document workspace startup file saves to its known path") {
	TempFile file("template subject\n\n# comment\n");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	REQUIRE(workspace.LoadStartupFiles({file.path}));
	(void)workspace.TakeRequests();

	DirtyBuffer(editor);
	workspace.RequestSave();
	CHECK_FALSE(editor.Modified());
	CHECK(workspace.Path() == file.path);
	CHECK(workspace.TakeFileErrors().empty());
	const auto read = Scalpel::ReadDocumentFile(file.path, Scalpel::DocumentFileHardLimitBytes);
	REQUIRE(read.status == Scalpel::DocumentFileReadStatus::Success);
	CHECK(read.bytes == editor.Text());
	// Interactive Open still records recent paths; startup load must not.
	CHECK(workspace.TakeRecentPaths().empty());
}

TEST_CASE("document workspace startup files opens ordered tabs and reuses first") {
	TempFile first("alpha body\n");
	TempFile second("beta body\n");
	TempFile third("gamma body\n");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	const DocumentId initial = workspace.ActiveTab();

	REQUIRE(workspace.LoadStartupFiles(
		{first.path, second.path, third.path}));
	CHECK(workspace.TabCount() == 3);
	CHECK(workspace.ActiveTab() != initial);
	CHECK(workspace.Path() == third.path);
	CHECK(editor.Text() == "gamma body\n");
	CHECK_FALSE(editor.Modified());
	CHECK(workspace.TakeRecentPaths().empty());
	CHECK(workspace.TakeFileErrors().empty());
	const auto requests = workspace.TakeRequests();
	CHECK(HasRequest(requests, DocumentShellRequest::RefreshTabs));
	const auto tabs = workspace.Tabs();
	REQUIRE(tabs.size() == 3);
	CHECK(tabs[0].id == initial);
	CHECK(tabs[0].path == first.path);
	CHECK(tabs[0].label == Scalpel::DocumentBaseName(first.path));
	CHECK_FALSE(tabs[0].dirty);
	CHECK_FALSE(tabs[0].active);
	CHECK(tabs[1].path == second.path);
	CHECK_FALSE(tabs[1].active);
	CHECK(tabs[2].path == third.path);
	CHECK(tabs[2].active);
	CHECK_FALSE(tabs[2].dirty);

	workspace.ActivateTab(initial);
	CHECK(editor.Text() == "alpha body\n");
	CHECK_FALSE(editor.Modified());
	workspace.ActivateTab(tabs[1].id);
	CHECK(editor.Text() == "beta body\n");
	CHECK_FALSE(editor.Modified());
}

TEST_CASE("document workspace startup files deduplicates and activates last path") {
	TempFile first("one\n");
	TempFile second("two\n");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	const DocumentId initial = workspace.ActiveTab();

	REQUIRE(workspace.LoadStartupFiles(
		{first.path, second.path, first.path}));
	CHECK(workspace.TabCount() == 2);
	CHECK(workspace.ActiveTab() == initial);
	CHECK(workspace.Path() == first.path);
	CHECK(editor.Text() == "one\n");
	const auto tabs = workspace.Tabs();
	REQUIRE(tabs.size() == 2);
	CHECK(tabs[0].path == first.path);
	CHECK(tabs[0].active);
	CHECK(tabs[1].path == second.path);
	CHECK_FALSE(tabs[1].active);
	CHECK(workspace.TakeRecentPaths().empty());
	CHECK(workspace.TakeFileErrors().empty());
}

TEST_CASE("document workspace startup files missing path fails without mutation") {
	TempFile first("kept\n");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	const DocumentId only = workspace.ActiveTab();
	const std::string missing = "/tmp/scalpel-startup-missing-XXXX-not-present";

	CHECK_FALSE(workspace.LoadStartupFiles({first.path, missing}));
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.ActiveTab() == only);
	CHECK(workspace.Path().empty());
	CHECK(editor.Text().empty());
	CHECK_FALSE(workspace.PromptActive());
	const auto errors = workspace.TakeFileErrors();
	REQUIRE(errors.size() == 1);
	CHECK(errors[0].operation == DocumentFileOperation::Open);
	CHECK(errors[0].path == missing);
	CHECK(workspace.TakeRecentPaths().empty());
	CHECK_FALSE(HasRequest(workspace.TakeRequests(),
		DocumentShellRequest::RefreshTabs));
}

TEST_CASE("document workspace startup files missing sole path fails coherently") {
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	const DocumentId only = workspace.ActiveTab();
	const std::string missing = "/tmp/scalpel-startup-missing-XXXX-not-present";

	CHECK_FALSE(workspace.LoadStartupFiles({missing}));
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.ActiveTab() == only);
	CHECK(workspace.Path().empty());
	CHECK_FALSE(workspace.PromptActive());
	const auto errors = workspace.TakeFileErrors();
	REQUIRE(errors.size() == 1);
	CHECK(errors[0].operation == DocumentFileOperation::Open);
	CHECK(errors[0].path == missing);
	CHECK(workspace.TakeRecentPaths().empty());
}

TEST_CASE("document workspace startup files rejects empty input") {
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	CHECK_FALSE(workspace.LoadStartupFiles({}));
	CHECK_FALSE(workspace.LoadStartupFiles({""}));
	CHECK_FALSE(workspace.LoadStartupFiles({"ok", ""}));
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.Path().empty());
	CHECK(workspace.TakeFileErrors().empty());
	CHECK(workspace.TakeRecentPaths().empty());
}

TEST_CASE("document workspace startup files rejects after workspace activity") {
	TempFile file("after activity\n");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);

	workspace.NewTab();
	REQUIRE(workspace.TabCount() == 2);
	CHECK_FALSE(workspace.LoadStartupFiles({file.path}));
	CHECK(workspace.TabCount() == 2);
	CHECK(workspace.Path().empty());
	CHECK(workspace.TakeRecentPaths().empty());
	CHECK(workspace.TakeFileErrors().empty());
}

TEST_CASE("document workspace startup files does not overwrite adopted text") {
	TempFile file("startup file\n");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("existing text\n");
	DocumentWorkspace workspace(editor);

	CHECK_FALSE(workspace.LoadStartupFiles({file.path}));
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.Path().empty());
	CHECK(editor.Text() == "existing text\n");
	CHECK_FALSE(editor.Modified());
	CHECK(workspace.TakeRecentPaths().empty());
	CHECK(workspace.TakeFileErrors().empty());
}

TEST_CASE("document workspace startup files does not overwrite dirty text") {
	TempFile file("startup file\n");
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	DirtyBuffer(editor);
	const std::string existing = editor.Text();

	CHECK_FALSE(workspace.LoadStartupFiles({file.path}));
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.Path().empty());
	CHECK(editor.Text() == existing);
	CHECK(editor.Modified());
	CHECK(workspace.TakeRecentPaths().empty());
	CHECK(workspace.TakeFileErrors().empty());
}

TEST_CASE("document workspace file size startup rejects hard-limit files") {
	SparseTempFile oversized(
		static_cast<off_t>(Scalpel::DocumentFileHardLimitBytes + 1));
	ApplicationEditor editor(320, 180);
	DocumentWorkspace workspace(editor);
	const DocumentId only = workspace.ActiveTab();

	CHECK_FALSE(workspace.LoadStartupFiles({oversized.path}));
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.ActiveTab() == only);
	CHECK(workspace.Path().empty());
	CHECK(editor.Text().empty());
	const auto errors = workspace.TakeFileErrors();
	REQUIRE(errors.size() == 1);
	CHECK(errors[0].operation == DocumentFileOperation::Open);
	CHECK(errors[0].path == oversized.path);
	CHECK(errors[0].reason == DocumentFileErrorReason::TooLarge);
	CHECK(workspace.TakeRecentPaths().empty());
	CHECK_FALSE(HasRequest(workspace.TakeRequests(),
		DocumentShellRequest::RefreshTabs));
}

TEST_CASE("document workspace large file open queues confirmation") {
	SparseTempFile large(
		static_cast<off_t>(Scalpel::DocumentFileWarningThresholdBytes + 1));
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);
	const DocumentId startup = workspace.ActiveTab();
	const std::string startupText = editor.Text();

	CHECK_FALSE(workspace.OpenPath(large.path));
	CHECK(workspace.LargeFilePromptActive());
	CHECK(workspace.LargeFilePromptPath() == large.path);
	CHECK_FALSE(workspace.PromptActive());
	CHECK(workspace.ActiveTab() == startup);
	CHECK(editor.Text() == startupText);
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.TakeFileErrors().empty());
	CHECK(workspace.TakeRecentPaths().empty());
	CHECK(HasRequest(workspace.TakeRequests(),
		DocumentShellRequest::PromptBegan));
}

TEST_CASE("document workspace large file cancel drops path") {
	SparseTempFile large(
		static_cast<off_t>(Scalpel::DocumentFileWarningThresholdBytes + 1));
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);
	const DocumentId startup = workspace.ActiveTab();

	CHECK_FALSE(workspace.OpenPath(large.path));
	REQUIRE(workspace.LargeFilePromptActive());
	(void)workspace.TakeRequests();

	workspace.ChooseLargeFile(LargeFileChoice::Cancel);
	CHECK_FALSE(workspace.LargeFilePromptActive());
	CHECK(workspace.LargeFilePromptPath().empty());
	CHECK(workspace.ActiveTab() == startup);
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.TakeFileErrors().empty());
	CHECK(workspace.TakeRecentPaths().empty());
}

TEST_CASE("document workspace large file accept loads under hard limit") {
	// Sparse file above the warning threshold but within the hard cap. Reading
	// materializes zeros up to warning+1 bytes (~64 MiB).
	SparseTempFile large(
		static_cast<off_t>(Scalpel::DocumentFileWarningThresholdBytes + 1));
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);

	CHECK_FALSE(workspace.OpenPath(large.path));
	REQUIRE(workspace.LargeFilePromptActive());
	(void)workspace.TakeRequests();
	(void)workspace.TakeRecentPaths();

	workspace.ChooseLargeFile(LargeFileChoice::Open);
	CHECK_FALSE(workspace.LargeFilePromptActive());
	CHECK(workspace.TabCount() == 2);
	CHECK(workspace.Path() == large.path);
	CHECK(editor.Text().size() ==
		Scalpel::DocumentFileWarningThresholdBytes + 1);
	CHECK_FALSE(editor.Modified());
	CHECK(workspace.TakeFileErrors().empty());
	const auto recent = workspace.TakeRecentPaths();
	REQUIRE(recent.size() == 1);
	CHECK(recent.front() == large.path);
	CHECK(HasRequest(workspace.TakeRequests(),
		DocumentShellRequest::RefreshTabs));
}

TEST_CASE("document workspace large file accept hard-limit failure") {
	// Above the hard cap: confirmation still appears from the warning read,
	// then Open fails without creating a tab.
	SparseTempFile oversized(
		static_cast<off_t>(Scalpel::DocumentFileHardLimitBytes + 1));
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);
	const DocumentId startup = workspace.ActiveTab();

	CHECK_FALSE(workspace.OpenPath(oversized.path));
	REQUIRE(workspace.LargeFilePromptActive());
	CHECK(workspace.LargeFilePromptPath() == oversized.path);
	(void)workspace.TakeRequests();

	workspace.ChooseLargeFile(LargeFileChoice::Open);
	CHECK_FALSE(workspace.LargeFilePromptActive());
	CHECK(workspace.ActiveTab() == startup);
	CHECK(workspace.TabCount() == 1);
	CHECK(workspace.TakeRecentPaths().empty());
	const auto errors = workspace.TakeFileErrors();
	REQUIRE(errors.size() == 1);
	CHECK(errors[0].path == oversized.path);
	CHECK(errors[0].reason == DocumentFileErrorReason::TooLarge);
}

TEST_CASE("document workspace large file multi-path resumes after cancel") {
	TempFile first("first\n");
	SparseTempFile large(
		static_cast<off_t>(Scalpel::DocumentFileWarningThresholdBytes + 1));
	TempFile second("second\n");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);

	workspace.HandleOpenResult(true, {first.path, large.path, second.path});
	REQUIRE(workspace.LargeFilePromptActive());
	CHECK(workspace.LargeFilePromptPath() == large.path);
	// First path opened before the pause; second waits on the remaining list.
	CHECK(workspace.TabCount() == 2);
	CHECK(workspace.Path() == first.path);
	const auto recentBefore = workspace.TakeRecentPaths();
	REQUIRE(recentBefore.size() == 1);
	CHECK(recentBefore.front() == first.path);
	(void)workspace.TakeRequests();

	workspace.ChooseLargeFile(LargeFileChoice::Cancel);
	CHECK_FALSE(workspace.LargeFilePromptActive());
	CHECK(workspace.TabCount() == 3);
	CHECK(workspace.Path() == second.path);
	CHECK(editor.Text() == "second\n");
	CHECK(workspace.TakeFileErrors().empty());
	const auto recentAfter = workspace.TakeRecentPaths();
	REQUIRE(recentAfter.size() == 1);
	CHECK(recentAfter.front() == second.path);
}

TEST_CASE("document workspace large file multi-path resumes after hard-limit open") {
	TempFile first("first\n");
	SparseTempFile oversized(
		static_cast<off_t>(Scalpel::DocumentFileHardLimitBytes + 1));
	TempFile second("second\n");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);

	workspace.HandleOpenResult(true,
		{first.path, oversized.path, second.path});
	REQUIRE(workspace.LargeFilePromptActive());
	(void)workspace.TakeRecentPaths();
	(void)workspace.TakeRequests();

	workspace.ChooseLargeFile(LargeFileChoice::Open);
	CHECK_FALSE(workspace.LargeFilePromptActive());
	CHECK(workspace.TabCount() == 3);
	CHECK(workspace.Path() == second.path);
	CHECK(editor.Text() == "second\n");
	const auto errors = workspace.TakeFileErrors();
	REQUIRE(errors.size() == 1);
	CHECK(errors[0].path == oversized.path);
	CHECK(errors[0].reason == DocumentFileErrorReason::TooLarge);
	const auto recent = workspace.TakeRecentPaths();
	REQUIRE(recent.size() == 1);
	CHECK(recent.front() == second.path);
}

TEST_CASE("document workspace large file blocks dirty-close and tab actions") {
	SparseTempFile large(
		static_cast<off_t>(Scalpel::DocumentFileWarningThresholdBytes + 1));
	TempFile other("other\n");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);
	const DocumentId startup = workspace.ActiveTab();
	REQUIRE(workspace.OpenPath(other.path));
	const DocumentId otherId = workspace.ActiveTab();
	(void)workspace.TakeRecentPaths();
	(void)workspace.TakeRequests();

	CHECK_FALSE(workspace.OpenPath(large.path));
	REQUIRE(workspace.LargeFilePromptActive());
	(void)workspace.TakeRequests();

	workspace.NewTab();
	workspace.ActivateTab(startup);
	workspace.CloseTab(otherId);
	workspace.RequestOpen();
	workspace.RequestSave();
	workspace.RequestClose();
	CHECK(workspace.LargeFilePromptActive());
	CHECK(workspace.TabCount() == 2);
	CHECK(workspace.ActiveTab() == otherId);
	CHECK_FALSE(workspace.PromptActive());
	CHECK(workspace.TakeRequests().empty());

	workspace.ChooseLargeFile(LargeFileChoice::Cancel);
	CHECK_FALSE(workspace.LargeFilePromptActive());
}

TEST_CASE("document workspace large file open is no-op while dirty prompt active") {
	SparseTempFile large(
		static_cast<off_t>(Scalpel::DocumentFileWarningThresholdBytes + 1));
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("dirty\n");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);

	workspace.RequestClose();
	REQUIRE(workspace.PromptActive());
	(void)workspace.TakeRequests();

	CHECK_FALSE(workspace.OpenPath(large.path));
	CHECK_FALSE(workspace.LargeFilePromptActive());
	CHECK(workspace.PromptActive());
	CHECK(workspace.TakeFileErrors().empty());
	CHECK(workspace.TakeRecentPaths().empty());
}
