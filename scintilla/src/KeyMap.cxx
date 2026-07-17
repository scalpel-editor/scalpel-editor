// Scintilla source code edit control
/** @file KeyMap.cxx
 ** Defines a mapping between keystrokes and commands.
 **/
// Copyright 1998-2003 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cstdlib>
#include <cstdint>

#include <stdexcept>
#include <string_view>
#include <vector>
#include <map>
#include <optional>
#include <memory>

#include "EditorBasicTypes.h"
#include "EditorDocumentTypes.h"
#include "EditorStyleTypes.h"
#include "EditorInputTypes.h"
#include "EditorLayoutTypes.h"

#include "Debugging.h"
#include "Geometry.h"
#include "Platform.h"

#include "KeyMap.h"

using namespace Scintilla;
using namespace Scintilla::Internal;

KeyMap::KeyMap() {
	for (int i = 0; static_cast<int>(MapDefault[i].key); i++) {
		AssignCmdKey(MapDefault[i].key,
			MapDefault[i].modifiers,
			MapDefault[i].msg);
	}
}

void KeyMap::Clear() noexcept {
	kmap.clear();
}

void KeyMap::AssignCmdKey(Keys key, KeyMod modifiers, EditorCommand command) {
	kmap[KeyModifiers(key, modifiers)] = command;
}

EditorCommand KeyMap::Find(Keys key, KeyMod modifiers) const {
	std::map<KeyModifiers, EditorCommand>::const_iterator it = kmap.find(KeyModifiers(key, modifiers));
	return (it == kmap.end()) ? EditorCommand::None : it->second;
}

const std::map<KeyModifiers, EditorCommand> &KeyMap::GetKeyMap() const noexcept {
	return kmap;
}

namespace {

constexpr Keys Key(char ch) noexcept {
	return static_cast<Keys>(ch);
}

// Ctrl is the primary modifier for key bindings on this Wayland host.
constexpr KeyMod CtrlOrMeta = KeyMod::Ctrl;
constexpr KeyMod CtrlOrMetaShift = KeyMod::Ctrl | KeyMod::Shift;

constexpr KeyMod CtrlShift = KeyMod::Ctrl | KeyMod::Shift;
constexpr KeyMod AltShift = KeyMod::Alt | KeyMod::Shift;

}

