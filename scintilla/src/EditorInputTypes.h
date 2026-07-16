// Scintilla source code edit control
/** @file EditorInputTypes.h
 ** Project-owned input, selection, and command types.
 **
 ** Cursor, IME, caret and visible policy, selection mode, virtual space,
 ** keys and modifiers, character source, completion methods, popup policy,
 ** focus change, modification flags, and UI update flags. Numeric values
 ** match the former generated constants.
 **/
// Copyright 1998-2019 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef EDITOR_INPUT_TYPES_H
#define EDITOR_INPUT_TYPES_H

namespace Scintilla {

enum class CursorShape {
	Normal = -1,
	Arrow = 2,
	Wait = 4,
	ReverseArrow = 7,
};

enum class IMEInteraction {
	Windowed = 0,
	Inline = 1,
};

enum class VisiblePolicy {
	Slop = 0x01,
	Strict = 0x04,
};

enum class CaretPolicy {
	Slop = 0x01,
	Strict = 0x04,
	Jumps = 0x10,
	Even = 0x08,
};

enum class SelectionMode {
	Stream = 0,
	Rectangle = 1,
	Lines = 2,
	Thin = 3,
};

enum class CaretSticky {
	Off = 0,
	On = 1,
	WhiteSpace = 2,
};

enum class CaretStyle {
	Invisible = 0,
	Line = 1,
	Block = 2,
	OverstrikeBar = 0,
	OverstrikeBlock = 0x10,
	Curses = 0x20,
	InsMask = 0xF,
	BlockAfter = 0x100,
};

enum class VirtualSpace {
	None = 0,
	RectangularSelection = 1,
	UserAccessible = 2,
	NoWrapLineStart = 4,
};

enum class MultiPaste {
	Once = 0,
	Each = 1,
};

enum class Keys {
	Down = 300,
	Up = 301,
	Left = 302,
	Right = 303,
	Home = 304,
	End = 305,
	Prior = 306,
	Next = 307,
	Delete = 308,
	Insert = 309,
	Escape = 7,
	Back = 8,
	Tab = 9,
	Return = 13,
	Add = 310,
	Subtract = 311,
	Divide = 312,
	Win = 313,
	RWin = 314,
	Menu = 315,
};

enum class KeyMod {
	Norm = 0,
	Shift = 1,
	Ctrl = 2,
	Alt = 4,
	Super = 8,
	Meta = 16,
};

enum class CharacterSource {
	DirectInput = 0,
	TentativeInput = 1,
	ImeResult = 2,
};

enum class CompletionMethods {
	FillUp = 1,
	DoubleClick = 2,
	Tab = 3,
	Newline = 4,
	Command = 5,
	SingleChoice = 6,
};

enum class PopUp {
	Never = 0,
	All = 1,
	Text = 2,
};

enum class FocusChange {
	Change = 768,
	Setfocus = 512,
	Killfocus = 256,
};

enum class ModificationFlags {
	None = 0x0,
	InsertText = 0x1,
	DeleteText = 0x2,
	ChangeStyle = 0x4,
	ChangeFold = 0x8,
	User = 0x10,
	Undo = 0x20,
	Redo = 0x40,
	MultiStepUndoRedo = 0x80,
	LastStepInUndoRedo = 0x100,
	ChangeMarker = 0x200,
	BeforeInsert = 0x400,
	BeforeDelete = 0x800,
	MultilineUndoRedo = 0x1000,
	StartAction = 0x2000,
	ChangeIndicator = 0x4000,
	ChangeLineState = 0x8000,
	ChangeMargin = 0x10000,
	ChangeAnnotation = 0x20000,
	Container = 0x40000,
	LexerState = 0x80000,
	InsertCheck = 0x100000,
	ChangeTabStops = 0x200000,
	ChangeEOLAnnotation = 0x400000,
	EventMaskAll = 0x7FFFFF,
};

enum class Update {
	None = 0x0,
	Content = 0x1,
	Selection = 0x2,
	VScroll = 0x4,
	HScroll = 0x8,
};

constexpr CaretPolicy operator|(CaretPolicy a, CaretPolicy b) noexcept {
	return static_cast<CaretPolicy>(static_cast<int>(a) | static_cast<int>(b));
}

constexpr CaretStyle operator|(CaretStyle a, CaretStyle b) noexcept {
	return static_cast<CaretStyle>(static_cast<int>(a) | static_cast<int>(b));
}

constexpr CaretStyle operator&(CaretStyle a, CaretStyle b) noexcept {
	return static_cast<CaretStyle>(static_cast<int>(a) & static_cast<int>(b));
}

constexpr ModificationFlags operator|(ModificationFlags a, ModificationFlags b) noexcept {
	return static_cast<ModificationFlags>(static_cast<int>(a) | static_cast<int>(b));
}

constexpr ModificationFlags operator&(ModificationFlags a, ModificationFlags b) noexcept {
	return static_cast<ModificationFlags>(static_cast<int>(a) & static_cast<int>(b));
}

inline ModificationFlags &operator|=(ModificationFlags &self, ModificationFlags a) noexcept {
	self = self | a;
	return self;
}

constexpr Update operator|(Update a, Update b) noexcept {
	return static_cast<Update>(static_cast<int>(a) | static_cast<int>(b));
}

constexpr KeyMod operator|(KeyMod a, KeyMod b) noexcept {
	return static_cast<KeyMod>(static_cast<int>(a) | static_cast<int>(b));
}

constexpr KeyMod operator&(KeyMod a, KeyMod b) noexcept {
	return static_cast<KeyMod>(static_cast<int>(a) & static_cast<int>(b));
}

constexpr KeyMod ModifierFlags(bool shift, bool ctrl, bool alt, bool meta=false, bool super=false) noexcept {
	return
		(shift ? KeyMod::Shift : KeyMod::Norm) |
		(ctrl ? KeyMod::Ctrl : KeyMod::Norm) |
		(alt ? KeyMod::Alt : KeyMod::Norm) |
		(meta ? KeyMod::Meta : KeyMod::Norm) |
		(super ? KeyMod::Super : KeyMod::Norm);
}

}

#endif
