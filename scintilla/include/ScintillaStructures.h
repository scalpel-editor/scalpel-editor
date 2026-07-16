// Scintilla source code edit control
/** @file ScintillaStructures.h
 ** Structures used to communicate with Scintilla.
 ** The same structures are defined for C in Scintilla.h.
 ** Uses definitions from ScintillaTypes.h.
 **/
// Copyright 2021 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef SCINTILLASTRUCTURES_H
#define SCINTILLASTRUCTURES_H

namespace Scintilla {

using PositionCR = long;

struct CharacterRange {
	PositionCR cpMin;
	PositionCR cpMax;
};

struct CharacterRangeFull {
	Position cpMin;
	Position cpMax;
};

struct TextRange {
	CharacterRange chrg;
	char *lpstrText;
};

struct TextRangeFull {
	CharacterRangeFull chrg;
	char *lpstrText;
};

struct TextToFind {
	CharacterRange chrg;
	const char *lpstrText;
	CharacterRange chrgText;
};

struct TextToFindFull {
	CharacterRangeFull chrg;
	const char *lpstrText;
	CharacterRangeFull chrgText;
};

using SurfaceID = void *;

struct Rectangle {
	int left;
	int top;
	int right;
	int bottom;
};

/* This structure is used in printing. Not needed by most client code. */

struct RangeToFormat {
	SurfaceID hdc;
	SurfaceID hdcTarget;
	Rectangle rc;
	Rectangle rcPage;
	CharacterRange chrg;
};

struct RangeToFormatFull {
	SurfaceID hdc;
	SurfaceID hdcTarget;
	Rectangle rc;
	Rectangle rcPage;
	CharacterRangeFull chrg;
};

// NotifyHeader and NotificationData moved to EditorNotifications.h (phase 5 step 7).

}

#endif
