#include "catch.hpp"

#include <cstdio>
#include <string>
#include <unistd.h>

#include "ApplicationEditor.h"
#include "DocumentFile.h"
#include "DocumentWorkspace.h"

using Scalpel::ApplicationEditor;
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

	workspace.RequestClose();
	const auto requests = workspace.TakeRequests();
	CHECK(HasRequest(requests, DocumentShellRequest::PromptBegan));
	CHECK(workspace.PromptActive());
	CHECK(workspace.Pending() == UnsavedPending::Close);
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
	CHECK(workspace.Pending() == UnsavedPending::Close);
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

TEST_CASE("document workspace single-document open while dirty prompts") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("dirty\n");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);

	workspace.RequestOpen();
	const auto requests = workspace.TakeRequests();
	CHECK(HasRequest(requests, DocumentShellRequest::PromptBegan));
	CHECK(workspace.Pending() == UnsavedPending::Open);
}

TEST_CASE("document workspace single-document open discard then dialog") {
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("dirty\n");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);
	workspace.RequestOpen();
	(void)workspace.TakeRequests();

	workspace.Choose(UnsavedChoice::Discard);
	const auto requests = workspace.TakeRequests();
	CHECK(HasRequest(requests, DocumentShellRequest::ShowOpen));
	CHECK_FALSE(workspace.PromptActive());
	// Discard must clear dirty so a later Open result is accepted.
	CHECK_FALSE(editor.Modified());
}

TEST_CASE("document workspace single-document open result loads path") {
	TempFile file("file contents\n");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DocumentWorkspace workspace(editor);

	workspace.HandleOpenResult(true, file.path);
	CHECK(workspace.Path() == file.path);
	CHECK(editor.Text() == "file contents\n");
	CHECK_FALSE(editor.Modified());
}

TEST_CASE("document workspace single-document open result rejects dirty") {
	TempFile file("other\n");
	ApplicationEditor editor(320, 180);
	editor.LoadInitialBuffer("startup\n");
	DirtyBuffer(editor);
	DocumentWorkspace workspace(editor);

	workspace.HandleOpenResult(true, file.path);
	CHECK(workspace.Path().empty());
	CHECK(editor.Text() != "other\n");
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
		CHECK(workspace.TakeRequests().empty());
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
