// Scintilla source code edit control
/** @file KeyMap.h
 ** Defines a mapping between keystrokes and commands.
 **/
// Copyright 1998-2001 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef KEYMAP_H
#define KEYMAP_H

#include "EditorCommands.h"

namespace Scintilla::Internal {

/**
 */
class KeyModifiers {
public:
	Scintilla::Keys key;
	Scintilla::KeyMod modifiers;
	KeyModifiers() noexcept : key{}, modifiers(KeyMod::Norm) {
	};
	KeyModifiers(Scintilla::Keys key_, Scintilla::KeyMod modifiers_) noexcept : key(key_), modifiers(modifiers_) {
	}
	bool operator<(const KeyModifiers &other) const noexcept {
		if (key == other.key)
			return modifiers < other.modifiers;
		else
			return key < other.key;
	}
};

/**
 */
class KeyToCommand {
public:
	Scintilla::Keys key;
	Scintilla::KeyMod modifiers;
	EditorCommand msg;
};

/**
 */
class KeyMap {
	std::map<KeyModifiers, EditorCommand> kmap;
	static const KeyToCommand MapDefault[];

public:
	KeyMap();
	void Clear() noexcept;
	void AssignCmdKey(Scintilla::Keys key, Scintilla::KeyMod modifiers, EditorCommand command);
	EditorCommand Find(Scintilla::Keys key, Scintilla::KeyMod modifiers) const;	// None returned on failure
	const std::map<KeyModifiers, EditorCommand> &GetKeyMap() const noexcept;
};

}

#endif
