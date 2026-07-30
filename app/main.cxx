// Production Wayland editor process entry.
//
// Ownership: ApplicationEditor retains Scintilla documents and paints one
// surface (one renderer, one EGL context). DocumentWorkspace owns tab order,
// paths, untitled numbering, portal request intents, and dirty-close prompts;
// it returns shell requests and owns no Wayland or drawing. ApplicationUi owns
// menu, tab-strip, scrollbar interaction, modal-card, file-error queue, hover,
// press, and overlay-selection state. MenuBar, TabStrip, and ScrollBar are
// permanent chrome (menu and tabs on top; vertical and horizontal bars around
// the editor client). The open menu dropdown, UnsavedChangesCard, and
// FileErrorCard share the post-paint overlay slot. Input priority: file error,
// unsaved card, open menu, active scrollbar drag, top chrome, scrollbar hit,
// then editor. An active editor selection drag still owns motion and release
// before scrollbar hit testing. This file is the event and rendering adapter:
// it delivers input and portal results into the workspace, performs shell
// requests (dialogs, quit, strip refresh), dismisses menus and scrollbar drags
// around portals, prompts, tab activation, force close, and focus loss, and
// paints chrome. Hit testing and painting share one ApplicationLayout snapshot
// per event or paint pass. Open/save policy and prompt transitions live in
// DocumentWorkspace, not here. Required-global loss force-closes without
// prompting the workspace.

#include <cmath>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ApplicationAction.h"
#include "ApplicationEditor.h"
#include "ApplicationUi.h"
#include "DocumentFile.h"
#include "DocumentWorkspace.h"
#include "FileErrorCard.h"
#include "GlContext.h"
#include "MenuBar.h"
#include "RecentFiles.h"
#include "ScrollBar.h"
#include "TabStrip.h"
#include "UnsavedChangesCard.h"
#include "UnsavedChangesPrompt.h"
#include "WaylandWindow.h"

