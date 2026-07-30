#include "DocumentWorkspace.h"

#include <filesystem>
#include <iostream>
#include <utility>

#include "ApplicationEditor.h"
#include "DocumentFile.h"

namespace Scalpel {
namespace {

[[nodiscard]] std::string NormalizePath(std::string_view path) {
	return std::filesystem::path(std::string(path)).lexically_normal().string();
}

}

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

void DocumentWorkspace::QueueShowSaveAs() {
	pendingSaveAsTab = prompt.Active() ? dirtyCloseTabId : activeId;
	pendingSaveAsPromptGeneration =
		prompt.AwaitingSaveAs() ? activePromptGeneration : 0;
	Queue(DocumentShellRequest::ShowSaveAs);
}

std::vector<DocumentShellRequest> DocumentWorkspace::TakeRequests() {
	std::vector<DocumentShellRequest> out;
	out.swap(requests);
	return out;
}

bool DocumentWorkspace::SaveToPath(DocumentId tabId,
	const std::string &destination) {
	const std::optional<std::size_t> index = FindIndex(tabId);
	if (!index) {
		return false;
	}
	if (!WriteDocumentFile(destination, editor.Text(tabId))) {
		std::cerr << "scalpel-editor: failed to write " << destination << '\n';
		fileErrors.push_back({DocumentFileOperation::Save, destination});
		return false;
	}
	editor.MarkSaved(tabId);
	tabs[*index].path = destination;
	tabs[*index].untitledNumber = 0;
	Queue(DocumentShellRequest::RefreshTabs);
	return true;
}

void DocumentWorkspace::ClearDirtyCloseState() noexcept {
	dirtyCloseTabId = 0;
	windowCloseResolved.clear();
	activePromptGeneration = 0;
}

void DocumentWorkspace::BeginPrompt(UnsavedPending pending, DocumentId tabId) {
	if (!prompt.TryBegin(pending, tabId)) {
		return;
	}
	dirtyCloseTabId = tabId;
	activePromptGeneration = ++lastPromptGeneration;
	if (activePromptGeneration == 0) {
		activePromptGeneration = ++lastPromptGeneration;
	}
	// The card names this document; keep it active for path, save, and subtitle.
	if (activeId != tabId && FindIndex(tabId)) {
		editor.ActivateDocument(tabId);
		activeId = tabId;
		Queue(DocumentShellRequest::RefreshTabs);
	}
	editor.CancelActiveTextInput();
	editor.InvalidateClient();
	Queue(DocumentShellRequest::PromptBegan);
}

std::optional<DocumentId> DocumentWorkspace::NextWindowCloseDirty() const {
	for (const Tab &tab : tabs) {
		if (windowCloseResolved.count(tab.id) != 0) {
			continue;
		}
		if (editor.Modified(tab.id)) {
			return tab.id;
		}
	}
	return std::nullopt;
}

void DocumentWorkspace::AdvanceOrAcceptWindowClose() {
	if (const std::optional<DocumentId> next = NextWindowCloseDirty()) {
		// Keep windowCloseResolved; start the next card on the next dirty tab.
		dirtyCloseTabId = 0;
		activePromptGeneration = 0;
		BeginPrompt(UnsavedPending::CloseWindow, *next);
		return;
	}
	ClearDirtyCloseState();
	Queue(DocumentShellRequest::AcceptClose);
}

void DocumentWorkspace::ApplyOutcome(UnsavedOutcome outcome,
	UnsavedPending completedKind, DocumentId completedTabId) {
	switch (outcome) {
	case UnsavedOutcome::None:
	case UnsavedOutcome::SaveFailed:
		break;
	case UnsavedOutcome::Dismissed:
		// Cancel aborts tab close and the whole window-close walk.
		ClearDirtyCloseState();
		editor.InvalidateClient();
		break;
	case UnsavedOutcome::PerformClose:
		if (completedKind == UnsavedPending::CloseTab) {
			ClearDirtyCloseState();
			if (const std::optional<std::size_t> index =
					FindIndex(completedTabId)) {
				RemoveTabAt(*index);
			}
		} else if (completedKind == UnsavedPending::CloseWindow) {
			// Discard leaves the buffer dirty; remember so it is not re-prompted.
			if (editor.HasDocument(completedTabId) &&
				editor.Modified(completedTabId)) {
				windowCloseResolved.insert(completedTabId);
			}
			AdvanceOrAcceptWindowClose();
		} else {
			ClearDirtyCloseState();
		}
		editor.InvalidateClient();
		break;
	case UnsavedOutcome::NeedSaveAs:
		QueueShowSaveAs();
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
		BeginPrompt(UnsavedPending::CloseTab, id);
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
		QueueShowSaveAs();
	} else {
		(void)SaveToPath(activeId, Path());
	}
}

void DocumentWorkspace::RequestSaveAs() {
	if (prompt.Active()) {
		return;
	}
	QueueShowSaveAs();
}

void DocumentWorkspace::RequestClose() {
	if (prompt.Active()) {
		// Pending action already owns the close decision.
		return;
	}
	windowCloseResolved.clear();
	if (const std::optional<DocumentId> firstDirty = NextWindowCloseDirty()) {
		BeginPrompt(UnsavedPending::CloseWindow, *firstDirty);
		return;
	}
	Queue(DocumentShellRequest::AcceptClose);
}

void DocumentWorkspace::Choose(UnsavedChoice choice) {
	if (!prompt.Active()) {
		return;
	}
	const UnsavedPending kind = prompt.Pending();
	const DocumentId tabId = prompt.TabId();
	const std::string &path = PathOf(tabId);
	if (choice == UnsavedChoice::Save && !path.empty()) {
		// Write before clearing the prompt so a failed write keeps the card.
		if (!SaveToPath(tabId, path)) {
			return;
		}
		ApplyOutcome(prompt.Choose(UnsavedChoice::Save, true), kind, tabId);
		return;
	}
	ApplyOutcome(prompt.Choose(choice, !path.empty()), kind, tabId);
}

void DocumentWorkspace::RegisterOpenRequest(uint64_t requestId) {
	if (requestId == 0) {
		return;
	}
	PortalIntent intent;
	intent.kind = PortalIntentKind::Open;
	portalIntents[requestId] = intent;
}

void DocumentWorkspace::RegisterSaveAsRequest(uint64_t requestId) {
	if (requestId == 0) {
		return;
	}
	PortalIntent intent;
	intent.kind = PortalIntentKind::SaveAs;
	intent.tabId = pendingSaveAsTab.value_or(activeId);
	intent.promptGeneration = pendingSaveAsPromptGeneration;
	pendingSaveAsTab.reset();
	pendingSaveAsPromptGeneration = 0;
	portalIntents[requestId] = intent;
}

void DocumentWorkspace::NoteSaveAsDialogFailed() {
	const bool continuePrompt =
		pendingSaveAsPromptGeneration != 0 &&
		pendingSaveAsPromptGeneration == activePromptGeneration &&
		prompt.AwaitingSaveAs();
	pendingSaveAsTab.reset();
	pendingSaveAsPromptGeneration = 0;
	if (continuePrompt) {
		prompt.NotifySaveIncomplete();
		editor.InvalidateClient();
	}
}

void DocumentWorkspace::HandlePortalResult(uint64_t requestId, bool accepted,
	const std::vector<std::string> &paths) {
	const auto it = portalIntents.find(requestId);
	if (it == portalIntents.end()) {
		return;
	}
	const PortalIntent intent = it->second;
	portalIntents.erase(it);

	const bool usable = accepted && !paths.empty();

	if (intent.kind == PortalIntentKind::Open) {
		if (usable) {
			(void)ApplyOpenPaths(paths);
		}
		return;
	}

	// Save As: ignore when the initiating tab no longer exists.
	if (!FindIndex(intent.tabId)) {
		return;
	}
	const std::string_view path = usable ?
		std::string_view(paths.front()) :
		std::string_view{};
	ApplySaveResult(intent.tabId, usable, path, intent.promptGeneration);
}

void DocumentWorkspace::HandleOpenResult(bool accepted,
	std::string_view openedPath) {
	if (!accepted || openedPath.empty()) {
		return;
	}
	(void)ApplyOpenPaths({std::string(openedPath)});
}

void DocumentWorkspace::HandleOpenResult(bool accepted,
	const std::vector<std::string> &paths) {
	if (!accepted || paths.empty()) {
		return;
	}
	(void)ApplyOpenPaths(paths);
}

bool DocumentWorkspace::OpenPath(std::string_view path) {
	if (path.empty() || prompt.Active()) {
		return false;
	}
	return ApplyOpenPaths({std::string(path)});
}

void DocumentWorkspace::HandleSaveResult(bool accepted,
	std::string_view savedPath) {
	const uint64_t promptGeneration =
		prompt.AwaitingSaveAs() ? activePromptGeneration : 0;
	ApplySaveResult(activeId, accepted, savedPath, promptGeneration);
}

void DocumentWorkspace::HandleSaveResult(DocumentId tabId, bool accepted,
	std::string_view savedPath) {
	const bool continuePrompt = prompt.AwaitingSaveAs() &&
		prompt.TabId() == tabId;
	ApplySaveResult(tabId, accepted, savedPath,
		continuePrompt ? activePromptGeneration : 0);
}

bool DocumentWorkspace::ApplyOpenPaths(const std::vector<std::string> &paths) {
	// Tab open must not move activeId away from a dirty-close prompt.
	if (prompt.Active()) {
		return false;
	}
	std::optional<DocumentId> lastActivated;
	for (const std::string &path : paths) {
		if (path.empty()) {
			continue;
		}
		const std::string pathString = NormalizePath(path);
		if (const std::optional<std::size_t> existing =
				FindIndexByPath(pathString)) {
			lastActivated = tabs[*existing].id;
			recentPaths.push_back(pathString);
			continue;
		}
		const std::optional<std::string> text = ReadDocumentFile(pathString);
		if (!text) {
			std::cerr << "scalpel-editor: failed to read " << pathString << '\n';
			fileErrors.push_back({DocumentFileOperation::Open, pathString});
			continue;
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
		lastActivated = id;
		recentPaths.push_back(pathString);
	}
	if (!lastActivated) {
		return false;
	}
	if (*lastActivated != activeId) {
		editor.ActivateDocument(*lastActivated);
		activeId = *lastActivated;
	}
	Queue(DocumentShellRequest::RefreshTabs);
	return true;
}

void DocumentWorkspace::ApplySaveResult(DocumentId tabId, bool accepted,
	std::string_view savedPath, uint64_t promptGeneration) {
	const bool continuePrompt =
		promptGeneration != 0 &&
		promptGeneration == activePromptGeneration &&
		prompt.AwaitingSaveAs() &&
		prompt.TabId() == tabId;
	if (!accepted || savedPath.empty()) {
		if (continuePrompt) {
			prompt.NotifySaveIncomplete();
			editor.InvalidateClient();
		}
		return;
	}
	if (!FindIndex(tabId)) {
		return;
	}
	const std::string pathString = NormalizePath(savedPath);
	if (SaveToPath(tabId, pathString)) {
		recentPaths.push_back(pathString);
		if (continuePrompt) {
			const UnsavedPending kind = prompt.Pending();
			const DocumentId completedTabId = prompt.TabId();
			ApplyOutcome(prompt.NotifySaved(), kind, completedTabId);
		}
	} else if (continuePrompt) {
		prompt.NotifySaveIncomplete();
		editor.InvalidateClient();
	}
}

std::vector<std::string> DocumentWorkspace::TakeRecentPaths() {
	std::vector<std::string> result = std::move(recentPaths);
	recentPaths.clear();
	return result;
}

std::vector<DocumentFileError> DocumentWorkspace::TakeFileErrors() {
	std::vector<DocumentFileError> result = std::move(fileErrors);
	fileErrors.clear();
	return result;
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

const std::string &DocumentWorkspace::PathOf(DocumentId tabId) const noexcept {
	if (const std::optional<std::size_t> index = FindIndex(tabId)) {
		return tabs[*index].path;
	}
	return ActiveTabRecord().path;
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
