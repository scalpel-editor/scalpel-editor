#include "DocumentWorkspace.h"

#include <iostream>
#include <optional>

#include "ApplicationEditor.h"
#include "DocumentFile.h"

namespace Scalpel {

DocumentWorkspace::DocumentWorkspace(ApplicationEditor &editorHost) :
	editor(editorHost) {
}

bool DocumentWorkspace::BufferModified() const noexcept {
	return editor.Modified();
}

void DocumentWorkspace::Queue(DocumentShellRequest request) {
	requests.push_back(request);
}

std::vector<DocumentShellRequest> DocumentWorkspace::TakeRequests() {
	std::vector<DocumentShellRequest> out;
	out.swap(requests);
	return out;
}

bool DocumentWorkspace::SaveToPath(const std::string &destination) {
	if (!WriteDocumentFile(destination, editor.Text())) {
		std::cerr << "scalpel-editor: failed to write " << destination << '\n';
		return false;
	}
	editor.MarkSaved();
	return true;
}

void DocumentWorkspace::BeginPrompt(UnsavedPending pending) {
	if (!prompt.TryBegin(pending)) {
		return;
	}
	editor.CancelActiveTextInput();
	editor.InvalidateClient();
	Queue(DocumentShellRequest::PromptBegan);
}

void DocumentWorkspace::ApplyOutcome(UnsavedOutcome outcome) {
	switch (outcome) {
	case UnsavedOutcome::None:
	case UnsavedOutcome::SaveFailed:
		break;
	case UnsavedOutcome::Dismissed:
		editor.InvalidateClient();
		break;
	case UnsavedOutcome::PerformClose:
		Queue(DocumentShellRequest::AcceptClose);
		editor.InvalidateClient();
		break;
	case UnsavedOutcome::PerformOpen:
		// Discard leaves the buffer dirty; clear so HandleOpenResult accepts
		// the chosen path. Save already cleared via MarkSaved.
		editor.MarkSaved();
		editor.InvalidateClient();
		Queue(DocumentShellRequest::ShowOpen);
		break;
	case UnsavedOutcome::NeedSaveAs:
		Queue(DocumentShellRequest::ShowSaveAs);
		// Keep the card visible while the portal runs.
		editor.InvalidateClient();
		break;
	}
}

void DocumentWorkspace::RequestOpen() {
	if (editor.Modified()) {
		BeginPrompt(UnsavedPending::Open);
	} else {
		Queue(DocumentShellRequest::ShowOpen);
	}
}

void DocumentWorkspace::RequestSave() {
	if (path.empty()) {
		Queue(DocumentShellRequest::ShowSaveAs);
	} else {
		(void)SaveToPath(path);
	}
}

void DocumentWorkspace::RequestSaveAs() {
	Queue(DocumentShellRequest::ShowSaveAs);
}

void DocumentWorkspace::RequestClose() {
	if (prompt.Active()) {
		// Pending action already owns the close/open decision.
		return;
	}
	if (editor.Modified()) {
		BeginPrompt(UnsavedPending::Close);
		return;
	}
	Queue(DocumentShellRequest::AcceptClose);
}

void DocumentWorkspace::Choose(UnsavedChoice choice) {
	if (!prompt.Active()) {
		return;
	}
	if (choice == UnsavedChoice::Save && !path.empty()) {
		// Write before clearing the prompt so a failed write keeps the card.
		if (!SaveToPath(path)) {
			return;
		}
		ApplyOutcome(prompt.Choose(UnsavedChoice::Save, true));
		return;
	}
	ApplyOutcome(prompt.Choose(choice, !path.empty()));
}

void DocumentWorkspace::HandleOpenResult(bool accepted,
	std::string_view openedPath) {
	if (!accepted || openedPath.empty()) {
		return;
	}
	if (editor.Modified()) {
		std::cerr << "scalpel-editor: save or discard changes before "
			"opening another file\n";
		return;
	}
	const std::string pathString(openedPath);
	const std::optional<std::string> text = ReadDocumentFile(pathString);
	if (!text) {
		std::cerr << "scalpel-editor: failed to read " << pathString << '\n';
		return;
	}
	editor.LoadInitialBuffer(*text);
	path = pathString;
}

void DocumentWorkspace::HandleSaveResult(bool accepted,
	std::string_view savedPath) {
	const bool awaiting = prompt.AwaitingSaveAs();
	if (!accepted || savedPath.empty()) {
		if (awaiting) {
			prompt.NotifySaveIncomplete();
			editor.InvalidateClient();
		}
		return;
	}
	const std::string pathString(savedPath);
	if (SaveToPath(pathString)) {
		path = pathString;
		if (awaiting) {
			ApplyOutcome(prompt.NotifySaved());
		}
	} else if (awaiting) {
		prompt.NotifySaveIncomplete();
		editor.InvalidateClient();
	}
}

}