namespace {

Scalpel::ApplicationClipboardOperation ApplicationOperation(
	Scalpel::ClipboardOperation operation) {
	return operation == Scalpel::ClipboardOperation::Copy ?
		Scalpel::ApplicationClipboardOperation::Copy :
		Scalpel::ApplicationClipboardOperation::Paste;
}

Scalpel::ApplicationClipboardStatus ApplicationStatus(
	Scalpel::ClipboardResultStatus status) {
	switch (status) {
	case Scalpel::ClipboardResultStatus::Published:
		return Scalpel::ApplicationClipboardStatus::Published;
	case Scalpel::ClipboardResultStatus::Complete:
		return Scalpel::ApplicationClipboardStatus::Complete;
	case Scalpel::ClipboardResultStatus::Unavailable:
		return Scalpel::ApplicationClipboardStatus::Unavailable;
	case Scalpel::ClipboardResultStatus::NoText:
		return Scalpel::ApplicationClipboardStatus::NoText;
	case Scalpel::ClipboardResultStatus::InvalidUtf8:
		return Scalpel::ApplicationClipboardStatus::InvalidText;
	case Scalpel::ClipboardResultStatus::Cancelled:
		return Scalpel::ApplicationClipboardStatus::Cancelled;
	case Scalpel::ClipboardResultStatus::Failed:
		return Scalpel::ApplicationClipboardStatus::Failed;
	case Scalpel::ClipboardResultStatus::TooLarge:
		return Scalpel::ApplicationClipboardStatus::TooLarge;
	case Scalpel::ClipboardResultStatus::TimedOut:
		return Scalpel::ApplicationClipboardStatus::TimedOut;
	}
	return Scalpel::ApplicationClipboardStatus::Failed;
}

Scalpel::ApplicationPrimarySelectionOperation ApplicationOperation(
	Scalpel::PrimarySelectionOperation operation) {
	return operation == Scalpel::PrimarySelectionOperation::Publish ?
		Scalpel::ApplicationPrimarySelectionOperation::Publish :
		Scalpel::ApplicationPrimarySelectionOperation::Paste;
}

Scalpel::ApplicationPrimarySelectionStatus ApplicationStatus(
	Scalpel::PrimarySelectionResultStatus status) {
	switch (status) {
	case Scalpel::PrimarySelectionResultStatus::Published:
		return Scalpel::ApplicationPrimarySelectionStatus::Published;
	case Scalpel::PrimarySelectionResultStatus::Complete:
		return Scalpel::ApplicationPrimarySelectionStatus::Complete;
	case Scalpel::PrimarySelectionResultStatus::Unavailable:
		return Scalpel::ApplicationPrimarySelectionStatus::Unavailable;
	case Scalpel::PrimarySelectionResultStatus::NoText:
		return Scalpel::ApplicationPrimarySelectionStatus::NoText;
	case Scalpel::PrimarySelectionResultStatus::InvalidUtf8:
		return Scalpel::ApplicationPrimarySelectionStatus::InvalidText;
	case Scalpel::PrimarySelectionResultStatus::Cancelled:
		return Scalpel::ApplicationPrimarySelectionStatus::Cancelled;
	case Scalpel::PrimarySelectionResultStatus::Failed:
		return Scalpel::ApplicationPrimarySelectionStatus::Failed;
	case Scalpel::PrimarySelectionResultStatus::TooLarge:
		return Scalpel::ApplicationPrimarySelectionStatus::TooLarge;
	case Scalpel::PrimarySelectionResultStatus::TimedOut:
		return Scalpel::ApplicationPrimarySelectionStatus::TimedOut;
	}
	return Scalpel::ApplicationPrimarySelectionStatus::Failed;
}

void DeliverClipboardResults(Scalpel::WaylandWindow &window,
	Scalpel::ApplicationEditor &editor) {
	for (Scalpel::ClipboardResult &result : window.TakeClipboardResults()) {
		editor.HandleClipboardResult(result.request, ApplicationOperation(result.operation),
			ApplicationStatus(result.status), std::move(result.text));
	}
	(void)editor.TakeClipboardResults();
	editor.SetClipboardPasteAvailable(window.ClipboardPasteAvailable());
}

void DispatchClipboardRequests(Scalpel::ApplicationEditor &editor,
	Scalpel::WaylandWindow &window) {
	for (Scalpel::ApplicationClipboardRequest &request : editor.TakeClipboardRequests()) {
		if (request.operation == Scalpel::ApplicationClipboardOperation::Copy) {
			window.CopyToClipboard(request.id, std::move(request.text));
		} else {
			window.PasteFromClipboard(request.id);
		}
	}
	DeliverClipboardResults(window, editor);
}

void DeliverPrimarySelectionResults(Scalpel::WaylandWindow &window,
	Scalpel::ApplicationEditor &editor) {
	for (Scalpel::PrimarySelectionResult &result :
		window.TakePrimarySelectionResults()) {
		editor.HandlePrimarySelectionResult(
			result.request, ApplicationOperation(result.operation),
			ApplicationStatus(result.status), std::move(result.text));
	}
	(void)editor.TakePrimarySelectionResults();
}

void DispatchPrimarySelectionRequests(Scalpel::ApplicationEditor &editor,
	Scalpel::WaylandWindow &window) {
	for (Scalpel::ApplicationPrimarySelectionRequest &request :
		editor.TakePrimarySelectionRequests()) {
		if (request.operation ==
			Scalpel::ApplicationPrimarySelectionOperation::Publish) {
			window.PublishPrimarySelection(
				request.id, std::move(request.text));
		} else {
			window.PasteFromPrimarySelection(request.id);
		}
	}
	DeliverPrimarySelectionResults(window, editor);
}

Scalpel::ApplicationTextInputBatch ApplicationBatch(
	Scalpel::WaylandTextInputBatch batch) {
	Scalpel::ApplicationTextInputBatch application;
	if (batch.preedit) {
		application.preedit = Scalpel::ApplicationTextInputPreedit{
			std::move(batch.preedit->text),
			batch.preedit->cursorBegin,
			batch.preedit->cursorEnd,
		};
	}
	application.commit = std::move(batch.commit);
	if (batch.deletion) {
		application.deletion = Scalpel::ApplicationTextInputDelete{
			batch.deletion->beforeLength,
			batch.deletion->afterLength,
		};
	}
	application.refreshState = batch.refreshState;
	application.cancel = batch.cancel;
	return application;
}

Scalpel::WaylandTextInputClientState WaylandState(
	Scalpel::ApplicationTextInputState state) {
	return {
		std::move(state.surroundingText),
		state.cursor,
		state.anchor,
		{
			state.cursorRectangle.x,
			state.cursorRectangle.y,
			state.cursorRectangle.width,
			state.cursorRectangle.height,
		},
		state.changeCause == Scalpel::ApplicationTextChangeCause::InputMethod ?
			Scalpel::WaylandTextChangeCause::InputMethod :
			Scalpel::WaylandTextChangeCause::Other,
	};
}

void DeliverTextInputBatches(Scalpel::WaylandWindow &window,
	Scalpel::ApplicationEditor &editor, bool chromeOwnsInput) {
	for (Scalpel::WaylandTextInputBatch &batch : window.TakeTextInputBatches()) {
		if (chromeOwnsInput) {
			// Unsaved card or open menu owns input; drop IME until it closes.
			continue;
		}
		editor.HandleTextInputBatch(ApplicationBatch(std::move(batch)));
	}
}

void SynchronizeTextInput(Scalpel::ApplicationEditor &editor,
	Scalpel::WaylandWindow &window) {
	if (std::optional<Scalpel::ApplicationTextInputState> state =
		editor.TakeTextInputState()) {
		window.UpdateTextInputState(WaylandState(std::move(*state)));
	}
}

void QueueFrameDamage(Scalpel::ApplicationEditor &editor,
	Scalpel::WaylandWindow &window) {
	for (const Scintilla::Internal::PRectangle &rectangle :
		editor.TakeFrameDamage()) {
		window.InvalidateFrame({
			static_cast<int>(std::floor(rectangle.left)),
			static_cast<int>(std::floor(rectangle.top)),
			static_cast<int>(std::ceil(rectangle.right)),
			static_cast<int>(std::ceil(rectangle.bottom)),
		});
	}
}

std::vector<Scintilla::Internal::PRectangle> EditorDamage(
	const std::vector<Scalpel::FrameRectangle> &damage) {
	std::vector<Scintilla::Internal::PRectangle> rectangles;
	rectangles.reserve(damage.size());
	for (const Scalpel::FrameRectangle &rectangle : damage) {
		rectangles.push_back(Scintilla::Internal::PRectangle::FromInts(
			rectangle.left, rectangle.top, rectangle.right, rectangle.bottom));
	}
	return rectangles;
}

std::vector<int> EglDamage(
	const std::vector<Scalpel::DamageRectangle> &damage) {
	std::vector<int> rectangles;
	rectangles.reserve(damage.size() * 4);
	for (const Scalpel::DamageRectangle &rectangle : damage) {
		rectangles.insert(rectangles.end(), {
			rectangle.x, rectangle.y, rectangle.width, rectangle.height});
	}
	return rectangles;
}

std::vector<Scalpel::FileDialogFilter> TextDialogFilters() {
	return {
		{"Text files", {"*.txt", "*.md"}},
		{"All files", {"*"}},
	};
}

[[nodiscard]] std::optional<uint64_t> StartOpenDialog(
	Scalpel::WaylandWindow &window, const std::string &documentPath) {
	Scalpel::FileDialogRequest request;
	request.mode = Scalpel::FileDialogMode::Open;
	request.title = "Open File";
	request.currentFolder = Scalpel::DocumentDirectory(documentPath);
	request.multiple = true;
	request.filters = TextDialogFilters();
	const std::optional<uint64_t> requestId = window.ShowFileDialog(request);
	if (!requestId) {
		std::cerr << "scalpel-editor: file dialog unavailable\n";
	}
	return requestId;
}

[[nodiscard]] std::optional<uint64_t> RequestSaveAsDialog(
	Scalpel::WaylandWindow &window, const std::string &documentPath) {
	Scalpel::FileDialogRequest request;
	request.mode = Scalpel::FileDialogMode::Save;
	request.title = "Save File As";
	request.currentFolder = Scalpel::DocumentDirectory(documentPath);
	request.suggestedName = documentPath.empty() ?
		"untitled.txt" :
		Scalpel::DocumentBaseName(documentPath);
	request.filters = TextDialogFilters();
	const std::optional<uint64_t> requestId = window.ShowFileDialog(request);
	if (!requestId) {
		std::cerr << "scalpel-editor: file dialog unavailable\n";
	}
	return requestId;
}

/** Rebuild strip tabs from the workspace. Returns true when display state changed. */
bool SyncTabStripTabs(const Scalpel::DocumentWorkspace &workspace,
	Scalpel::TabStripModel &model, int stripWidth) {
	const std::vector<Scalpel::DocumentTabInfo> tabs = workspace.Tabs();
	bool changed = model.tabs.size() != tabs.size();
	std::vector<Scalpel::TabStripTab> next;
	next.reserve(tabs.size());
	for (const Scalpel::DocumentTabInfo &info : tabs) {
		Scalpel::TabStripTab tab;
		tab.id = info.id;
		tab.label = info.label;
		tab.dirty = info.dirty;
		tab.active = info.active;
		next.push_back(std::move(tab));
	}
	if (!changed) {
		for (std::size_t i = 0; i < next.size(); ++i) {
			if (next[i].id != model.tabs[i].id ||
				next[i].label != model.tabs[i].label ||
				next[i].dirty != model.tabs[i].dirty ||
				next[i].active != model.tabs[i].active) {
				changed = true;
				break;
			}
		}
	}
	model.tabs = std::move(next);

	bool hoverValid = model.hoveredId == 0;
	for (const Scalpel::TabStripTab &tab : model.tabs) {
		if (tab.id == model.hoveredId) {
			hoverValid = true;
			break;
		}
	}
	if (!hoverValid) {
		model.hoveredId = 0;
		model.closeHovered = false;
		changed = true;
	}

	const int clamped = Scalpel::ClampTabStripScroll(
		stripWidth, model.tabs.size(), model.scrollOffset);
	if (clamped != model.scrollOffset) {
		model.scrollOffset = clamped;
		changed = true;
	}
	return changed;
}

void RevealActiveTab(Scalpel::TabStripModel &model, int stripWidth) {
	std::size_t activeIndex = 0;
	bool found = false;
	for (std::size_t i = 0; i < model.tabs.size(); ++i) {
		if (model.tabs[i].active) {
			activeIndex = i;
			found = true;
			break;
		}
	}
	if (!found) {
		return;
	}
	model.scrollOffset = Scalpel::ScrollTabStripToIndex(
		stripWidth, model.tabs.size(), activeIndex, model.scrollOffset);
}

/** Card subtitle names the tab that owns the dirty-close prompt. */
[[nodiscard]] std::string UnsavedPromptSubtitle(
	const Scalpel::DocumentWorkspace &workspace) {
	const Scalpel::DocumentId promptId = workspace.PromptTab();
	for (const Scalpel::DocumentTabInfo &tab : workspace.Tabs()) {
		if (tab.id != promptId) {
			continue;
		}
		std::string label = tab.label;
		// Drop the dirty marker; the card already asks about unsaved changes.
		if (label.size() >= 2 &&
			label.compare(label.size() - 2, 2, " *") == 0) {
			label.resize(label.size() - 2);
		}
		return label;
	}
	if (workspace.Path().empty()) {
		return "Untitled";
	}
	return Scalpel::DocumentBaseName(workspace.Path());
}

/** Close an open menu and force a full-frame repaint so the dropdown cannot linger. */
void DismissOpenMenu(Scalpel::MenuBarModel &menuModel,
	Scalpel::ApplicationEditor &editor) {
	if (!menuModel.openMenu.has_value()) {
		return;
	}
	Scalpel::CloseMenuBar(menuModel);
	editor.InvalidateFrame();
}

void PersistRecentFiles(const std::string &statePath,
	const Scalpel::RecentFiles &recent) {
	if (statePath.empty()) {
		return;
	}
	if (!Scalpel::SaveRecentFiles(statePath, recent)) {
		std::cerr << "scalpel-editor: failed to write recent-file state\n";
	}
}

void SyncRecentMenu(const Scalpel::RecentFiles &recent,
	Scalpel::MenuBarModel &menuModel,
	Scalpel::ApplicationEditor &editor) {
	if (menuModel.recentFiles == recent.Paths()) {
		return;
	}
	menuModel.recentFiles = recent.Paths();
	editor.InvalidateTopChrome();
	// Recent rows are identified by index. If the MRU list rewrites while
	// Recent is open (for example a late portal Open/Save As), focus, hover,
	// and press would still point at the old index and could activate the
	// wrong path. Dismiss so the next open rebuilds from the new list.
	if (menuModel.openMenu == Scalpel::ApplicationMenu::Recent) {
		DismissOpenMenu(menuModel, editor);
	}
}

void CollectWorkspaceOutcomes(Scalpel::DocumentWorkspace &workspace,
	Scalpel::RecentFiles &recent,
	const std::string &recentStatePath,
	Scalpel::MenuBarModel &menuModel,
	std::deque<Scalpel::DocumentFileError> &fileErrors,
	Scalpel::ApplicationEditor &editor) {
	bool recentChanged = false;
	for (const std::string &path : workspace.TakeRecentPaths()) {
		recentChanged = recent.Record(path) || recentChanged;
	}
	if (recentChanged) {
		PersistRecentFiles(recentStatePath, recent);
		SyncRecentMenu(recent, menuModel, editor);
	}

	const std::vector<Scalpel::DocumentFileError> errors =
		workspace.TakeFileErrors();
	if (errors.empty()) {
		return;
	}
	const bool wasEmpty = fileErrors.empty();
	fileErrors.insert(fileErrors.end(), errors.begin(), errors.end());
	if (wasEmpty) {
		DismissOpenMenu(menuModel, editor);
		editor.CancelActiveTextInput();
		editor.InvalidateFrame();
	}
}

void ActivateMenuBarItem(Scalpel::MenuBarItemId item,
	Scalpel::MenuBarModel &menuModel,
	Scalpel::RecentFiles &recent,
	const std::string &recentStatePath,
	Scalpel::DocumentWorkspace &workspace,
	Scalpel::ApplicationEditor &editor) {
	switch (item.kind) {
	case Scalpel::MenuBarItemKind::ApplicationAction:
		Scalpel::DispatchApplicationAction(item.action, workspace, editor);
		break;
	case Scalpel::MenuBarItemKind::RecentFile:
		if (item.recentIndex < recent.Paths().size()) {
			const std::string path = recent.Paths()[item.recentIndex];
			(void)workspace.OpenPath(path);
		}
		break;
	case Scalpel::MenuBarItemKind::ClearRecentFiles:
		if (recent.Clear()) {
			PersistRecentFiles(recentStatePath, recent);
			SyncRecentMenu(recent, menuModel, editor);
		}
		break;
	case Scalpel::MenuBarItemKind::EmptyRecentFiles:
		break;
	}
}

void PerformShellRequests(Scalpel::DocumentWorkspace &workspace,
	Scalpel::WaylandWindow &window,
	Scalpel::ApplicationEditor &editor,
	Scalpel::MenuBarModel &menuModel,
	Scalpel::TabStripModel &stripModel,
	bool &quitAccepted,
	int &cardFocus,
	std::optional<Scalpel::UnsavedCardHit> &pressHit) {
	for (const Scalpel::DocumentShellRequest request :
		workspace.TakeRequests()) {
		switch (request) {
		case Scalpel::DocumentShellRequest::ShowOpen:
			// Portals take over interaction; the in-window menu must not stay open.
			DismissOpenMenu(menuModel, editor);
			if (const std::optional<uint64_t> requestId =
					StartOpenDialog(window, workspace.Path())) {
				workspace.RegisterOpenRequest(*requestId);
			}
			break;
		case Scalpel::DocumentShellRequest::ShowSaveAs:
			DismissOpenMenu(menuModel, editor);
			if (const std::optional<uint64_t> requestId =
					RequestSaveAsDialog(window, workspace.Path())) {
				workspace.RegisterSaveAsRequest(*requestId);
			} else {
				workspace.NoteSaveAsDialogFailed();
			}
			break;
		case Scalpel::DocumentShellRequest::AcceptClose:
			quitAccepted = true;
			break;
		case Scalpel::DocumentShellRequest::PromptBegan:
			// Card input and paint priority is higher than the menu.
			DismissOpenMenu(menuModel, editor);
			cardFocus = 0;
			pressHit.reset();
			break;
		case Scalpel::DocumentShellRequest::RefreshTabs:
			(void)SyncTabStripTabs(workspace, stripModel, editor.FrameWidth());
			RevealActiveTab(stripModel, editor.FrameWidth());
			editor.InvalidateTopChrome();
			break;
		}
	}
}

void ApplyFileDialogResults(Scalpel::WaylandWindow &window,
	Scalpel::DocumentWorkspace &workspace) {
	for (const Scalpel::FileDialogResult &result :
		window.TakeFileDialogResults()) {
		const bool accepted =
			result.status == Scalpel::FileDialogResultStatus::Accepted &&
			!result.paths.empty();
		workspace.HandlePortalResult(result.id, accepted, result.paths);
	}
}

[[nodiscard]] bool IsSaveShortcut(const Scalpel::KeyboardInput &input) {
	return input.pressed &&
		input.key == static_cast<Scintilla::Keys>('S') &&
		input.modifiers == Scintilla::KeyMod::Ctrl;
}

[[nodiscard]] bool IsNextTabShortcut(const Scalpel::KeyboardInput &input) {
	return input.pressed &&
		input.key == Scintilla::Keys::Tab &&
		input.modifiers == Scintilla::KeyMod::Ctrl;
}

[[nodiscard]] bool IsPrevTabShortcut(const Scalpel::KeyboardInput &input) {
	return input.pressed &&
		input.key == Scintilla::Keys::Tab &&
		input.modifiers == (Scintilla::KeyMod::Ctrl | Scintilla::KeyMod::Shift);
}

[[nodiscard]] bool IsLetterKey(const Scalpel::KeyboardInput &input, char upper,
	char lower) {
	if (!input.pressed || input.modifiers != Scintilla::KeyMod::Norm) {
		return false;
	}
	if (input.key == static_cast<Scintilla::Keys>(upper) ||
		input.key == static_cast<Scintilla::Keys>(lower)) {
		return true;
	}
	return input.text == std::string(1, upper) ||
		input.text == std::string(1, lower);
}

bool HandlePromptKeyboard(const Scalpel::KeyboardInput &input,
	Scalpel::DocumentWorkspace &workspace,
	Scalpel::ApplicationEditor &editor,
	int &cardFocus) {
	if (!input.pressed) {
		return true;
	}
	if (input.key == Scintilla::Keys::Escape) {
		workspace.Choose(Scalpel::UnsavedChoice::Cancel);
		return true;
	}
	if (input.key == Scintilla::Keys::Tab) {
		const int delta =
			(input.modifiers == Scintilla::KeyMod::Shift) ? -1 : 1;
		cardFocus = Scalpel::CycleUnsavedCardFocus(cardFocus, delta);
		editor.InvalidateClient();
		return true;
	}
	if (input.key == Scintilla::Keys::Left) {
		cardFocus = Scalpel::CycleUnsavedCardFocus(cardFocus, -1);
		editor.InvalidateClient();
		return true;
	}
	if (input.key == Scintilla::Keys::Right) {
		cardFocus = Scalpel::CycleUnsavedCardFocus(cardFocus, 1);
		editor.InvalidateClient();
		return true;
	}
	if (input.key == Scintilla::Keys::Return ||
		input.key == static_cast<Scintilla::Keys>(' ') ||
		input.text == " ") {
		const Scalpel::UnsavedChoice choice = cardFocus == 0 ?
			Scalpel::UnsavedChoice::Save :
			(cardFocus == 1 ? Scalpel::UnsavedChoice::Discard :
				Scalpel::UnsavedChoice::Cancel);
		workspace.Choose(choice);
		return true;
	}
	if (IsSaveShortcut(input) || IsLetterKey(input, 'S', 's')) {
		workspace.Choose(Scalpel::UnsavedChoice::Save);
		return true;
	}
	if (IsLetterKey(input, 'D', 'd')) {
		workspace.Choose(Scalpel::UnsavedChoice::Discard);
		return true;
	}
	// Ignore other keys while the prompt owns input.
	return true;
}

bool HandlePromptPointer(const Scalpel::PointerInput &input,
	const Scalpel::UnsavedChangesCardLayout &layout,
	std::optional<Scalpel::UnsavedCardHit> &pressHit,
	Scalpel::DocumentWorkspace &workspace,
	Scalpel::ApplicationEditor &editor,
	int &cardFocus) {
	const Scintilla::Internal::Point point(input.x, input.y);
	if (input.action == Scalpel::PointerAction::Press && input.button == 0) {
		pressHit = Scalpel::HitTestUnsavedChangesCard(layout, point);
		if (*pressHit == Scalpel::UnsavedCardHit::Save) {
			cardFocus = 0;
			editor.InvalidateClient();
		} else if (*pressHit == Scalpel::UnsavedCardHit::Discard) {
			cardFocus = 1;
			editor.InvalidateClient();
		} else if (*pressHit == Scalpel::UnsavedCardHit::Cancel) {
			cardFocus = 2;
			editor.InvalidateClient();
		}
		return true;
	}
	if (input.action == Scalpel::PointerAction::Release && input.button == 0) {
		const Scalpel::UnsavedCardHit hit =
			Scalpel::HitTestUnsavedChangesCard(layout, point);
		if (pressHit && *pressHit == hit) {
			if (hit == Scalpel::UnsavedCardHit::Save) {
				workspace.Choose(Scalpel::UnsavedChoice::Save);
			} else if (hit == Scalpel::UnsavedCardHit::Discard) {
				workspace.Choose(Scalpel::UnsavedChoice::Discard);
			} else if (hit == Scalpel::UnsavedCardHit::Cancel) {
				workspace.Choose(Scalpel::UnsavedChoice::Cancel);
			}
		}
		pressHit.reset();
		return true;
	}
	// Scrim clicks, move, and scroll do not cancel or edit.
	return true;
}

std::string_view FileErrorTitle(
	const Scalpel::DocumentFileError &error) noexcept {
	return error.operation == Scalpel::DocumentFileOperation::Open ?
		"Could not open file" :
		"Could not save file";
}

void DismissFileError(std::deque<Scalpel::DocumentFileError> &errors,
	Scalpel::ApplicationEditor &editor,
	bool &pressHit) {
	if (!errors.empty()) {
		errors.pop_front();
	}
	pressHit = false;
	editor.InvalidateFrame();
}

bool HandleFileErrorKeyboard(const Scalpel::KeyboardInput &input,
	std::deque<Scalpel::DocumentFileError> &errors,
	Scalpel::ApplicationEditor &editor,
	bool &pressHit) {
	if (!input.pressed) {
		return true;
	}
	if (input.key == Scintilla::Keys::Escape ||
		input.key == Scintilla::Keys::Return ||
		input.key == static_cast<Scintilla::Keys>(' ') ||
		input.text == " ") {
		DismissFileError(errors, editor, pressHit);
	}
	return true;
}

bool HandleFileErrorPointer(const Scalpel::PointerInput &input,
	const Scalpel::FileErrorCardLayout &layout,
	std::deque<Scalpel::DocumentFileError> &errors,
	Scalpel::ApplicationEditor &editor,
	bool &pressHit) {
	const Scintilla::Internal::Point point(input.x, input.y);
	if (input.action == Scalpel::PointerAction::Press && input.button == 0) {
		pressHit = Scalpel::HitTestFileErrorCard(layout, point);
		return true;
	}
	if (input.action == Scalpel::PointerAction::Release && input.button == 0) {
		const bool releaseHit = Scalpel::HitTestFileErrorCard(layout, point);
		if (pressHit && releaseHit) {
			DismissFileError(errors, editor, pressHit);
		} else {
			pressHit = false;
		}
		return true;
	}
	return true;
}

[[nodiscard]] bool PointerInTopChrome(const Scalpel::PointerInput &input,
	const Scalpel::ApplicationLayout &layout) noexcept {
	if (input.action == Scalpel::PointerAction::Leave) {
		return false;
	}
	const int inset = layout.topChromeInset;
	if (inset <= 0) {
		return false;
	}
	return input.x >= 0.0 && input.x < static_cast<double>(layout.frameWidth) &&
		input.y >= 0.0 && input.y < static_cast<double>(inset);
}

void CancelScrollBarShellInteraction(Scalpel::ScrollBarInteraction &interaction,
	Scalpel::ApplicationEditor &editor) {
	const bool paintChanged = interaction.dragging ||
		interaction.pressed != Scalpel::ScrollBarHit::None ||
		interaction.hover != Scalpel::ScrollBarHit::None;
	const bool wheelPending = interaction.verticalWheelRemainder != 0.0 ||
		interaction.horizontalWheelRemainder != 0.0;
	if (!paintChanged && !wheelPending) {
		return;
	}
	Scalpel::CancelScrollBarInteraction(interaction);
	if (paintChanged) {
		editor.InvalidateScrollBars();
	}
}

void ApplyScrollBarShellRequest(Scalpel::ApplicationEditor &editor,
	const Scalpel::ScrollBarRequest &request) {
	if (request.kind == Scalpel::ScrollBarRequestKind::SetVertical) {
		editor.ScrollVerticalTo(request.position);
	} else if (request.kind == Scalpel::ScrollBarRequestKind::SetHorizontal) {
		editor.ScrollHorizontalTo(static_cast<int>(request.position));
	}
}

/**
 * Apply scrollbar pointer transitions. Returns true when the event must not
 * reach the editor. Active drag owns motion and release even outside the track.
 * layout.scrollBars must match the current frame and editor metrics.
 */
bool HandleScrollBarShellPointer(const Scalpel::PointerInput &input,
	const Scalpel::ScrollBarLayout &scrollBars,
	Scalpel::ScrollBarInteraction &interaction,
	Scalpel::ApplicationEditor &editor,
	bool &pointerOverChrome) {
	const Scalpel::ScrollBarPointerResult result =
		Scalpel::HandleScrollBarPointer(interaction, scrollBars, input);
	if (result.barDirty) {
		editor.InvalidateScrollBars();
	}
	ApplyScrollBarShellRequest(editor, result.request);
	if (result.pointerOverScrollBar) {
		pointerOverChrome = true;
	}
	return result.consumed;
}

Scalpel::TabStripHitResult UpdateTabStripPointerState(
	const Scalpel::PointerInput &input,
	const Scalpel::ApplicationLayout &layout,
	Scalpel::TabStripModel &model,
	Scalpel::ApplicationEditor &editor,
	bool &pointerOverChrome) {
	Scalpel::TabStripHitResult hit;
	if (input.action != Scalpel::PointerAction::Leave) {
		hit = Scalpel::HitTestTabStrip(
			layout.tabs, Scintilla::Internal::Point(input.x, input.y));
	}

	// Arrow cursor over the whole permanent chrome band (menu bar + strip).
	pointerOverChrome = hit.kind != Scalpel::TabStripHit::None ||
		PointerInTopChrome(input, layout);
	Scalpel::DocumentId hoveredId = 0;
	bool closeHovered = false;
	if (hit.kind == Scalpel::TabStripHit::Tab ||
		hit.kind == Scalpel::TabStripHit::Close) {
		hoveredId = hit.tabId;
		closeHovered = hit.kind == Scalpel::TabStripHit::Close;
	}
	if (hoveredId != model.hoveredId ||
		closeHovered != model.closeHovered) {
		model.hoveredId = hoveredId;
		model.closeHovered = closeHovered;
		editor.InvalidateTopChrome();
	}
	return hit;
}

/**
 * Apply menu, tab-strip, and scrollbar pointer transitions.
 * Returns true when the event must not reach the editor. Priority after modals:
 * active scrollbar drag, open menu, top chrome, scrollbar hit, then editor.
 * Editor selection capture still bypasses chrome so selection release is
 * delivered; an active scrollbar drag is cancelled when a menu opens.
 * layout is one frame snapshot shared by menu, tab, and scrollbar hit tests.
 */
bool HandleTopChromePointer(const Scalpel::PointerInput &input,
	const Scalpel::ApplicationLayout &layout,
	Scalpel::MenuBarModel &menuModel,
	Scalpel::TabStripModel &stripModel,
	Scalpel::ScrollBarInteraction &scrollBarInteraction,
	Scalpel::RecentFiles &recent,
	const std::string &recentStatePath,
	Scalpel::DocumentWorkspace &workspace,
	Scalpel::ApplicationEditor &editor,
	bool &pointerOverChrome) {
	const bool captured = editor.WindowState().mouseCaptured;

	// Active scrollbar drag owns the sequence before top chrome (not when the
	// editor already holds a selection capture).
	if (!captured && scrollBarInteraction.dragging) {
		return HandleScrollBarShellPointer(input, layout.scrollBars,
			scrollBarInteraction, editor, pointerOverChrome);
	}

	const bool menuWasOpen = menuModel.openMenu.has_value();
	// The caller refreshes enablement before building layout so item activation
	// and geometry read one coherent snapshot.
	const Scalpel::MenuBarPointerResult menuResult =
		Scalpel::HandleMenuBarPointer(menuModel, layout.menu, input, captured);

	if (!menuWasOpen && menuModel.openMenu.has_value()) {
		// Opening a menu cancels tentative IME; batches stay dropped while open.
		editor.CancelActiveTextInput();
		CancelScrollBarShellInteraction(scrollBarInteraction, editor);
	}
	if (menuResult.barDirty) {
		editor.InvalidateTopChrome();
	}
	if (menuResult.frameDirty) {
		// Open/close and dropdown hover need a full frame so the overlay path
		// cannot leave a stale panel.
		editor.InvalidateFrame();
	}
	if (menuResult.activated) {
		// Dropdown is already closed before document or persistent state changes.
		ActivateMenuBarItem(*menuResult.activated, menuModel, recent,
			recentStatePath, workspace, editor);
	}

	// A selection drag that began in the editor still owns motion and release
	// over chrome. Scintilla must see release to drop capture; the menu
	// handler already refused to consume those events.
	if (captured) {
		(void)UpdateTabStripPointerState(
			input, layout, stripModel, editor, pointerOverChrome);
		if (menuResult.pointerOverMenu) {
			pointerOverChrome = true;
		}
		return false;
	}

	// While a menu is open it owns presses and moves; keep strip hover clear
	// so tabs under the dropdown do not highlight.
	if (menuModel.openMenu.has_value()) {
		if (stripModel.hoveredId != 0 || stripModel.closeHovered) {
			stripModel.hoveredId = 0;
			stripModel.closeHovered = false;
			editor.InvalidateTopChrome();
		}
		pointerOverChrome = menuResult.pointerOverMenu ||
			PointerInTopChrome(input, layout);
		// Surface leave still needs to clear editor hover.
		if (input.action == Scalpel::PointerAction::Leave) {
			return false;
		}
		return true;
	}

	if (menuResult.consumed) {
		// Closed menu still swallows bar-band events (heading open, empty bar).
		(void)UpdateTabStripPointerState(
			input, layout, stripModel, editor, pointerOverChrome);
		if (menuResult.pointerOverMenu) {
			pointerOverChrome = true;
		}
		return true;
	}

	const Scalpel::TabStripHitResult stripHit = UpdateTabStripPointerState(
		input, layout, stripModel, editor, pointerOverChrome);
	if (menuResult.pointerOverMenu) {
		pointerOverChrome = true;
	}

	const bool inStrip = stripHit.kind != Scalpel::TabStripHit::None;
	const bool inTopChrome = inStrip || PointerInTopChrome(input, layout);
	if (inTopChrome) {
		if (!inStrip) {
			// Menu bar band already handled above when it owns the hit.
			return true;
		}

		if (input.action == Scalpel::PointerAction::Move) {
			return true;
		}

		if (input.action == Scalpel::PointerAction::Scroll) {
			const double amount =
				std::abs(input.deltaX) > std::abs(input.deltaY) ?
				input.deltaX :
				input.deltaY;
			int delta = 0;
			if (amount > 0.0) {
				delta = 1;
			} else if (amount < 0.0) {
				delta = -1;
			}
			if (delta != 0) {
				const int next = Scalpel::AdjustTabStripScroll(
					layout.frameWidth, stripModel.tabs.size(),
					stripModel.scrollOffset, delta,
					Scalpel::TabStripPreferredTabWidth() / 2);
				if (next != stripModel.scrollOffset) {
					stripModel.scrollOffset = next;
					editor.InvalidateTopChrome();
				}
			}
			return true;
		}

		if (input.action == Scalpel::PointerAction::Press && input.button == 0) {
			// Tab work must not leave a menu open (for example after a race with
			// a dismissal release that already cleared openMenu).
			DismissOpenMenu(menuModel, editor);
			CancelScrollBarShellInteraction(scrollBarInteraction, editor);
			if (stripHit.kind == Scalpel::TabStripHit::Tab) {
				workspace.ActivateTab(stripHit.tabId);
			} else if (stripHit.kind == Scalpel::TabStripHit::Close) {
				workspace.CloseTab(stripHit.tabId);
			} else if (stripHit.kind == Scalpel::TabStripHit::Add) {
				workspace.NewTab();
			}
			return true;
		}

		// Swallow other strip presses/releases so the editor does not select.
		return true;
	}

	// Below top chrome: scrollbar hit testing before the editor.
	if (HandleScrollBarShellPointer(input, layout.scrollBars,
		scrollBarInteraction, editor, pointerOverChrome)) {
		return true;
	}
	// Surface leave still needs to clear editor hover.
	return false;
}

/**
 * Apply menu keyboard transitions before editor shortcuts.
 * Returns true when the event must not reach the editor (open accelerators
 * while closed, or any key while a menu owns input).
 */
bool HandleMenuBarKeyboardInput(const Scalpel::KeyboardInput &input,
	Scalpel::MenuBarModel &menuModel,
	Scalpel::RecentFiles &recent,
	const std::string &recentStatePath,
	Scalpel::DocumentWorkspace &workspace,
	Scalpel::ApplicationEditor &editor) {
	const bool menuWasOpen = menuModel.openMenu.has_value();
	// Refresh before open accelerators so FirstEnabledItem uses live state.
	(void)Scalpel::UpdateMenuBarActionState(menuModel, editor);
	const Scalpel::MenuBarKeyboardResult menuResult =
		Scalpel::HandleMenuBarKeyboard(menuModel, input);
	if (!menuWasOpen && menuModel.openMenu.has_value()) {
		editor.CancelActiveTextInput();
	}
	if (menuResult.barDirty) {
		editor.InvalidateTopChrome();
	}
	if (menuResult.frameDirty) {
		editor.InvalidateFrame();
	}
	if (menuResult.activated) {
		ActivateMenuBarItem(*menuResult.activated, menuModel, recent,
			recentStatePath, workspace, editor);
	}
	return menuResult.consumed;
}

bool HandleEditorKeyboard(const Scalpel::KeyboardInput &input,
	Scalpel::DocumentWorkspace &workspace,
	Scalpel::ApplicationEditor &editor) {
	// File/Edit shortcuts and Ctrl+Q share the menu action dispatcher.
	if (const std::optional<Scalpel::ApplicationAction> action =
		Scalpel::MatchApplicationAction(input)) {
		Scalpel::DispatchApplicationAction(*action, workspace, editor);
		return true;
	}
	// Tab cycling stays outside the File/Edit action list.
	if (IsPrevTabShortcut(input)) {
		workspace.CycleTab(-1);
		return true;
	}
	if (IsNextTabShortcut(input)) {
		workspace.CycleTab(1);
		return true;
	}
	editor.HandleKeyboardInput(input);
	return false;
}

}

