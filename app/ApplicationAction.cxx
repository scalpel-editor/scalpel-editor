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

// File then Edit; separatorBefore groups related rows for later menu paint.
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
	for (const ApplicationActionInfo &info : kActions) {
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
	// File actions always dispatch; the workspace no-ops during a prompt.
	// Edit actions refuse when disabled so menus and shortcuts share one gate.
	switch (action) {
	case ApplicationAction::NewTab:
	case ApplicationAction::Open:
	case ApplicationAction::Save:
	case ApplicationAction::SaveAs:
	case ApplicationAction::CloseTab:
	case ApplicationAction::Quit:
		break;
	case ApplicationAction::Undo:
	case ApplicationAction::Redo:
	case ApplicationAction::Cut:
	case ApplicationAction::Copy:
	case ApplicationAction::Paste:
	case ApplicationAction::SelectAll:
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
	}
}

}
