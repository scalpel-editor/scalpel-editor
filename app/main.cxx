// Production Wayland editor process entry: platform pump and transport.
//
// Construction, Wayland event collection, external-service transport
// (clipboard, primary selection, text input, file dialogs), frame submission,
// and waiting live here. ApplicationEditor retains Scintilla documents and
// paints one surface. DocumentWorkspace owns tabs, paths, file-dialog intents,
// and dirty-close policy. ApplicationUi owns chrome, overlays, input routing,
// composition, shell-effect consumption, close requests, frame-size response,
// dirty-tab sync, interaction cleanup, and exit dismissal. This file maps
// portal request IDs to application dialog identities, delivers platform
// events and unconsumed pointer input through ApplicationUi, applies
// CurrentPointerCursor, drops IME batches while ChromeOwnsInput is true, drains
// TakeShellEffects for portal dialogs and accept-close, submits frames, and
// waits. Open/save policy and prompt transitions live in DocumentWorkspace.
// Required-global loss force-closes without prompting the workspace.

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
#include "ContextMenu.h"
#include "DocumentFile.h"
#include "DocumentWorkspace.h"
#include "DrawSurface.h"
#include "GlContext.h"
#include "RecentFiles.h"
#include "Renderer.h"
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
	Scalpel::ApplicationUi &ui) {
	for (Scalpel::ClipboardResult &result : window.TakeClipboardResults()) {
		ui.HandleClipboardResult(result.request, ApplicationOperation(result.operation),
			ApplicationStatus(result.status), std::move(result.text));
	}
	(void)ui.TakeClipboardResults();
	ui.SetClipboardPasteAvailable(window.ClipboardPasteAvailable());
}

void DispatchClipboardRequests(Scalpel::ApplicationUi &ui,
	Scalpel::WaylandWindow &window) {
	for (Scalpel::ApplicationClipboardRequest &request : ui.TakeClipboardRequests()) {
		if (request.operation == Scalpel::ApplicationClipboardOperation::Copy) {
			window.CopyToClipboard(request.id, std::move(request.text));
		} else {
			window.PasteFromClipboard(request.id);
		}
	}
	DeliverClipboardResults(window, ui);
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
	Scalpel::ApplicationUi &ui) {
	for (Scalpel::WaylandTextInputBatch &batch : window.TakeTextInputBatches()) {
		// ApplicationUi drops batches while a modal or menu owns input and
		// routes the rest to the find field or editor.
		ui.HandleTextInputBatch(ApplicationBatch(std::move(batch)));
	}
}

