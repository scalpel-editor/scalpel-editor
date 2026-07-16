// Scintilla source code edit control
/** @file EditorBasicTypes.h
 ** Project-owned shared position, colour, and platform-state types.
 **
 ** Position and colour aliases used by the named editor API and notifications.
 ** Technology selects a surface/font backend until phase 6 collapses the platform.
 ** Bidirectional stores layout mode; full mixed-direction screen lines are later work.
 **/
// Copyright 1998-2019 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef EDITOR_BASIC_TYPES_H
#define EDITOR_BASIC_TYPES_H

#include <cstdint>

namespace Scintilla {

using Position = intptr_t;
using Line = intptr_t;
using Colour = int;
using ColourAlpha = int;

constexpr Position InvalidPosition = -1;
// UTF-8-only document reports this code page to Lexilla.
constexpr int CpUtf8 = 65001;

enum class Technology {
	Default = 0,
	DirectWrite = 1,
	DirectWriteRetain = 2,
	DirectWriteDC = 3,
	DirectWrite1 = 4,
};

enum class Bidirectional {
	Disabled = 0,
	L2R = 1,
	R2L = 2,
};

// Test if an enum class value has some bit flag(s) of test set.
template <typename T>
constexpr bool FlagSet(T value, T test) {
	return (static_cast<int>(value) & static_cast<int>(test)) != 0;
}

}

#endif
