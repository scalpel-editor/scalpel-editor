// Production Wayland editor process entry.
//
// Ownership: ApplicationEditor retains Scintilla documents and paints one
// surface (one renderer, one EGL context). DocumentWorkspace owns tab order,
// paths, untitled numbering, portal request intents, and dirty-close prompts;
// it returns shell requests and owns no Wayland or drawing. ApplicationUi owns
// menu, tab-strip, scrollbar interaction, modal-card, file-error queue, hover,
// press, overlay-selection state, and pointer and keyboard routing (file error,
// unsaved card, menu, scrollbar drag, editor capture, permanent chrome, then
// editor; keyboard: modals, menu, application shortcuts, tab cycling, editor).
// Focus loss closes menus, cancels scrollbar interaction, and clears press
// state. Opening a menu or modal card cancels tentative IME; ChromeOwnsInput
// drops IME batches while chrome owns keyboard. MenuBar, TabStrip, and
// ScrollBar are permanent chrome. The open menu dropdown, UnsavedChangesCard,
// and FileErrorCard share the post-paint overlay slot. This file is the event
// and rendering adapter: it delivers portal results, calls ApplicationUi for
// pointer, keyboard, and focus, performs shell requests (dialogs, quit, strip
// refresh), dismisses menus around portals and force close, and paints chrome.
// Hit testing and painting share one ApplicationLayout snapshot per event or
// paint pass. Text-input protocol conversion stays here. Open/save policy and
// prompt transitions live in DocumentWorkspace, not here. Required-global loss
// force-closes without prompting the workspace.

#include <cmath>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
			// Modal card or open menu owns input; drop IME until it closes.
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
	Scalpel::ApplicationUi &ui) {
	bool recentChanged = false;
	for (const std::string &path : workspace.TakeRecentPaths()) {
		recentChanged = recent.Record(path) || recentChanged;
	}
	if (recentChanged) {
		PersistRecentFiles(recentStatePath, recent);
		SyncRecentMenu(recent, ui.MenuModel(), ui.Editor());
	}

	ui.AppendFileErrors(workspace.TakeFileErrors());
}

void PerformShellRequests(Scalpel::DocumentWorkspace &workspace,
	Scalpel::WaylandWindow &window,
	Scalpel::ApplicationUi &ui,
	bool &quitAccepted) {
	for (const Scalpel::DocumentShellRequest request :
		workspace.TakeRequests()) {
		switch (request) {
		case Scalpel::DocumentShellRequest::ShowOpen:
			// Portals take over interaction; the in-window menu must not stay open.
			DismissOpenMenu(ui.MenuModel(), ui.Editor());
			if (const std::optional<uint64_t> requestId =
					StartOpenDialog(window, workspace.Path())) {
				workspace.RegisterOpenRequest(*requestId);
			}
			break;
		case Scalpel::DocumentShellRequest::ShowSaveAs:
			DismissOpenMenu(ui.MenuModel(), ui.Editor());
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
			ui.NotifyPromptBegan();
			break;
		case Scalpel::DocumentShellRequest::RefreshTabs:
			(void)SyncTabStripTabs(workspace, ui.StripModel(),
				ui.Editor().FrameWidth());
			RevealActiveTab(ui.StripModel(), ui.Editor().FrameWidth());
			ui.Editor().InvalidateTopChrome();
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

std::string_view FileErrorTitle(
	const Scalpel::DocumentFileError &error) noexcept {
	return error.operation == Scalpel::DocumentFileOperation::Open ?
		"Could not open file" :
		"Could not save file";
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
		Scalpel::ApplicationPointerCursor pointerCursor =
			Scalpel::ApplicationPointerCursor::Editor;
		const auto applyPointerCursor = [&] {
			if (pointerCursor == Scalpel::ApplicationPointerCursor::Arrow) {
				window.SetCursor(Scintilla::Internal::Window::Cursor::arrow);
			} else {
				window.SetCursor(editor.WindowState().cursor);
			}
		};
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
			DeliverTextInputBatches(window, editor, ui.ChromeOwnsInput());
			ApplyFileDialogResults(window, workspace);
			PerformShellRequests(workspace, window, ui, quitAccepted);
			CollectWorkspaceOutcomes(workspace, recent, recentStatePath, ui);
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
				PerformShellRequests(workspace, window, ui, quitAccepted);
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
					// Focus gain/loss, menu dismiss, scrollbar cancel, and
					// press clear are one ApplicationUi transition.
					ui.HandleFocus(focus->focused);
					continue;
				}
				if (const auto *pointer =
					std::get_if<Scalpel::PointerInput>(&input)) {
					// File error, unsaved card, menu, bars, and chrome ownership
					// are decided inside ApplicationUi; deliver leftovers only.
					const Scalpel::ApplicationPointerResult pointerResult =
						ui.HandlePointer(*pointer);
					pointerCursor = pointerResult.cursor;
					if (!pointerResult.consumed) {
						editor.HandlePointerInput(*pointer);
					}
					PerformShellRequests(workspace, window, ui, quitAccepted);
					CollectWorkspaceOutcomes(workspace, recent, recentStatePath,
						ui);
					synchronizeScrollBarInteraction();
					continue;
				}
				if (const auto *keyboard =
					std::get_if<Scalpel::KeyboardInput>(&input)) {
					// Modal cards, menu, shortcuts, tab cycling, and editor
					// delivery are decided inside ApplicationUi.
					(void)ui.HandleKeyboard(*keyboard);
				}
				PerformShellRequests(workspace, window, ui, quitAccepted);
				CollectWorkspaceOutcomes(workspace, recent, recentStatePath,
					ui);
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
				applyPointerCursor();
				// Dropdown uses the overlay path only while a menu is open.
				if (ui.Overlay() != Scalpel::BoundOverlay::Menu) {
					editor.SetOverlayPainter(paintMenuDropdown);
					ui.SetOverlay(Scalpel::BoundOverlay::Menu);
					editor.InvalidateFrame();
				}
			} else {
				applyPointerCursor();
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