void SynchronizeTextInput(Scalpel::ApplicationUi &ui,
	Scalpel::WaylandWindow &window) {
	if (std::optional<Scalpel::ApplicationTextInputState> state =
		ui.TakeTextInputState()) {
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

/**
 * Drain ApplicationUi shell effects. Application-side work is already applied;
 * portal dialogs, accept-close, and context-menu popup lifecycle remain host work.
 */
using ActiveFileDialogs =
	std::unordered_map<uint64_t, Scalpel::DocumentDialogId>;

struct ContextMenuPopupHost {
	bool wantsPaint = false;
	Scalpel::ContextMenuPainter painter;
};

void DestroyContextMenuPopupHost(Scalpel::WaylandWindow &window,
	Scintilla::Internal::GlContext &gl, ContextMenuPopupHost &host) {
	gl.DestroyPopupSurface();
	window.DestroyContextMenuPopup();
	host.wantsPaint = false;
}

void ShowContextMenuPopup(Scalpel::ApplicationUi &ui,
	Scalpel::WaylandWindow &window, Scintilla::Internal::GlContext &gl,
	ContextMenuPopupHost &host, const Scalpel::ApplicationShellEffect &effect) {
	try {
		DestroyContextMenuPopupHost(window, gl, host);
		const int width = Scalpel::ContextMenuPreferredWidth();
		const int height = Scalpel::ContextMenuPreferredHeight();
		if (!window.CreateContextMenuPopup(width, height,
			static_cast<int>(effect.anchorX),
			static_cast<int>(effect.anchorY), effect.serial)) {
			std::cerr << "scalpel-editor: context menu popup unavailable\n";
			ui.NotifyContextPopupDone();
			return;
		}
		host.wantsPaint = true;
	} catch (const std::exception &error) {
		std::cerr << "scalpel-editor: context menu popup failed: "
			<< error.what() << '\n';
		DestroyContextMenuPopupHost(window, gl, host);
		ui.NotifyContextPopupDone();
	}
}

void ServiceContextMenuPopup(Scalpel::ApplicationUi &ui,
	Scalpel::WaylandWindow &window, Scintilla::Internal::GlContext &gl,
	ContextMenuPopupHost &host) {
	if (window.ContextPopupLifecycle().NeedsDestroy()) {
		DestroyContextMenuPopupHost(window, gl, host);
		ui.NotifyContextPopupDone();
		return;
	}
	if (!window.ContextPopupSurface()) {
		return;
	}
	// Ack any pending configure pair before attaching a buffer.
	if (!window.ContextPopupLifecycle().CanPaint()) {
		(void)window.AckContextMenuPopupConfigure();
	}
	if (!window.ContextPopupLifecycle().CanPaint() || !host.wantsPaint) {
		return;
	}
	if (!ui.ContextMenuOpen()) {
		DestroyContextMenuPopupHost(window, gl, host);
		return;
	}

	try {
		(void)Scalpel::UpdateContextMenuActionState(ui.ContextModel(),
			ui.Editor());
		const Scalpel::ContextMenuLayout layout =
			Scalpel::LayoutContextMenu(ui.ContextModel());
		const int logicalW = static_cast<int>(layout.panel.Width());
		const int logicalH = static_cast<int>(layout.panel.Height());
		if (logicalW <= 0 || logicalH <= 0) {
			return;
		}
		const Scalpel::WaylandScaleConfiguration &scale =
			window.ScaleConfiguration();
		const int bufferW = std::max(1,
			static_cast<int>(std::lround(
				logicalW * scale.scaleNumerator / 120.0)));
		const int bufferH = std::max(1,
			static_cast<int>(std::lround(
				logicalH * scale.scaleNumerator / 120.0)));

		if (!window.EnsureContextMenuPopupEglWindow(bufferW, bufferH)) {
			throw std::runtime_error("could not create popup EGL window");
		}
		if (!gl.HasPopupSurface()) {
			gl.CreatePopupSurface(window.ContextPopupEglWindow());
		}
		{
			// Short-lived renderer for this popup paint; shares the process GL
			// context while retaining the popup EGL surface as its target.
			Scintilla::Internal::Renderer popupRenderer(gl,
				Scintilla::Internal::GlContext::SurfaceTarget::Popup);
			const Scintilla::Internal::RasterScale rasterScale =
				Scintilla::Internal::RasterScale::FromWaylandNumerator(
					scale.scaleNumerator);
			std::unique_ptr<Scintilla::Internal::DrawSurface> draw =
				Scintilla::Internal::CreateExternalDrawSurface(
					popupRenderer, 0, bufferW, bufferH, logicalW, logicalH,
					rasterScale);
			host.painter.Paint(*draw, layout, ui.ContextModel());

			gl.SwapBuffers();
		}
		// Restore the editor only after popup GL resources are released.
		gl.MakeCurrent(Scintilla::Internal::GlContext::SurfaceTarget::Editor);
		host.wantsPaint = false;
	} catch (const std::exception &error) {
		std::cerr << "scalpel-editor: context menu paint failed: "
			<< error.what() << '\n';
		try {
			gl.MakeCurrent(
				Scintilla::Internal::GlContext::SurfaceTarget::Editor);
		} catch (...) {
		}
		DestroyContextMenuPopupHost(window, gl, host);
		ui.NotifyContextPopupDone();
	}
}

void ApplyShellEffects(Scalpel::ApplicationUi &ui,
	Scalpel::WaylandWindow &window,
	Scintilla::Internal::GlContext &gl,
	ContextMenuPopupHost &contextPopup,
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
		case Scalpel::ApplicationShellEffectKind::ShowContextMenu:
			ShowContextMenuPopup(ui, window, gl, contextPopup, effect);
			break;
		case Scalpel::ApplicationShellEffectKind::CloseContextMenu:
			DestroyContextMenuPopupHost(window, gl, contextPopup);
			break;
		case Scalpel::ApplicationShellEffectKind::InvalidateContextMenu:
			if (window.ContextPopupSurface() && ui.ContextMenuOpen()) {
				contextPopup.wantsPaint = true;
			}
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

}

int main() {
	try {
		Scalpel::WaylandWindow window("scalpel-editor", 800, 600);
		auto glContext = std::make_unique<Scintilla::Internal::GlContext>(
			window.Display(), window.EglWindow());
		// Keep a non-owning pointer so the shell can target the popup surface.
		Scintilla::Internal::GlContext &gl = *glContext;
		Scalpel::ApplicationEditor editor(
			std::move(glContext), window.Width(), window.Height());
		editor.SetFrameBufferSize(
			window.ScaleConfiguration().bufferWidth,
			window.ScaleConfiguration().bufferHeight);
		editor.SetFrameRasterScale(
			Scintilla::Internal::RasterScale::FromWaylandNumerator(
				window.ScaleConfiguration().scaleNumerator));
		constexpr std::string_view initialText =
			"scalpel-editor\n\n"
			"A direct Scintilla editor for Wayland.\n"
			"Ctrl+N new tab, Ctrl+W close tab, Ctrl+Tab cycle tabs.\n"
			"Ctrl+O open, Ctrl+S save, Ctrl+Shift+S save as, Ctrl+Q quit.\n"
			"Right-click or Shift+F10 opens the editor context menu.\n";
		editor.LoadInitialBuffer(initialText);
		// ApplicationUi owns the base menu-plus-tab inset and expands it when
		// the find bar is visible.
		Scalpel::DocumentWorkspace workspace(editor);
		const std::string recentStatePath = Scalpel::RecentFilesStatePath();
		Scalpel::RecentFiles recent =
			Scalpel::LoadRecentFiles(recentStatePath);
		Scalpel::ApplicationUi ui(editor, workspace, recent, recentStatePath);
		ActiveFileDialogs activeFileDialogs;
		ContextMenuPopupHost contextPopup;
		bool quitAccepted = false;
		const auto applyPointerCursor = [&] {
			if (ui.CurrentPointerCursor() ==
				Scalpel::ApplicationPointerCursor::Arrow) {
				window.SetCursor(Scintilla::Internal::Window::Cursor::arrow);
			} else {
				window.SetCursor(editor.WindowState().cursor);
			}
		};
		const auto applyEffects = [&] {
			ApplyShellEffects(ui, window, gl, contextPopup, activeFileDialogs,
				quitAccepted);
		};

		(void)ui.SynchronizeTabs();
		editor.InvalidateTopChrome();
		editor.InvalidateScrollBars();
		ui.BindPainters();

		while (!quitAccepted && !window.ForceCloseRequested()) {
			(void)window.TakePresentationResults();
			DeliverClipboardResults(window, ui);
			DeliverPrimarySelectionResults(window, editor);
			DeliverTextInputBatches(window, ui);
			ApplyFileDialogResults(window, ui, activeFileDialogs);
			applyEffects();
			ServiceContextMenuPopup(ui, window, gl, contextPopup);
			SynchronizeTextInput(ui, window);
			ui.RefreshOpenMenuActionState();
			// Clipboard-offer enablement may queue popup invalidate effects.
			applyEffects();

			if (window.ForceCloseRequested()) {
				ui.PrepareForExit();
				applyEffects();
				break;
			}
			if (window.UserCloseRequested()) {
				ui.RequestClose();
				applyEffects();
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
				editor.SetFrameRasterScale(
					Scintilla::Internal::RasterScale::FromWaylandNumerator(
						scale->scaleNumerator));
				ui.HandleFrameSizeChange();
				applyEffects();
			}

			for (const Scalpel::InputEvent &input : window.TakeInputs()) {
				if (const auto *focus =
					std::get_if<Scalpel::KeyboardFocusInput>(&input)) {
					ui.HandleFocus(focus->focused);
					applyEffects();
					continue;
				}
				if (const auto *pointer =
					std::get_if<Scalpel::PointerInput>(&input)) {
					const Scalpel::ApplicationPointerResult pointerResult =
						ui.HandlePointer(*pointer);
					if (!pointerResult.consumed) {
						editor.HandlePointerInput(*pointer);
					}
					applyEffects();
					continue;
				}
				if (const auto *keyboard =
					std::get_if<Scalpel::KeyboardInput>(&input)) {
					(void)ui.HandleKeyboard(*keyboard);
				}
				applyEffects();
			}

			if (quitAccepted || window.ForceCloseRequested()) {
				ui.PrepareForExit();
				applyEffects();
				break;
			}

			DispatchClipboardRequests(ui, window);
			DispatchPrimarySelectionRequests(editor, window);
			editor.RunPendingWork();
			ui.SynchronizeDirtyTabs();
			SynchronizeTextInput(ui, window);
			(void)ui.SynchronizeComposition();
			applyPointerCursor();
			// Popup paint is independent of the toplevel frame path.
			ServiceContextMenuPopup(ui, window, gl, contextPopup);
			QueueFrameDamage(editor, window);
			if (window.CanSubmitFrame()) {
				// Ensure the editor surface is current before editor frame work.
				gl.MakeCurrent(
					Scintilla::Internal::GlContext::SurfaceTarget::Editor);
				const std::optional<Scalpel::FramePlan> plan = window.BeginFrame(
					editor.BufferAge(),
					editor.BufferAgeSupported(), editor.DamageSwapSupported());
				if (plan) {
					window.PrepareFrame(*plan);
					try {
						ui.BeginFrameLayout();
						const bool presented = editor.PresentFrame(
							EditorDamage(plan->repaintDamage),
							EglDamage(plan->eglDamage), plan->fullSwap);
						ui.EndFrameLayout();
						if (presented) {
							window.SubmitFrame(plan->submission);
						} else {
							window.CancelFrame();
						}
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
		DestroyContextMenuPopupHost(window, gl, contextPopup);
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "scalpel-editor: " << error.what() << '\n';
		return 1;
	}
}
