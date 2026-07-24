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
#include "DocumentFile.h"
#include "GlContext.h"
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
	Scalpel::ApplicationEditor &editor, bool promptActive) {
	for (Scalpel::WaylandTextInputBatch &batch : window.TakeTextInputBatches()) {
		if (promptActive) {
			// Modal chrome owns input; drop IME commits while the card is up.
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

void StartOpenDialog(Scalpel::WaylandWindow &window,
	const std::string &documentPath) {
	Scalpel::FileDialogRequest request;
	request.mode = Scalpel::FileDialogMode::Open;
	request.title = "Open File";
	request.currentFolder = Scalpel::DocumentDirectory(documentPath);
	request.filters = TextDialogFilters();
	if (!window.ShowFileDialog(request)) {
		std::cerr << "scalpel-editor: file dialog unavailable\n";
	}
}

void RequestSaveAsDialog(Scalpel::WaylandWindow &window,
	const std::string &documentPath) {
	Scalpel::FileDialogRequest request;
	request.mode = Scalpel::FileDialogMode::Save;
	request.title = "Save File As";
	request.currentFolder = Scalpel::DocumentDirectory(documentPath);
	request.suggestedName = documentPath.empty() ?
		"untitled.txt" :
		Scalpel::DocumentBaseName(documentPath);
	request.filters = TextDialogFilters();
	if (!window.ShowFileDialog(request)) {
		std::cerr << "scalpel-editor: file dialog unavailable\n";
	}
}

bool SaveDocumentToPath(Scalpel::ApplicationEditor &editor,
	const std::string &path) {
	if (!Scalpel::WriteDocumentFile(path, editor.Text())) {
		std::cerr << "scalpel-editor: failed to write " << path << '\n';
		return false;
	}
	editor.MarkSaved();
	return true;
}

void RefreshPromptChrome(Scalpel::ApplicationEditor &editor, int &cardFocus) {
	cardFocus = 0;
	editor.CancelActiveTextInput();
	editor.InvalidateClient();
}

void HandleUnsavedOutcome(Scalpel::UnsavedOutcome outcome,
	Scalpel::WaylandWindow &window,
	Scalpel::ApplicationEditor &editor,
	const std::string &documentPath,
	bool &quitAccepted,
	int &cardFocus) {
	switch (outcome) {
	case Scalpel::UnsavedOutcome::None:
	case Scalpel::UnsavedOutcome::SaveFailed:
		break;
	case Scalpel::UnsavedOutcome::Dismissed:
		editor.InvalidateClient();
		break;
	case Scalpel::UnsavedOutcome::PerformClose:
		quitAccepted = true;
		editor.InvalidateClient();
		break;
	case Scalpel::UnsavedOutcome::PerformOpen:
		// Discard leaves the buffer dirty; clear so ApplyFileDialogResults
		// accepts the chosen path. Save already cleared via MarkSaved.
		editor.MarkSaved();
		editor.InvalidateClient();
		StartOpenDialog(window, documentPath);
		break;
	case Scalpel::UnsavedOutcome::NeedSaveAs:
		RequestSaveAsDialog(window, documentPath);
		// Keep the card visible while the portal runs.
		editor.InvalidateClient();
		break;
	}
	(void)cardFocus;
}

void ApplyUnsavedChoice(Scalpel::UnsavedChoice choice,
	Scalpel::UnsavedChangesPrompt &prompt,
	Scalpel::WaylandWindow &window,
	Scalpel::ApplicationEditor &editor,
	std::string &documentPath,
	bool &quitAccepted,
	int &cardFocus) {
	if (!prompt.Active()) {
		return;
	}
	if (choice == Scalpel::UnsavedChoice::Save && !documentPath.empty()) {
		// Write before clearing the prompt so a failed write keeps the card.
		if (!SaveDocumentToPath(editor, documentPath)) {
			return;
		}
		const Scalpel::UnsavedOutcome outcome =
			prompt.Choose(Scalpel::UnsavedChoice::Save, true);
		HandleUnsavedOutcome(outcome, window, editor, documentPath,
			quitAccepted, cardFocus);
		return;
	}
	const Scalpel::UnsavedOutcome outcome =
		prompt.Choose(choice, !documentPath.empty());
	HandleUnsavedOutcome(outcome, window, editor, documentPath,
		quitAccepted, cardFocus);
}

void ApplyFileDialogResults(Scalpel::WaylandWindow &window,
	Scalpel::ApplicationEditor &editor,
	std::string &documentPath,
	Scalpel::UnsavedChangesPrompt &prompt,
	bool &quitAccepted,
	int &cardFocus) {
	for (const Scalpel::FileDialogResult &result :
		window.TakeFileDialogResults()) {
		const bool awaitingSaveAs = prompt.AwaitingSaveAs();
		if (result.status != Scalpel::FileDialogResultStatus::Accepted ||
			result.paths.empty()) {
			if (awaitingSaveAs &&
				result.mode == Scalpel::FileDialogMode::Save) {
				prompt.NotifySaveIncomplete();
				editor.InvalidateClient();
			}
			continue;
		}
		const std::string &path = result.paths.front();
		if (result.mode == Scalpel::FileDialogMode::Open) {
			if (editor.Modified()) {
				std::cerr << "scalpel-editor: save or discard changes before "
					"opening another file\n";
				continue;
			}
			const std::optional<std::string> text =
				Scalpel::ReadDocumentFile(path);
			if (!text) {
				std::cerr << "scalpel-editor: failed to read " << path << '\n';
				continue;
			}
			editor.LoadInitialBuffer(*text);
			documentPath = path;
		} else if (SaveDocumentToPath(editor, path)) {
			documentPath = path;
			if (awaitingSaveAs) {
				const Scalpel::UnsavedOutcome outcome = prompt.NotifySaved();
				HandleUnsavedOutcome(outcome, window, editor, documentPath,
					quitAccepted, cardFocus);
			}
		} else if (awaitingSaveAs) {
			prompt.NotifySaveIncomplete();
			editor.InvalidateClient();
		}
	}
}

[[nodiscard]] bool IsOpenShortcut(const Scalpel::KeyboardInput &input) {
	return input.pressed &&
		input.key == static_cast<Scintilla::Keys>('O') &&
		input.modifiers == Scintilla::KeyMod::Ctrl;
}

[[nodiscard]] bool IsSaveShortcut(const Scalpel::KeyboardInput &input) {
	return input.pressed &&
		input.key == static_cast<Scintilla::Keys>('S') &&
		input.modifiers == Scintilla::KeyMod::Ctrl;
}

[[nodiscard]] bool IsSaveAsShortcut(const Scalpel::KeyboardInput &input) {
	return input.pressed &&
		input.key == static_cast<Scintilla::Keys>('S') &&
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
	Scalpel::UnsavedChangesPrompt &prompt,
	Scalpel::WaylandWindow &window,
	Scalpel::ApplicationEditor &editor,
	std::string &documentPath,
	bool &quitAccepted,
	int &cardFocus) {
	if (!input.pressed) {
		return true;
	}
	if (input.key == Scintilla::Keys::Escape) {
		ApplyUnsavedChoice(Scalpel::UnsavedChoice::Cancel, prompt, window,
			editor, documentPath, quitAccepted, cardFocus);
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
		ApplyUnsavedChoice(choice, prompt, window, editor, documentPath,
			quitAccepted, cardFocus);
		return true;
	}
	if (IsSaveShortcut(input) || IsLetterKey(input, 'S', 's')) {
		ApplyUnsavedChoice(Scalpel::UnsavedChoice::Save, prompt, window,
			editor, documentPath, quitAccepted, cardFocus);
		return true;
	}
	if (IsLetterKey(input, 'D', 'd')) {
		ApplyUnsavedChoice(Scalpel::UnsavedChoice::Discard, prompt, window,
			editor, documentPath, quitAccepted, cardFocus);
		return true;
	}
	// Ignore other keys while the prompt owns input.
	return true;
}

bool HandlePromptPointer(const Scalpel::PointerInput &input,
	const Scalpel::UnsavedChangesCardLayout &layout,
	std::optional<Scalpel::UnsavedCardHit> &pressHit,
	Scalpel::UnsavedChangesPrompt &prompt,
	Scalpel::WaylandWindow &window,
	Scalpel::ApplicationEditor &editor,
	std::string &documentPath,
	bool &quitAccepted,
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
				ApplyUnsavedChoice(Scalpel::UnsavedChoice::Save, prompt,
					window, editor, documentPath, quitAccepted, cardFocus);
			} else if (hit == Scalpel::UnsavedCardHit::Discard) {
				ApplyUnsavedChoice(Scalpel::UnsavedChoice::Discard, prompt,
					window, editor, documentPath, quitAccepted, cardFocus);
			} else if (hit == Scalpel::UnsavedCardHit::Cancel) {
				ApplyUnsavedChoice(Scalpel::UnsavedChoice::Cancel, prompt,
					window, editor, documentPath, quitAccepted, cardFocus);
			}
		}
		pressHit.reset();
		return true;
	}
	// Scrim clicks, move, and scroll do not cancel or edit.
	return true;
}

