// Production Wayland editor process entry.
//
// Ownership: ApplicationEditor retains Scintilla documents and paints one
// surface (one renderer, one EGL context). DocumentWorkspace owns tab order,
// paths, untitled numbering, file-dialog intents, and dirty-close prompts;
// it returns shell requests and owns no Wayland or drawing. ApplicationUi owns
// menu, tab-strip, scrollbar interaction, modal-card, file-error queue, hover,
// press, painters, overlay composition, pointer and keyboard routing (file
// error, unsaved card, menu, scrollbar drag, editor capture, permanent chrome,
// then editor; keyboard: modals, menu, application shortcuts, tab cycling,
// editor), and workspace-request consumption into typed shell effects. Focus
// loss closes menus, cancels scrollbar interaction, and clears press state.
// Opening a menu or modal card cancels tentative IME; ChromeOwnsInput drops
// IME batches while chrome owns keyboard. MenuBar, TabStrip, and ScrollBar are
// permanent chrome. The open menu dropdown, UnsavedChangesCard, and
// FileErrorCard share the post-paint overlay slot through ApplicationUi. This
// file is the event and rendering adapter: it maps portal request IDs to
// application dialog identities, delivers results through ApplicationUi,
// drains TakeShellEffects for portal dialogs and accept-close, dismisses menus
// on force close, and submits frames. Hit
// testing and painting share one ApplicationLayout snapshot per event or paint
// pass. Text-input protocol conversion stays here. Open/save policy and prompt
// transitions live in DocumentWorkspace, not here. Required-global loss
// force-closes without prompting the workspace.

#include <cmath>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ApplicationEditor.h"
#include "ApplicationUi.h"
#include "DocumentFile.h"
#include "DocumentWorkspace.h"
#include "GlContext.h"
#include "MenuBar.h"
#include "RecentFiles.h"
#include "ScrollBar.h"
#include "TabStrip.h"
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

/** Close an open menu and force a full-frame repaint so the dropdown cannot linger. */
void DismissOpenMenu(Scalpel::MenuBarModel &menuModel,
	Scalpel::ApplicationEditor &editor) {
	if (!menuModel.openMenu.has_value()) {
		return;
	}
	Scalpel::CloseMenuBar(menuModel);
	editor.InvalidateFrame();
}

/**
 * Drain ApplicationUi shell effects. Application-side work is already applied;
 * only portal dialogs and accept-close remain host work.
 */
using ActiveFileDialogs =
	std::unordered_map<uint64_t, Scalpel::DocumentDialogId>;

void ApplyShellEffects(Scalpel::ApplicationUi &ui,
	Scalpel::WaylandWindow &window,
	ActiveFileDialogs &activeFileDialogs,
	bool &quitAccepted) {
	for (const Scalpel::ApplicationShellEffect &effect : ui.TakeShellEffects()) {
		switch (effect.kind) {
		case Scalpel::ApplicationShellEffectKind::ShowOpen:
			if (const std::optional<uint64_t> requestId =
					StartOpenDialog(window, effect.documentPath)) {
				activeFileDialogs[*requestId] = effect.dialogId;
			} else {
				ui.NotifyDialogFailed(effect.dialogId);
			}
			break;
		case Scalpel::ApplicationShellEffectKind::ShowSaveAs:
			if (const std::optional<uint64_t> requestId =
					RequestSaveAsDialog(window, effect.documentPath)) {
				activeFileDialogs[*requestId] = effect.dialogId;
			} else {
				ui.NotifyDialogFailed(effect.dialogId);
			}
			break;
		case Scalpel::ApplicationShellEffectKind::AcceptClose:
			quitAccepted = true;
			break;
		}
	}
}

void ApplyFileDialogResults(Scalpel::WaylandWindow &window,
	Scalpel::ApplicationUi &ui,
	ActiveFileDialogs &activeFileDialogs) {
	for (const Scalpel::FileDialogResult &result :
		window.TakeFileDialogResults()) {
		const auto intent = activeFileDialogs.find(result.id);
		if (intent == activeFileDialogs.end()) {
			continue;
		}
		const Scalpel::DocumentDialogId dialogId = intent->second;
		activeFileDialogs.erase(intent);
		const bool accepted =
			result.status == Scalpel::FileDialogResultStatus::Accepted &&
			!result.paths.empty();
		ui.NotifyDialogResult(dialogId, accepted, result.paths);
	}
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
		ActiveFileDialogs activeFileDialogs;
		const auto applyPointerCursor = [&] {
			if (ui.CurrentPointerCursor() ==
				Scalpel::ApplicationPointerCursor::Arrow) {
				window.SetCursor(Scintilla::Internal::Window::Cursor::arrow);
			} else {
				window.SetCursor(editor.WindowState().cursor);
			}
		};
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

		(void)ui.SynchronizeTabs();
		editor.InvalidateTopChrome();
		editor.InvalidateScrollBars();
		ui.BindPainters();

		while (!quitAccepted && !window.ForceCloseRequested()) {
			(void)window.TakePresentationResults();
			DeliverClipboardResults(window, editor);
			DeliverPrimarySelectionResults(window, editor);
			// Modal cards and an open menu all suppress IME delivery.
			DeliverTextInputBatches(window, editor, ui.ChromeOwnsInput());
			ApplyFileDialogResults(window, ui, activeFileDialogs);
			ApplyShellEffects(ui, window, activeFileDialogs, quitAccepted);
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
				ApplyShellEffects(
					ui, window, activeFileDialogs, quitAccepted);
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
				(void)ui.SynchronizeTabs(true);
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
					if (!pointerResult.consumed) {
						editor.HandlePointerInput(*pointer);
					}
					ApplyShellEffects(
						ui, window, activeFileDialogs, quitAccepted);
					synchronizeScrollBarInteraction();
					continue;
				}
				if (const auto *keyboard =
					std::get_if<Scalpel::KeyboardInput>(&input)) {
					// Modal cards, menu, shortcuts, tab cycling, and editor
					// delivery are decided inside ApplicationUi.
					(void)ui.HandleKeyboard(*keyboard);
				}
				ApplyShellEffects(
					ui, window, activeFileDialogs, quitAccepted);
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
			if (ui.SynchronizeTabs()) {
				editor.InvalidateTopChrome();
			}
			SynchronizeTextInput(editor, window);
			// Overlay priority, painter bind/unbind, and full-frame invalidation
			// when overlays appear, change, or disappear live in ApplicationUi.
			(void)ui.SynchronizeComposition();
			applyPointerCursor();
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
