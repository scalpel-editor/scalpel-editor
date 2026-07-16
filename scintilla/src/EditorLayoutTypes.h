// Scintilla source code edit control
/** @file EditorLayoutTypes.h
 ** Project-owned wrapping, folding, and autocomplete-option types.
 **
 ** Wrap modes and flags, fold levels and actions, and autocomplete list
 ** options. Numeric values match the former generated constants. Fold helpers
 ** decode FoldLevel bit fields for fold margin and display text work.
 **/
// Copyright 1998-2019 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef EDITOR_LAYOUT_TYPES_H
#define EDITOR_LAYOUT_TYPES_H

namespace Scintilla {

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

enum class AutoCompleteOption {
	Normal = 0,
	FixedSize = 1,
	SelectFirstItem = 2,
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

}

#endif