void BeginUnsavedPrompt(Scalpel::UnsavedPending pending,
	Scalpel::UnsavedChangesPrompt &prompt,
	Scalpel::ApplicationEditor &editor,
	int &cardFocus,
	std::optional<Scalpel::UnsavedCardHit> &pressHit) {
	if (!prompt.TryBegin(pending)) {
		return;
	}
	pressHit.reset();
	RefreshPromptChrome(editor, cardFocus);
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
			"Ctrl+O open, Ctrl+S save, Ctrl+Shift+S save as.\n";
		editor.LoadInitialBuffer(initialText);
		std::string documentPath;
		Scalpel::UnsavedChangesPrompt unsavedPrompt;
		Scalpel::UnsavedChangesCardPainter cardPainter;
		int cardFocus = 0;
		bool quitAccepted = false;
		bool cardOverlayBound = false;
		std::optional<Scalpel::UnsavedCardHit> promptPressHit;

		const auto paintUnsavedCard =
			[&](Scintilla::Internal::Surface &surface, int width, int height) {
				const Scalpel::UnsavedChangesCardLayout layout =
					Scalpel::LayoutUnsavedChangesCard(width, height);
				const std::string subtitle = documentPath.empty() ?
					"Untitled" :
					Scalpel::DocumentBaseName(documentPath);
				cardPainter.Paint(surface, layout, "Save changes?", subtitle,
					cardFocus);
			};

		while (!quitAccepted && !window.ForceCloseRequested()) {
			(void)window.TakePresentationResults();
			DeliverClipboardResults(window, editor);
			DeliverPrimarySelectionResults(window, editor);
			DeliverTextInputBatches(window, editor, unsavedPrompt.Active());
			ApplyFileDialogResults(window, editor, documentPath, unsavedPrompt,
				quitAccepted, cardFocus);
			SynchronizeTextInput(editor, window);

			if (window.ForceCloseRequested()) {
				break;
			}
			if (window.CloseRequested()) {
				if (unsavedPrompt.Active()) {
					// Pending action already owns the close/open decision.
					window.ClearCloseRequest();
				} else if (editor.Modified()) {
					BeginUnsavedPrompt(Scalpel::UnsavedPending::Close,
						unsavedPrompt, editor, cardFocus, promptPressHit);
					window.ClearCloseRequest();
				} else {
					quitAccepted = true;
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
				if (unsavedPrompt.Active()) {
					editor.InvalidateClient();
				}
			}

			const Scalpel::UnsavedChangesCardLayout promptLayout =
				Scalpel::LayoutUnsavedChangesCard(
					editor.FrameWidth(), editor.FrameHeight());

			for (const Scalpel::InputEvent &input : window.TakeInputs()) {
				if (const auto *focus =
					std::get_if<Scalpel::KeyboardFocusInput>(&input)) {
					editor.SetKeyboardFocus(focus->focused);
					continue;
				}
				if (unsavedPrompt.Active()) {
					if (const auto *keyboard =
						std::get_if<Scalpel::KeyboardInput>(&input)) {
						HandlePromptKeyboard(*keyboard, unsavedPrompt, window,
							editor, documentPath, quitAccepted, cardFocus);
					} else if (const auto *pointer =
						std::get_if<Scalpel::PointerInput>(&input)) {
						HandlePromptPointer(*pointer, promptLayout,
							promptPressHit, unsavedPrompt, window, editor,
							documentPath, quitAccepted, cardFocus);
					}
					continue;
				}
				if (const auto *keyboard =
					std::get_if<Scalpel::KeyboardInput>(&input)) {
					if (IsOpenShortcut(*keyboard)) {
						if (editor.Modified()) {
							BeginUnsavedPrompt(Scalpel::UnsavedPending::Open,
								unsavedPrompt, editor, cardFocus,
								promptPressHit);
						} else {
							StartOpenDialog(window, documentPath);
						}
					} else if (IsSaveAsShortcut(*keyboard)) {
						RequestSaveAsDialog(window, documentPath);
					} else if (IsSaveShortcut(*keyboard)) {
						if (documentPath.empty()) {
							RequestSaveAsDialog(window, documentPath);
						} else {
							(void)SaveDocumentToPath(editor, documentPath);
						}
					} else {
						editor.HandleKeyboardInput(*keyboard);
					}
				} else {
					editor.HandlePointerInput(
						std::get<Scalpel::PointerInput>(input));
				}
			}

			if (quitAccepted || window.ForceCloseRequested()) {
				break;
			}

			DispatchClipboardRequests(editor, window);
			DispatchPrimarySelectionRequests(editor, window);
			editor.RunPendingWork();
			SynchronizeTextInput(editor, window);
			if (unsavedPrompt.Active()) {
				window.SetCursor(Scintilla::Internal::Window::Cursor::arrow);
				// Bind only while active so PresentFrame full-swaps only then.
				if (!cardOverlayBound) {
					editor.SetOverlayPainter(paintUnsavedCard);
					cardOverlayBound = true;
				}
				// Full client damage so Wayland/EGL damage matches the scrim.
				editor.InvalidateClient();
			} else {
				window.SetCursor(editor.WindowState().cursor);
				if (cardOverlayBound) {
					editor.SetOverlayPainter(nullptr);
					cardOverlayBound = false;
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
						editor.PresentFrame(EditorDamage(plan->repaintDamage),
							EglDamage(plan->eglDamage), plan->fullSwap);
						window.SubmitFrame(plan->submission);
					} catch (...) {
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
