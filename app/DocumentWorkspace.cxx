#include "DocumentWorkspace.h"

#include <iostream>
#include <utility>

#include "DocumentFile.h"

namespace Scalpel {

DocumentWorkspace::DocumentWorkspace(ApplicationEditor &editorHost) :
	editor(editorHost) {
	// The host already owns one document; adopt it as the first untitled tab.
	const DocumentId id = editor.ActiveDocument();
	activeId = id;
	AppendUntitled(id);
}

const std::string &DocumentWorkspace::Path() const noexcept {
	return ActiveTabRecord().path;
}

std::vector<DocumentTabInfo> DocumentWorkspace::Tabs() const {
	std::vector<DocumentTabInfo> out;
	out.reserve(tabs.size());
	for (const Tab &tab : tabs) {
		DocumentTabInfo info;
		info.id = tab.id;
		info.path = tab.path;
		info.untitledNumber = tab.untitledNumber;
		info.label = LabelFor(tab);
		info.dirty = editor.Modified(tab.id);
		info.active = tab.id == activeId;
		out.push_back(std::move(info));
	}
	return out;
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
	ActiveTabRecord().path = destination;
	ActiveTabRecord().untitledNumber = 0;
	Queue(DocumentShellRequest::RefreshTabs);
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
		closingTabId.reset();
		editor.InvalidateClient();
		break;
	case UnsavedOutcome::PerformClose:
		if (closingTabId) {
			const DocumentId id = *closingTabId;
			closingTabId.reset();
			if (const std::optional<std::size_t> index = FindIndex(id)) {
				RemoveTabAt(*index);
			}
		} else {
			Queue(DocumentShellRequest::AcceptClose);
		}
		editor.InvalidateClient();
		break;
	case UnsavedOutcome::NeedSaveAs:
		Queue(DocumentShellRequest::ShowSaveAs);
		// Keep the card visible while the portal runs.
		editor.InvalidateClient();
		break;
	}
}

void DocumentWorkspace::NewTab() {
	if (prompt.Active()) {
		return;
	}
	const DocumentId id = editor.CreateDocument();
	AppendUntitled(id);
	editor.ActivateDocument(id);
	activeId = id;
	Queue(DocumentShellRequest::RefreshTabs);
}

void DocumentWorkspace::ActivateTab(DocumentId id) {
	if (prompt.Active()) {
		return;
	}
	if (id == activeId) {
		return;
	}
	if (!FindIndex(id)) {
		return;
	}
	editor.ActivateDocument(id);
	activeId = id;
	Queue(DocumentShellRequest::RefreshTabs);
}

void DocumentWorkspace::CycleTab(int delta) {
	if (prompt.Active() || tabs.empty() || delta == 0) {
		return;
	}
	const std::size_t count = tabs.size();
	const std::size_t current = IndexOf(activeId);
	// Positive delta moves right; negative moves left. Wrap in both directions.
	const int countInt = static_cast<int>(count);
	int next = static_cast<int>(current) + (delta % countInt);
	next %= countInt;
	if (next < 0) {
		next += countInt;
	}
	ActivateTab(tabs[static_cast<std::size_t>(next)].id);
}

void DocumentWorkspace::CloseTab(DocumentId id) {
	if (prompt.Active()) {
		return;
	}
	const std::optional<std::size_t> index = FindIndex(id);
	if (!index) {
		return;
	}
	if (editor.Modified(id)) {
		ActivateTab(id);
		closingTabId = id;
		BeginPrompt(UnsavedPending::Close);
		return;
	}
	RemoveTabAt(*index);
}

void DocumentWorkspace::RequestOpen() {
	if (prompt.Active()) {
		return;
	}
	Queue(DocumentShellRequest::ShowOpen);
}

void DocumentWorkspace::RequestSave() {
	if (prompt.Active()) {
		return;
	}
	if (Path().empty()) {
		Queue(DocumentShellRequest::ShowSaveAs);
	} else {
		(void)SaveToPath(Path());
	}
}

void DocumentWorkspace::RequestSaveAs() {
	if (prompt.Active()) {
		return;
	}
	Queue(DocumentShellRequest::ShowSaveAs);
}

void DocumentWorkspace::RequestClose() {
	if (prompt.Active()) {
		// Pending action already owns the close decision.
		return;
	}
	if (editor.Modified()) {
		closingTabId.reset();
		BeginPrompt(UnsavedPending::Close);
		return;
	}
	Queue(DocumentShellRequest::AcceptClose);
}

