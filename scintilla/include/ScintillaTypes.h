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
#include "EditorInputTypes.h"

namespace Scintilla {

// Enumerations still pending move into concern headers (step 9 batches).

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

enum class Accessibility {
	Disabled = 0,
	Enabled = 1,
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

}

#endif
