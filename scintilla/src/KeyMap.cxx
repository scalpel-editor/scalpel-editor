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

#include "ScintillaTypes.h"

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

#if PLAT_GTK_MACOSX
#define OS_X_KEYS 1
#else
#define OS_X_KEYS 0
#endif

// Define a modifier that is exactly Ctrl key on all platforms
// Most uses of Ctrl map to Cmd on macOS but some can't so use SCI_[S]CTRL_META
#if OS_X_KEYS
#define SCI_CTRL_META SCI_META
#define SCI_SCTRL_META (SCI_META | SCI_SHIFT)
#else
#define SCI_CTRL_META SCI_CTRL
#define SCI_SCTRL_META (SCI_CTRL | SCI_SHIFT)
#endif

namespace {

constexpr Keys Key(char ch) noexcept {
    return static_cast<Keys>(ch);
}

}

const KeyToCommand KeyMap::MapDefault[] = {

#if OS_X_KEYS
    {Keys::Down,		SCI_CTRL,	EditorCommand::DocumentEnd},
    {Keys::Down,		SCI_CSHIFT,	EditorCommand::DocumentEndExtend},
    {Keys::Up,		    SCI_CTRL,	EditorCommand::DocumentStart},
    {Keys::Up,		    SCI_CSHIFT,	EditorCommand::DocumentStartExtend},
    {Keys::Left,		SCI_CTRL,	EditorCommand::VCHome},
    {Keys::Left,		SCI_CSHIFT,	EditorCommand::VCHomeExtend},
    {Keys::Right,		SCI_CTRL,	EditorCommand::LineEnd},
    {Keys::Right,		SCI_CSHIFT,	EditorCommand::LineEndExtend},
#endif

    {Keys::Down,		SCI_NORM,	EditorCommand::LineDown},
    {Keys::Down,		SCI_SHIFT,	EditorCommand::LineDownExtend},
    {Keys::Down,		SCI_CTRL_META,	EditorCommand::LineScrollDown},
    {Keys::Down,		SCI_ASHIFT,	EditorCommand::LineDownRectExtend},
    {Keys::Up,		    SCI_NORM,	EditorCommand::LineUp},
    {Keys::Up,			SCI_SHIFT,	EditorCommand::LineUpExtend},
    {Keys::Up,			SCI_CTRL_META,	EditorCommand::LineScrollUp},
    {Keys::Up,		    SCI_ASHIFT,	EditorCommand::LineUpRectExtend},
    {Key('['),			SCI_CTRL,	EditorCommand::ParaUp},
    {Key('['),			SCI_CSHIFT,	EditorCommand::ParaUpExtend},
    {Key(']'),			SCI_CTRL,	EditorCommand::ParaDown},
    {Key(']'),			SCI_CSHIFT,	EditorCommand::ParaDownExtend},
    {Keys::Left,		SCI_NORM,	EditorCommand::CharLeft},
    {Keys::Left,		SCI_SHIFT,	EditorCommand::CharLeftExtend},
    {Keys::Left,		SCI_CTRL_META,	EditorCommand::WordLeft},
    {Keys::Left,		SCI_SCTRL_META,	EditorCommand::WordLeftExtend},
    {Keys::Left,		SCI_ASHIFT,	EditorCommand::CharLeftRectExtend},
    {Keys::Right,		SCI_NORM,	EditorCommand::CharRight},
    {Keys::Right,		SCI_SHIFT,	EditorCommand::CharRightExtend},
    {Keys::Right,		SCI_CTRL_META,	EditorCommand::WordRight},
    {Keys::Right,		SCI_SCTRL_META,	EditorCommand::WordRightExtend},
    {Keys::Right,		SCI_ASHIFT,	EditorCommand::CharRightRectExtend},
    {Key('/'),		    SCI_CTRL,	EditorCommand::WordPartLeft},
    {Key('/'),		    SCI_CSHIFT,	EditorCommand::WordPartLeftExtend},
    {Key('\\'),		    SCI_CTRL,	EditorCommand::WordPartRight},
    {Key('\\'),		    SCI_CSHIFT,	EditorCommand::WordPartRightExtend},
    {Keys::Home,		SCI_NORM,	EditorCommand::VCHome},
    {Keys::Home, 		SCI_SHIFT, 	EditorCommand::VCHomeExtend},
    {Keys::Home, 		SCI_CTRL, 	EditorCommand::DocumentStart},
    {Keys::Home, 		SCI_CSHIFT, EditorCommand::DocumentStartExtend},
    {Keys::Home, 		SCI_ALT, 	EditorCommand::HomeDisplay},
    {Keys::Home,		SCI_ASHIFT,	EditorCommand::VCHomeRectExtend},
    {Keys::End,	 	    SCI_NORM,	EditorCommand::LineEnd},
    {Keys::End,	 	    SCI_SHIFT, 	EditorCommand::LineEndExtend},
    {Keys::End, 		SCI_CTRL, 	EditorCommand::DocumentEnd},
    {Keys::End, 		SCI_CSHIFT, EditorCommand::DocumentEndExtend},
    {Keys::End, 		SCI_ALT, 	EditorCommand::LineEndDisplay},
    {Keys::End,		    SCI_ASHIFT,	EditorCommand::LineEndRectExtend},
    {Keys::Prior,		SCI_NORM,	EditorCommand::PageUp},
    {Keys::Prior,		SCI_SHIFT, 	EditorCommand::PageUpExtend},
    {Keys::Prior,		SCI_ASHIFT,	EditorCommand::PageUpRectExtend},
    {Keys::Next, 		SCI_NORM, 	EditorCommand::PageDown},
    {Keys::Next, 		SCI_SHIFT, 	EditorCommand::PageDownExtend},
    {Keys::Next,		SCI_ASHIFT,	EditorCommand::PageDownRectExtend},
    {Keys::Delete,      SCI_NORM,	EditorCommand::Clear},
    {Keys::Delete, 	    SCI_SHIFT,	EditorCommand::Cut},
    {Keys::Delete, 	    SCI_CTRL,	EditorCommand::DelWordRight},
    {Keys::Delete,	    SCI_CSHIFT,	EditorCommand::DelLineRight},
    {Keys::Insert, 		SCI_NORM,	EditorCommand::EditToggleOvertype},
    {Keys::Insert, 		SCI_SHIFT,	EditorCommand::Paste},
    {Keys::Insert, 		SCI_CTRL,	EditorCommand::Copy},
    {Keys::Escape,  	SCI_NORM,	EditorCommand::Cancel},
    {Keys::Back,		SCI_NORM, 	EditorCommand::DeleteBack},
    {Keys::Back,		SCI_SHIFT, 	EditorCommand::DeleteBack},
    {Keys::Back,		SCI_CTRL, 	EditorCommand::DelWordLeft},
    {Keys::Back, 		SCI_ALT,	EditorCommand::Undo},
    {Keys::Back,		SCI_CSHIFT,	EditorCommand::DelLineLeft},
    {Key('Z'), 			SCI_CTRL,	EditorCommand::Undo},
#if OS_X_KEYS
    {Key('Z'), 			SCI_CSHIFT,	EditorCommand::Redo},
#else
    {Key('Y'), 			SCI_CTRL,	EditorCommand::Redo},
#endif
    {Key('X'), 			SCI_CTRL,	EditorCommand::Cut},
    {Key('C'), 			SCI_CTRL,	EditorCommand::Copy},
    {Key('V'), 			SCI_CTRL,	EditorCommand::Paste},
    {Key('A'), 			SCI_CTRL,	EditorCommand::SelectAll},
    {Keys::Tab,		    SCI_NORM,	EditorCommand::Tab},
    {Keys::Tab,		    SCI_SHIFT,	EditorCommand::BackTab},
    {Keys::Return, 	    SCI_NORM,	EditorCommand::NewLine},
    {Keys::Return, 	    SCI_SHIFT,	EditorCommand::NewLine},
    {Keys::Add, 		SCI_CTRL,	EditorCommand::ZoomIn},
    {Keys::Subtract,	SCI_CTRL,	EditorCommand::ZoomOut},
    {Keys::Divide,	    SCI_CTRL,	EditorCommand::SetZoom},
    {Key('L'), 			SCI_CTRL,	EditorCommand::LineCut},
    {Key('L'), 			SCI_CSHIFT,	EditorCommand::LineDelete},
    {Key('T'), 			SCI_CSHIFT,	EditorCommand::LineCopy},
    {Key('T'), 			SCI_CTRL,	EditorCommand::LineTranspose},
    {Key('D'), 			SCI_CTRL,	EditorCommand::SelectionDuplicate},
    {Key('U'), 			SCI_CTRL,	EditorCommand::LowerCase},
    {Key('U'), 			SCI_CSHIFT,	EditorCommand::UpperCase},
    {Key(0),SCI_NORM,EditorCommand::None},
};