int main() {
	try {
		Scalpel::WaylandWindow window("scalpel-editor", 800, 600);
		auto glContext = std::make_unique<Scintilla::Internal::GlContext>(
			window.Display(), window.EglWindow());
		Scalpel::ApplicationEditor editor(
			std::move(glContext), window.Width(), window.Height());
		editor.SetFrameBufferSize(
			window.ScaleConfiguration().bufferWidth,
			window.ScaleConfiguration().bufferHeight);
		constexpr std::string_view initialText =
			"scalpel-editor\n\n"
			"A direct Scintilla editor for Wayland.\n"
			"Ctrl+N new tab, Ctrl+W close tab, Ctrl+Tab cycle tabs.\n"
			"Ctrl+O open, Ctrl+S save, Ctrl+Shift+S save as, Ctrl+Q quit.\n";
		editor.LoadInitialBuffer(initialText);
		const int topChromeInset =
			Scalpel::MenuBarHeight() + Scalpel::TabStripHeight();
		editor.SetTopChromeInset(topChromeInset);
		Scalpel::DocumentWorkspace workspace(editor);
		const std::string recentStatePath = Scalpel::RecentFilesStatePath();
		Scalpel::RecentFiles recent =
			Scalpel::LoadRecentFiles(recentStatePath);
		Scalpel::ApplicationUi ui(editor, workspace, recent, recentStatePath);
		Scalpel::MenuBarPainter menuPainter;
		Scalpel::TabStripPainter stripPainter;
		Scalpel::UnsavedChangesCardPainter cardPainter;
		Scalpel::FileErrorCardPainter fileErrorPainter;
		bool quitAccepted = false;
		const auto synchronizeScrollBarInteraction = [&] {
			const Scalpel::DocumentId activeDocument = editor.ActiveDocument();
			const bool documentChanged =
				activeDocument != ui.LastActiveDocument();
			if (documentChanged || !ui.FileErrors().empty() ||
				workspace.PromptActive() ||
				ui.MenuModel().openMenu.has_value()) {
				CancelScrollBarShellInteraction(ui.ScrollBars(), editor);
			}
			ui.LastActiveDocument() = activeDocument;
		};

		(void)SyncTabStripTabs(workspace, ui.StripModel(), editor.FrameWidth());
		editor.InvalidateTopChrome();
		editor.InvalidateScrollBars();

		editor.SetPermanentChromePainter(
			[&](Scintilla::Internal::Surface &surface, int, int) {
				const Scalpel::ApplicationLayout &layout = ui.FrameLayout();
				menuPainter.PaintBar(surface, layout.menu, ui.MenuModel());
				stripPainter.Paint(surface, layout.tabs, ui.StripModel());
				Scalpel::PaintScrollBars(surface, layout.scrollBars,
					Scalpel::ScrollBarPaintFromInteraction(ui.ScrollBars()));
			});

		const auto paintMenuDropdown =
			[&](Scintilla::Internal::Surface &surface, int, int) {
				const Scalpel::ApplicationLayout &layout = ui.FrameLayout();
				menuPainter.PaintDropdown(surface, layout.menu, ui.MenuModel());
			};

		const auto paintUnsavedCard =
			[&](Scintilla::Internal::Surface &surface, int, int) {
				const Scalpel::ApplicationLayout &layout = ui.FrameLayout();
				const std::string subtitle = UnsavedPromptSubtitle(workspace);
				cardPainter.Paint(surface, layout.unsavedCard, "Save changes?",
					subtitle, ui.CardFocus());
			};

		const auto paintFileError =
			[&](Scintilla::Internal::Surface &surface, int, int) {
				if (ui.FileErrors().empty()) {
					return;
				}
				const Scalpel::ApplicationLayout &layout = ui.FrameLayout();
				const Scalpel::DocumentFileError &error = ui.FileErrors().front();
				fileErrorPainter.Paint(surface, layout.fileErrorCard,
					FileErrorTitle(error), error.path);
			};

		while (!quitAccepted && !window.ForceCloseRequested()) {
			(void)window.TakePresentationResults();
			DeliverClipboardResults(window, editor);
			DeliverPrimarySelectionResults(window, editor);
			// Modal cards and an open menu all suppress IME delivery.
			DeliverTextInputBatches(window, editor,
				!ui.FileErrors().empty() || workspace.PromptActive() ||
					ui.MenuModel().openMenu.has_value());
			ApplyFileDialogResults(window, workspace);
			PerformShellRequests(workspace, window, editor, ui.MenuModel(),
				ui.StripModel(), quitAccepted, ui.CardFocus(),
				ui.PromptPressHit());
			CollectWorkspaceOutcomes(workspace, recent, recentStatePath,
				ui.MenuModel(), ui.FileErrors(), editor);
			// Portal results can activate a document or surface a modal without
			// an input event. Neither may inherit an old scrollbar interaction.
			synchronizeScrollBarInteraction();
			SynchronizeTextInput(editor, window);
			// Clipboard offer can arrive while a dropdown is open; flip paste
			// enablement and force a full-frame paint so the row updates.
			if (ui.MenuModel().openMenu.has_value() &&
				Scalpel::UpdateMenuBarActionState(ui.MenuModel(), editor)) {
				editor.InvalidateFrame();
			}

			if (window.ForceCloseRequested()) {
				// Force-close exits without a prompt; drop menu state so the
				// final frames cannot paint a stale dropdown.
				DismissOpenMenu(ui.MenuModel(), editor);
				break;
			}
			if (window.UserCloseRequested()) {
				workspace.RequestClose();
				PerformShellRequests(workspace, window, editor, ui.MenuModel(),
					ui.StripModel(), quitAccepted, ui.CardFocus(),
					ui.PromptPressHit());
				synchronizeScrollBarInteraction();
				window.ClearCloseRequest();
				if (quitAccepted) {
					break;
				}
			}

			if (const std::optional<Scalpel::WaylandScaleConfiguration> scale =
				window.TakeScaleConfiguration()) {
				if (editor.FrameWidth() != scale->logicalWidth ||
					editor.FrameHeight() != scale->logicalHeight) {
					editor.Resize(scale->logicalWidth, scale->logicalHeight);
				}
				editor.SetFrameBufferSize(scale->bufferWidth, scale->bufferHeight);
				// Width may change scroll clamping and tab layout. Keep an open
				// menu; LayoutMenuBar recomputes headings and clamps the panel.
				// Resize and scale cancel an in-progress scrollbar drag.
				CancelScrollBarShellInteraction(ui.ScrollBars(), editor);
				(void)SyncTabStripTabs(
					workspace, ui.StripModel(), editor.FrameWidth());
				RevealActiveTab(ui.StripModel(), editor.FrameWidth());
				editor.InvalidateTopChrome();
				editor.InvalidateScrollBars();
				if (!ui.FileErrors().empty() || workspace.PromptActive() ||
					ui.MenuModel().openMenu.has_value()) {
					// Full frame so the card or dropdown cannot leave stale pixels.
					editor.InvalidateFrame();
				}
			}

			for (const Scalpel::InputEvent &input : window.TakeInputs()) {
				if (const auto *focus =
					std::get_if<Scalpel::KeyboardFocusInput>(&input)) {
					editor.SetKeyboardFocus(focus->focused);
					if (!focus->focused) {
						// Focus loss closes the menu; IME cancel is in SetKeyboardFocus.
						DismissOpenMenu(ui.MenuModel(), editor);
						CancelScrollBarShellInteraction(
							ui.ScrollBars(), editor);
					}
					continue;
				}
				// Refresh model values copied into MenuBarLayout before taking
				// the one snapshot used by every hit test for this event.
				(void)Scalpel::UpdateMenuBarActionState(
					ui.MenuModel(), editor);
				const Scalpel::ApplicationLayout layout = ui.Layout();
				// Input priority: file error, unsaved card, open menu, active
				// scrollbar drag, top chrome, scrollbar hit, then editor.
				if (!ui.FileErrors().empty()) {
					CancelScrollBarShellInteraction(
						ui.ScrollBars(), editor);
					if (const auto *keyboard =
						std::get_if<Scalpel::KeyboardInput>(&input)) {
						HandleFileErrorKeyboard(*keyboard, ui.FileErrors(),
							editor, ui.FileErrorPressHit());
					} else if (const auto *pointer =
						std::get_if<Scalpel::PointerInput>(&input)) {
						HandleFileErrorPointer(*pointer, layout.fileErrorCard,
							ui.FileErrors(), editor, ui.FileErrorPressHit());
					}
					continue;
				}
				if (workspace.PromptActive()) {
					CancelScrollBarShellInteraction(
						ui.ScrollBars(), editor);
					if (const auto *keyboard =
						std::get_if<Scalpel::KeyboardInput>(&input)) {
						HandlePromptKeyboard(*keyboard, workspace, editor,
							ui.CardFocus());
					} else if (const auto *pointer =
						std::get_if<Scalpel::PointerInput>(&input)) {
						(void)UpdateTabStripPointerState(*pointer, layout,
							ui.StripModel(), editor, ui.PointerOverChrome());
						HandlePromptPointer(*pointer, layout.unsavedCard,
							ui.PromptPressHit(), workspace, editor,
							ui.CardFocus());
					}
					PerformShellRequests(workspace, window, editor,
						ui.MenuModel(), ui.StripModel(), quitAccepted,
						ui.CardFocus(), ui.PromptPressHit());
					CollectWorkspaceOutcomes(workspace, recent, recentStatePath,
						ui.MenuModel(), ui.FileErrors(), editor);
					continue;
				}
				if (const auto *keyboard =
					std::get_if<Scalpel::KeyboardInput>(&input)) {
					// Menu accelerators and open-menu navigation run before
					// application shortcuts and editor typing.
					if (!HandleMenuBarKeyboardInput(*keyboard, ui.MenuModel(),
						recent, recentStatePath, workspace, editor)) {
						HandleEditorKeyboard(*keyboard, workspace, editor);
					}
				} else {
					const auto &pointer =
						std::get<Scalpel::PointerInput>(input);
					if (!HandleTopChromePointer(pointer, layout, ui.MenuModel(),
						ui.StripModel(), ui.ScrollBars(), recent,
						recentStatePath, workspace, editor,
						ui.PointerOverChrome())) {
						editor.HandlePointerInput(pointer);
					}
				}
				PerformShellRequests(workspace, window, editor, ui.MenuModel(),
					ui.StripModel(), quitAccepted, ui.CardFocus(),
					ui.PromptPressHit());
				CollectWorkspaceOutcomes(workspace, recent, recentStatePath,
					ui.MenuModel(), ui.FileErrors(), editor);
				// Includes menus opened from the keyboard as well as tab changes.
				synchronizeScrollBarInteraction();
			}

			if (quitAccepted || window.ForceCloseRequested()) {
				DismissOpenMenu(ui.MenuModel(), editor);
				CancelScrollBarShellInteraction(ui.ScrollBars(), editor);
				break;
			}

			DispatchClipboardRequests(editor, window);
			DispatchPrimarySelectionRequests(editor, window);
			editor.RunPendingWork();
			// Edits can flip dirty markers without a workspace request.
			if (SyncTabStripTabs(workspace, ui.StripModel(), editor.FrameWidth())) {
				editor.InvalidateTopChrome();
			}
			SynchronizeTextInput(editor, window);
			if (!ui.FileErrors().empty()) {
				window.SetCursor(Scintilla::Internal::Window::Cursor::arrow);
				if (ui.MenuModel().openMenu.has_value()) {
					Scalpel::CloseMenuBar(ui.MenuModel());
				}
				if (ui.Overlay() != Scalpel::BoundOverlay::FileError) {
					editor.SetOverlayPainter(paintFileError);
					ui.SetOverlay(Scalpel::BoundOverlay::FileError);
					editor.InvalidateFrame();
				}
			} else if (workspace.PromptActive()) {
				window.SetCursor(Scintilla::Internal::Window::Cursor::arrow);
				if (ui.MenuModel().openMenu.has_value()) {
					Scalpel::CloseMenuBar(ui.MenuModel());
				}
				if (ui.Overlay() != Scalpel::BoundOverlay::UnsavedChanges) {
					editor.SetOverlayPainter(paintUnsavedCard);
					ui.SetOverlay(Scalpel::BoundOverlay::UnsavedChanges);
					editor.InvalidateFrame();
				}
			} else if (ui.MenuModel().openMenu.has_value()) {
				window.SetCursor(ui.PointerOverChrome() ?
					Scintilla::Internal::Window::Cursor::arrow :
					editor.WindowState().cursor);
				// Dropdown uses the overlay path only while a menu is open.
				if (ui.Overlay() != Scalpel::BoundOverlay::Menu) {
					editor.SetOverlayPainter(paintMenuDropdown);
					ui.SetOverlay(Scalpel::BoundOverlay::Menu);
					editor.InvalidateFrame();
				}
			} else {
				window.SetCursor(ui.PointerOverChrome() ?
					Scintilla::Internal::Window::Cursor::arrow :
					editor.WindowState().cursor);
				if (ui.Overlay() != Scalpel::BoundOverlay::None) {
					// Clear the painter and damage the full frame so preserved
					// buffer contents cannot leave a stale dropdown or card.
					editor.SetOverlayPainter(nullptr);
					ui.SetOverlay(Scalpel::BoundOverlay::None);
					editor.InvalidateFrame();
				}
			}
			QueueFrameDamage(editor, window);
			if (window.CanSubmitFrame()) {
				const std::optional<Scalpel::FramePlan> plan = window.BeginFrame(
					editor.BufferAge(),
					editor.BufferAgeSupported(), editor.DamageSwapSupported());
				if (plan) {
					window.PrepareFrame(*plan);
					try {
						// Finalize every model value copied into the layout before
						// retaining one snapshot for both paint callbacks.
						(void)Scalpel::UpdateMenuBarActionState(
							ui.MenuModel(), editor);
						ui.StripModel().scrollOffset =
							Scalpel::ClampTabStripScroll(editor.FrameWidth(),
								ui.StripModel().tabs.size(),
								ui.StripModel().scrollOffset);
						ui.BeginFrameLayout();
						editor.PresentFrame(EditorDamage(plan->repaintDamage),
							EglDamage(plan->eglDamage), plan->fullSwap);
						ui.EndFrameLayout();
						window.SubmitFrame(plan->submission);
					} catch (...) {
						ui.EndFrameLayout();
						window.CancelFrame();
						throw;
					}
				}
			}
			if (!quitAccepted && !window.ForceCloseRequested()) {
				window.WaitForEvents(editor.TimeUntilNextWork());
			}
		}
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "scalpel-editor: " << error.what() << '\n';
		return 1;
	}
}
