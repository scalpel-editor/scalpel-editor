// Scintilla source code edit control
/** @file ScintillaTypes.h
 ** Temporary umbrella for project-owned editor types (phase 5 step 9).
 **
 ** Retained enums and constants live in Editor*Types.h headers under src/.
 ** This file re-includes them so existing includes keep working until every
 ** consumer points at the concern headers and this file is deleted.
 **/
// Copyright 1998-2019 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef SCINTILLATYPES_H
#define SCINTILLATYPES_H

#include "EditorBasicTypes.h"
#include "EditorDocumentTypes.h"
#include "EditorStyleTypes.h"

namespace Scintilla {

// Enumerations still pending move into concern headers (step 9 batches).

enum class IMEInteraction {
	Windowed = 0,
	Inline = 1,
};

enum class CursorShape {
	Normal = -1,
	Arrow = 2,
	Wait = 4,
	ReverseArrow = 7,
};

enum class AutoCompleteOption {
	Normal = 0,
	FixedSize = 1,
	SelectFirstItem = 2,
};

enum class FoldLevel {
	None = 0x0,
	Base = 0x400,
	WhiteFlag = 0x1000,
	HeaderFlag = 0x2000,
	NumberMask = 0x0FFF,
};

enum class FoldDisplayTextStyle {
	Hidden = 0,
	Standard = 1,
	Boxed = 2,
};

enum class FoldAction {
	Contract = 0,
	Expand = 1,
	Toggle = 2,
	ContractEveryLevel = 4,
};

enum class AutomaticFold {
	None = 0x0000,
	Show = 0x0001,
	Click = 0x0002,
	Change = 0x0004,
};

enum class FoldFlag {
	None = 0x0000,
	LineBeforeExpanded = 0x0002,
	LineBeforeContracted = 0x0004,
	LineAfterExpanded = 0x0008,
	LineAfterContracted = 0x0010,
	LevelNumbers = 0x0040,
	LineState = 0x0080,
};

enum class Wrap {
	None = 0,
	Word = 1,
	Char = 2,
	WhiteSpace = 3,
};

enum class WrapVisualFlag {
	None = 0x0000,
	End = 0x0001,
	Start = 0x0002,
	Margin = 0x0004,
};

enum class WrapVisualLocation {
	Default = 0x0000,
	EndByText = 0x0001,
	StartByText = 0x0002,
};

enum class WrapIndentMode {
	Fixed = 0,
	Same = 1,
	Indent = 2,
	DeepIndent = 3,
};

enum class MultiPaste {
	Once = 0,
	Each = 1,
};

enum class Accessibility {
	Disabled = 0,
	Enabled = 1,
};

enum class PopUp {
	Never = 0,
	All = 1,
	Text = 2,
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

enum class CaseInsensitiveBehaviour {
	RespectCase = 0,
	IgnoreCase = 1,
};

enum class MultiAutoComplete {
	Once = 0,
	Each = 1,
};

enum class Ordering {
	PreSorted = 0,
	PerformSort = 1,
	Custom = 2,
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

enum class FocusChange {
	Change = 768,
	Setfocus = 512,
	Killfocus = 256,
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

enum class CompletionMethods {
	FillUp = 1,
	DoubleClick = 2,
	Tab = 3,
	Newline = 4,
	Command = 5,
	SingleChoice = 6,
};

enum class CharacterSource {
	DirectInput = 0,
	TentativeInput = 1,
	ImeResult = 2,
};

enum class ScaleTechnique {
	Default = 0,
	PixelAligned = 1,
};

// Temporary message-shaped pointer aliases; not a long-term project API.
// Removed when the last test consumer is converted (step 9).
using uptr_t = uintptr_t;
using sptr_t = intptr_t;



constexpr FoldLevel operator&(FoldLevel lhs, FoldLevel rhs) noexcept {
	return static_cast<FoldLevel>(static_cast<int>(lhs) & static_cast<int>(rhs));
}

constexpr FoldLevel LevelNumberPart(FoldLevel level) noexcept {
	return level & FoldLevel::NumberMask;
}

constexpr int LevelNumber(FoldLevel level) noexcept {
	return static_cast<int>(LevelNumberPart(level));
}

constexpr bool LevelIsHeader(FoldLevel level) noexcept {
	return (level & FoldLevel::HeaderFlag) == FoldLevel::HeaderFlag;
}

constexpr bool LevelIsWhitespace(FoldLevel level) noexcept {
	return (level & FoldLevel::WhiteFlag) == FoldLevel::WhiteFlag;
}

constexpr FoldFlag operator|(FoldFlag a, FoldFlag b) noexcept {
	return static_cast<FoldFlag>(static_cast<int>(a) | static_cast<int>(b));
}

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
