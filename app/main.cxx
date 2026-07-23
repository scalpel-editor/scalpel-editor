#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "ApplicationEditor.h"
#include "GlContext.h"
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

void DeliverClipboardResults(Scalpel::WaylandWindow &window,
	Scalpel::ApplicationEditor &editor) {
	for (Scalpel::ClipboardResult &result : window.TakeClipboardResults()) {
		editor.HandleClipboardResult(result.request, ApplicationOperation(result.operation),
			ApplicationStatus(result.status), std::move(result.text));
	}
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

}

int main() {
	try {
		Scalpel::WaylandWindow window("scalpel-editor", 800, 600);
		auto glContext = std::make_unique<Scintilla::Internal::GlContext>(
			window.Display(), window.EglWindow());
		Scalpel::ApplicationEditor editor(
			std::move(glContext), window.Width(), window.Height());
		constexpr std::string_view initialText =
			"scalpel-editor\n\n"
			"A direct Scintilla editor for Wayland.\n"
			"The first application frame is rendered in an xdg-toplevel.\n";
		editor.LoadInitialBuffer(initialText);
		while (!window.CloseRequested()) {
			DeliverClipboardResults(window, editor);
			if (const std::optional<Scalpel::WindowSize> resize = window.TakeResize()) {
				editor.Resize(resize->width, resize->height);
			}
			for (const Scalpel::InputEvent &input : window.TakeInputs()) {
				if (const auto *focus = std::get_if<Scalpel::KeyboardFocusInput>(&input)) {
					editor.SetKeyboardFocus(focus->focused);
				} else if (const auto *keyboard = std::get_if<Scalpel::KeyboardInput>(&input)) {
					editor.HandleKeyboardInput(*keyboard);
				} else {
					editor.HandlePointerInput(std::get<Scalpel::PointerInput>(input));
				}
			}
			DispatchClipboardRequests(editor, window);
			editor.RunPendingWork();
			window.SetCursor(editor.WindowState().cursor);
			if (editor.NeedsRedraw()) {
				editor.PresentFrame();
			}
			if (!window.CloseRequested()) {
				window.WaitForEvents(editor.TimeUntilNextWork());
			}
		}
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "scalpel-editor: " << error.what() << '\n';
		return 1;
	}
}
