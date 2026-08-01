#include "ApplicationAction.h"

#include "ApplicationEditor.h"
#include "DocumentWorkspace.h"

namespace Scalpel {

namespace {

constexpr Scintilla::Keys Key(char letter) noexcept {
	return static_cast<Scintilla::Keys>(letter);
}

constexpr Scintilla::KeyMod Ctrl = Scintilla::KeyMod::Ctrl;
constexpr Scintilla::KeyMod CtrlShift =
	Scintilla::KeyMod::Ctrl | Scintilla::KeyMod::Shift;

// File, Edit, then Font; separatorBefore groups related rows for later menu paint.
// Font rows are menu-only: empty shortcut label and key zero (never match input).
constexpr ApplicationActionInfo kActions[] = {
	{ApplicationAction::NewTab, ApplicationMenu::File, "New Tab", "Ctrl+N",
		Key('N'), Ctrl, false},
	{ApplicationAction::Open, ApplicationMenu::File, "Open\u2026", "Ctrl+O",
		Key('O'), Ctrl, false},
	{ApplicationAction::Save, ApplicationMenu::File, "Save", "Ctrl+S",
		Key('S'), Ctrl, true},
	{ApplicationAction::SaveAs, ApplicationMenu::File, "Save As\u2026",
		"Ctrl+Shift+S", Key('S'), CtrlShift, false},
	{ApplicationAction::CloseTab, ApplicationMenu::File, "Close Tab", "Ctrl+W",
		Key('W'), Ctrl, true},
	{ApplicationAction::Quit, ApplicationMenu::File, "Quit", "Ctrl+Q",
		Key('Q'), Ctrl, true},
	{ApplicationAction::Undo, ApplicationMenu::Edit, "Undo", "Ctrl+Z",
		Key('Z'), Ctrl, false},
	{ApplicationAction::Redo, ApplicationMenu::Edit, "Redo", "Ctrl+Y",
		Key('Y'), Ctrl, false},
	{ApplicationAction::Cut, ApplicationMenu::Edit, "Cut", "Ctrl+X",
		Key('X'), Ctrl, true},
	{ApplicationAction::Copy, ApplicationMenu::Edit, "Copy", "Ctrl+C",
		Key('C'), Ctrl, false},
	{ApplicationAction::Paste, ApplicationMenu::Edit, "Paste", "Ctrl+V",
		Key('V'), Ctrl, false},
	{ApplicationAction::SelectAll, ApplicationMenu::Edit, "Select All", "Ctrl+A",
		Key('A'), Ctrl, true},
	{ApplicationAction::Find, ApplicationMenu::Edit, "Find", "Ctrl+F",
		Key('F'), Ctrl, false},
	{ApplicationAction::ConvertLineEndingsToLf, ApplicationMenu::Edit,
		"Convert Line Endings to LF", "", static_cast<Scintilla::Keys>(0),
		Scintilla::KeyMod::Norm, true},
	{ApplicationAction::ConvertLineEndingsToCrLf, ApplicationMenu::Edit,
		"Convert Line Endings to CRLF", "", static_cast<Scintilla::Keys>(0),
		Scintilla::KeyMod::Norm, false},
	{ApplicationAction::FontMonospace, ApplicationMenu::Font, "Monospace", "",
		static_cast<Scintilla::Keys>(0), Scintilla::KeyMod::Norm, false},
	{ApplicationAction::FontSerif, ApplicationMenu::Font, "Serif", "",
		static_cast<Scintilla::Keys>(0), Scintilla::KeyMod::Norm, false},
	{ApplicationAction::FontSans, ApplicationMenu::Font, "Sans", "",
		static_cast<Scintilla::Keys>(0), Scintilla::KeyMod::Norm, false},
	{ApplicationAction::FontSystem, ApplicationMenu::Font, "System", "",
		static_cast<Scintilla::Keys>(0), Scintilla::KeyMod::Norm, false},
};

}

const ApplicationActionInfo *ApplicationActionTable() noexcept {
	return kActions;
}

std::size_t ApplicationActionCount() noexcept {
	return sizeof(kActions) / sizeof(kActions[0]);
}

const ApplicationActionInfo &InfoFor(ApplicationAction action) noexcept {
	for (const ApplicationActionInfo &info : kActions) {
		if (info.action == action) {
			return info;
		}
	}
	return kActions[0];
}

std::optional<ApplicationAction> MatchApplicationAction(
	const KeyboardInput &input) noexcept {
	if (!input.pressed) {
		return std::nullopt;
	}
	// Key zero is the unbound sentinel used by menu-only rows and by ordinary
	// text-input events. Matching it would steal typing into the first such row.
	if (input.key == static_cast<Scintilla::Keys>(0)) {
		return std::nullopt;
	}
	for (const ApplicationActionInfo &info : kActions) {
		if (info.key == static_cast<Scintilla::Keys>(0)) {
			continue;
		}
		if (input.key == info.key && input.modifiers == info.modifiers) {
			return info.action;
		}
	}
	return std::nullopt;
}

bool ApplicationActionEnabled(ApplicationAction action,
	ApplicationEditor &editor) {
	switch (action) {
	case ApplicationAction::NewTab:
	case ApplicationAction::Open:
	case ApplicationAction::Save:
	case ApplicationAction::SaveAs:
	case ApplicationAction::CloseTab:
	case ApplicationAction::Quit:
	case ApplicationAction::Find:
	case ApplicationAction::ConvertLineEndingsToLf:
	case ApplicationAction::ConvertLineEndingsToCrLf:
	case ApplicationAction::FontMonospace:
	case ApplicationAction::FontSerif:
	case ApplicationAction::FontSans:
	case ApplicationAction::FontSystem:
		return true;
	case ApplicationAction::Undo:
		return editor.CanUndoEdit();
	case ApplicationAction::Redo:
		return editor.CanRedoEdit();
	case ApplicationAction::Cut:
		return editor.CanCut();
	case ApplicationAction::Copy:
		return editor.CanCopy();
	case ApplicationAction::Paste:
		return editor.ClipboardPasteAvailable();
	case ApplicationAction::SelectAll:
		return editor.CanSelectAll();
	}
	return false;
}

void DispatchApplicationAction(ApplicationAction action,
	DocumentWorkspace &workspace, ApplicationEditor &editor) {
	// File and Font actions always dispatch; the workspace no-ops during a prompt.
	// Edit actions refuse when disabled so menus and shortcuts share one gate.
	switch (action) {
	case ApplicationAction::NewTab:
	case ApplicationAction::Open:
	case ApplicationAction::Save:
	case ApplicationAction::SaveAs:
	case ApplicationAction::CloseTab:
	case ApplicationAction::Quit:
	case ApplicationAction::Find:
	case ApplicationAction::FontMonospace:
	case ApplicationAction::FontSerif:
	case ApplicationAction::FontSans:
	case ApplicationAction::FontSystem:
		break;
	case ApplicationAction::Undo:
	case ApplicationAction::Redo:
	case ApplicationAction::Cut:
	case ApplicationAction::Copy:
	case ApplicationAction::Paste:
	case ApplicationAction::SelectAll:
	case ApplicationAction::ConvertLineEndingsToLf:
	case ApplicationAction::ConvertLineEndingsToCrLf:
		if (!ApplicationActionEnabled(action, editor)) {
			return;
		}
		break;
	}

	switch (action) {
	case ApplicationAction::NewTab:
		workspace.NewTab();
		break;
	case ApplicationAction::Open:
		workspace.RequestOpen();
		break;
	case ApplicationAction::Save:
		workspace.RequestSave();
		break;
	case ApplicationAction::SaveAs:
		workspace.RequestSaveAs();
		break;
	case ApplicationAction::CloseTab:
		workspace.CloseTab(workspace.ActiveTab());
		break;
	case ApplicationAction::Quit:
		workspace.RequestClose();
		break;
	case ApplicationAction::Undo:
		editor.RequestUndo();
		break;
	case ApplicationAction::Redo:
		editor.RequestRedo();
		break;
	case ApplicationAction::Cut:
		editor.RequestCut();
		break;
	case ApplicationAction::Copy:
		editor.RequestClipboardCopy();
		break;
	case ApplicationAction::Paste:
		editor.RequestClipboardPaste();
		break;
	case ApplicationAction::SelectAll:
		editor.RequestSelectAll();
		break;
	case ApplicationAction::Find:
		// UI-local: ApplicationUi opens the find bar before this dispatcher.
		break;
	case ApplicationAction::ConvertLineEndingsToLf:
		editor.ConvertLineEndings(Scintilla::EndOfLine::Lf);
		break;
	case ApplicationAction::ConvertLineEndingsToCrLf:
		editor.ConvertLineEndings(Scintilla::EndOfLine::CrLf);
		break;
	case ApplicationAction::FontMonospace:
		editor.SetEditorFont(EditorFont::Monospace);
		break;
	case ApplicationAction::FontSerif:
		editor.SetEditorFont(EditorFont::Serif);
		break;
	case ApplicationAction::FontSans:
		editor.SetEditorFont(EditorFont::Sans);
		break;
	case ApplicationAction::FontSystem:
		editor.SetEditorFont(EditorFont::System);
		break;
	}
}

}