const KeyToCommand KeyMap::MapDefault[] = {

	{Keys::Down,		KeyMod::Norm,	EditorCommand::LineDown},
	{Keys::Down,		KeyMod::Shift,	EditorCommand::LineDownExtend},
	{Keys::Down,		CtrlOrMeta,	EditorCommand::LineScrollDown},
	{Keys::Down,		AltShift,	EditorCommand::LineDownRectExtend},
	{Keys::Up,		KeyMod::Norm,	EditorCommand::LineUp},
	{Keys::Up,		KeyMod::Shift,	EditorCommand::LineUpExtend},
	{Keys::Up,		CtrlOrMeta,	EditorCommand::LineScrollUp},
	{Keys::Up,		AltShift,	EditorCommand::LineUpRectExtend},
	{Key('['),		KeyMod::Ctrl,	EditorCommand::ParaUp},
	{Key('['),		CtrlShift,	EditorCommand::ParaUpExtend},
	{Key(']'),		KeyMod::Ctrl,	EditorCommand::ParaDown},
	{Key(']'),		CtrlShift,	EditorCommand::ParaDownExtend},
	{Keys::Left,		KeyMod::Norm,	EditorCommand::CharLeft},
	{Keys::Left,		KeyMod::Shift,	EditorCommand::CharLeftExtend},
	{Keys::Left,		CtrlOrMeta,	EditorCommand::WordLeft},
	{Keys::Left,		CtrlOrMetaShift,	EditorCommand::WordLeftExtend},
	{Keys::Left,		AltShift,	EditorCommand::CharLeftRectExtend},
	{Keys::Right,		KeyMod::Norm,	EditorCommand::CharRight},
	{Keys::Right,		KeyMod::Shift,	EditorCommand::CharRightExtend},
	{Keys::Right,		CtrlOrMeta,	EditorCommand::WordRight},
	{Keys::Right,		CtrlOrMetaShift,	EditorCommand::WordRightExtend},
	{Keys::Right,		AltShift,	EditorCommand::CharRightRectExtend},
	{Key('/'),		KeyMod::Ctrl,	EditorCommand::WordPartLeft},
	{Key('/'),		CtrlShift,	EditorCommand::WordPartLeftExtend},
	{Key('\\'),		KeyMod::Ctrl,	EditorCommand::WordPartRight},
	{Key('\\'),		CtrlShift,	EditorCommand::WordPartRightExtend},
	{Keys::Home,		KeyMod::Norm,	EditorCommand::VCHome},
	{Keys::Home,		KeyMod::Shift,	EditorCommand::VCHomeExtend},
	{Keys::Home,		KeyMod::Ctrl,	EditorCommand::DocumentStart},
	{Keys::Home,		CtrlShift,	EditorCommand::DocumentStartExtend},
	{Keys::Home,		KeyMod::Alt,	EditorCommand::HomeDisplay},
	{Keys::Home,		AltShift,	EditorCommand::VCHomeRectExtend},
	{Keys::End,		KeyMod::Norm,	EditorCommand::LineEnd},
	{Keys::End,		KeyMod::Shift,	EditorCommand::LineEndExtend},
	{Keys::End,		KeyMod::Ctrl,	EditorCommand::DocumentEnd},
	{Keys::End,		CtrlShift,	EditorCommand::DocumentEndExtend},
	{Keys::End,		KeyMod::Alt,	EditorCommand::LineEndDisplay},
	{Keys::End,		AltShift,	EditorCommand::LineEndRectExtend},
	{Keys::Prior,		KeyMod::Norm,	EditorCommand::PageUp},
	{Keys::Prior,		KeyMod::Shift,	EditorCommand::PageUpExtend},
	{Keys::Prior,		AltShift,	EditorCommand::PageUpRectExtend},
	{Keys::Next,		KeyMod::Norm,	EditorCommand::PageDown},
	{Keys::Next,		KeyMod::Shift,	EditorCommand::PageDownExtend},
	{Keys::Next,		AltShift,	EditorCommand::PageDownRectExtend},
	{Keys::Delete,		KeyMod::Norm,	EditorCommand::Clear},
	{Keys::Delete,		KeyMod::Shift,	EditorCommand::Cut},
	{Keys::Delete,		KeyMod::Ctrl,	EditorCommand::DelWordRight},
	{Keys::Delete,		CtrlShift,	EditorCommand::DelLineRight},
	{Keys::Insert,		KeyMod::Norm,	EditorCommand::EditToggleOvertype},
	{Keys::Insert,		KeyMod::Shift,	EditorCommand::Paste},
	{Keys::Insert,		KeyMod::Ctrl,	EditorCommand::Copy},
	{Keys::Escape,		KeyMod::Norm,	EditorCommand::Cancel},
	{Keys::Back,		KeyMod::Norm,	EditorCommand::DeleteBack},
	{Keys::Back,		KeyMod::Shift,	EditorCommand::DeleteBack},
	{Keys::Back,		KeyMod::Ctrl,	EditorCommand::DelWordLeft},
	{Keys::Back,		KeyMod::Alt,	EditorCommand::Undo},
	{Keys::Back,		CtrlShift,	EditorCommand::DelLineLeft},
	{Key('Z'),		KeyMod::Ctrl,	EditorCommand::Undo},
	{Key('Y'),		KeyMod::Ctrl,	EditorCommand::Redo},
	{Key('X'),		KeyMod::Ctrl,	EditorCommand::Cut},
	{Key('C'),		KeyMod::Ctrl,	EditorCommand::Copy},
	{Key('V'),		KeyMod::Ctrl,	EditorCommand::Paste},
	{Key('A'),		KeyMod::Ctrl,	EditorCommand::SelectAll},
	{Keys::Tab,		KeyMod::Norm,	EditorCommand::Tab},
	{Keys::Tab,		KeyMod::Shift,	EditorCommand::BackTab},
	{Keys::Return,		KeyMod::Norm,	EditorCommand::NewLine},
	{Keys::Return,		KeyMod::Shift,	EditorCommand::NewLine},
	{Keys::Add,		KeyMod::Ctrl,	EditorCommand::ZoomIn},
	{Keys::Subtract,	KeyMod::Ctrl,	EditorCommand::ZoomOut},
	{Keys::Divide,		KeyMod::Ctrl,	EditorCommand::SetZoom},
	{Key('L'),		KeyMod::Ctrl,	EditorCommand::LineCut},
	{Key('L'),		CtrlShift,	EditorCommand::LineDelete},
	{Key('T'),		CtrlShift,	EditorCommand::LineCopy},
	{Key('T'),		KeyMod::Ctrl,	EditorCommand::LineTranspose},
	{Key('D'),		KeyMod::Ctrl,	EditorCommand::SelectionDuplicate},
	{Key('U'),		KeyMod::Ctrl,	EditorCommand::LowerCase},
	{Key('U'),		CtrlShift,	EditorCommand::UpperCase},
	{Key(0),		KeyMod::Norm,	EditorCommand::None},
};
