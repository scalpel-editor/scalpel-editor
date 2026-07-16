// Scintilla source code edit control
/** @file EditorNotifications.h
 ** Project-owned notification kinds and payload for the host callback.
 **
 ** The editor reports events through NotifyParent(NotificationData). Kinds are
 ** only those the retained core emits. There is no Windows-style header, no
 ** message-number packing fields (message / wParam / lParam), and no
 ** MacroRecord kind: typed recording uses RecordedAction only.
 **
 ** Numeric values match the former generated SCN_* constants so residual
 ** client headers stay coherent until they are deleted.
 **/
// Copyright 1998-2011 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef EDITOR_NOTIFICATIONS_H
#define EDITOR_NOTIFICATIONS_H

#include "EditorBasicTypes.h"
#include "EditorInputTypes.h"
#include "EditorLayoutTypes.h"

namespace Scintilla {

// Event kinds delivered to the host. Only kinds the production core emits.
enum class Notification {
	StyleNeeded = 2000,
	CharAdded = 2001,
	SavePointReached = 2002,
	SavePointLeft = 2003,
	ModifyAttemptRO = 2004,
	DoubleClick = 2006,
	UpdateUI = 2007,
	Modified = 2008,
	MarginClick = 2010,
	NeedShown = 2011,
	Painted = 2013,
	UserListSelection = 2014,
	DwellStart = 2016,
	DwellEnd = 2017,
	Zoom = 2018,
	HotSpotClick = 2019,
	HotSpotDoubleClick = 2020,
	CallTipClick = 2021,
	AutoCSelection = 2022,
	IndicatorClick = 2023,
	IndicatorRelease = 2024,
	AutoCCancelled = 2025,
	AutoCCharDeleted = 2026,
	HotSpotReleaseClick = 2027,
	FocusIn = 2028,
	FocusOut = 2029,
	AutoCCompleted = 2030,
	MarginRightClick = 2031,
	AutoCSelectionChange = 2032,
};

// One host notification. Fields are filled only when relevant to `code`;
// unused fields remain zero or null. text points at editor-owned memory for
// the duration of NotifyParent and must not be retained without copying.
struct NotificationData {
	Notification code{};
	Position position = 0;
	int ch = 0;
	KeyMod modifiers{};
	ModificationFlags modificationType{};
	const char *text = nullptr;
	Position length = 0;
	Position linesAdded = 0;
	Position line = 0;
	FoldLevel foldLevelNow{};
	FoldLevel foldLevelPrev{};
	int margin = 0;
	int listType = 0;
	int x = 0;
	int y = 0;
	int token = 0;
	Position annotationLinesAdded = 0;
	Update updated{};
	CompletionMethods listCompletionMethod{};
	CharacterSource characterSource{};
};

}

#endif