void DocumentWorkspace::Choose(UnsavedChoice choice) {
	if (!prompt.Active()) {
		return;
	}
	if (choice == UnsavedChoice::Save && !Path().empty()) {
		// Write before clearing the prompt so a failed write keeps the card.
		if (!SaveToPath(Path())) {
			return;
		}
		ApplyOutcome(prompt.Choose(UnsavedChoice::Save, true));
		return;
	}
	ApplyOutcome(prompt.Choose(choice, !Path().empty()));
}

void DocumentWorkspace::HandleOpenResult(bool accepted,
	std::string_view openedPath) {
	if (!accepted || openedPath.empty()) {
		return;
	}
	if (const std::optional<std::size_t> existing = FindIndexByPath(openedPath)) {
		ActivateTab(tabs[*existing].id);
		return;
	}
	const std::string pathString(openedPath);
	const std::optional<std::string> text = ReadDocumentFile(pathString);
	if (!text) {
		std::cerr << "scalpel-editor: failed to read " << pathString << '\n';
		return;
	}
	const DocumentId id = editor.CreateDocument();
	Tab tab;
	tab.id = id;
	tab.path = pathString;
	tab.untitledNumber = 0;
	tabs.push_back(std::move(tab));
	editor.ActivateDocument(id);
	activeId = id;
	editor.LoadInitialBuffer(*text);
	Queue(DocumentShellRequest::RefreshTabs);
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
		if (awaiting) {
			ApplyOutcome(prompt.NotifySaved());
		}
	} else if (awaiting) {
		prompt.NotifySaveIncomplete();
		editor.InvalidateClient();
	}
}

std::size_t DocumentWorkspace::IndexOf(DocumentId id) const {
	if (const std::optional<std::size_t> index = FindIndex(id)) {
		return *index;
	}
	return 0;
}

std::optional<std::size_t> DocumentWorkspace::FindIndex(DocumentId id) const {
	for (std::size_t i = 0; i < tabs.size(); ++i) {
		if (tabs[i].id == id) {
			return i;
		}
	}
	return std::nullopt;
}

std::optional<std::size_t> DocumentWorkspace::FindIndexByPath(
	std::string_view path) const {
	for (std::size_t i = 0; i < tabs.size(); ++i) {
		if (tabs[i].path == path) {
			return i;
		}
	}
	return std::nullopt;
}

DocumentWorkspace::Tab &DocumentWorkspace::ActiveTabRecord() {
	return tabs[IndexOf(activeId)];
}

const DocumentWorkspace::Tab &DocumentWorkspace::ActiveTabRecord() const {
	return tabs[IndexOf(activeId)];
}

std::string DocumentWorkspace::LabelFor(const Tab &tab) const {
	std::string label;
	if (tab.path.empty()) {
		label = "Untitled " + std::to_string(tab.untitledNumber);
	} else {
		label = DocumentBaseName(tab.path);
	}
	if (editor.Modified(tab.id)) {
		label += " *";
	}
	return label;
}

DocumentWorkspace::Tab &DocumentWorkspace::AppendUntitled(DocumentId id) {
	Tab tab;
	tab.id = id;
	tab.untitledNumber = nextUntitledNumber++;
	tabs.push_back(std::move(tab));
	return tabs.back();
}

void DocumentWorkspace::RemoveTabAt(std::size_t index) {
	const DocumentId id = tabs[index].id;
	if (tabs.size() == 1) {
		// Closing the last tab yields a fresh untitled tab, not a quit.
		const DocumentId fresh = editor.CreateDocument();
		editor.ActivateDocument(fresh);
		editor.CloseDocument(id);
		tabs[0] = Tab{};
		tabs[0].id = fresh;
		tabs[0].untitledNumber = nextUntitledNumber++;
		activeId = fresh;
		Queue(DocumentShellRequest::RefreshTabs);
		return;
	}
	DocumentId nextActive = activeId;
	if (id == activeId) {
		nextActive = (index + 1 < tabs.size()) ?
			tabs[index + 1].id :
			tabs[index - 1].id;
		editor.ActivateDocument(nextActive);
		activeId = nextActive;
	}
	editor.CloseDocument(id);
	tabs.erase(tabs.begin() + static_cast<std::ptrdiff_t>(index));
	EnsureActiveMatchesEditor();
	Queue(DocumentShellRequest::RefreshTabs);
}

void DocumentWorkspace::EnsureActiveMatchesEditor() {
	if (editor.ActiveDocument() != activeId) {
		activeId = editor.ActiveDocument();
	}
}

}
