// Scintilla source code edit control
/** @file EditorCommands.h
 ** Bindable editing actions: the dedicated command type and its dispatcher.
 ** Key maps, keyboard input, and temporary message forwarders use EditorCommand
 ** instead of message numbers for zero-argument editing actions.
 **/
// Copyright 1998-2003 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef EDITORCOMMANDS_H
#define EDITORCOMMANDS_H

#include "ScintillaMessages.h"

namespace Scintilla::Internal {

/**
 * Bindable editor actions (keyboard commands and other zero-argument editing
 * operations). KeyMap stores these; ExecuteCommand runs them. None means unbound.
 *
 * Member names match the former interface entry names. SetZoom as a command
 * resets the zoom level to 0 (the zero-parameter key-binding behaviour of the
 * old message path); the application zoom API with an explicit level is separate.
 */
enum class EditorCommand {
	None = 0,
	LineDown,
	LineDownExtend,
	LineDownRectExtend,
	LineUp,
	LineUpExtend,
	LineUpRectExtend,
	LineScrollDown,
	LineScrollUp,
	ParaDown,
	ParaDownExtend,
	ParaUp,
	ParaUpExtend,
	CharLeft,
	CharLeftExtend,
	CharLeftRectExtend,
	CharRight,
	CharRightExtend,
	CharRightRectExtend,
	WordLeft,
	WordLeftExtend,
	WordRight,
	WordRightExtend,
	WordLeftEnd,
	WordLeftEndExtend,
	WordRightEnd,
	WordRightEndExtend,
	WordPartLeft,
	WordPartLeftExtend,
	WordPartRight,
	WordPartRightExtend,
	Home,
	HomeExtend,
	HomeRectExtend,
	HomeDisplay,
	HomeDisplayExtend,
	HomeWrap,
	HomeWrapExtend,
	VCHome,
	VCHomeExtend,
	VCHomeRectExtend,
	VCHomeDisplay,
	VCHomeDisplayExtend,
	VCHomeWrap,
	VCHomeWrapExtend,
	LineEnd,
	LineEndExtend,
	LineEndRectExtend,
	LineEndDisplay,
	LineEndDisplayExtend,
	LineEndWrap,
	LineEndWrapExtend,
	DocumentStart,
	DocumentStartExtend,
	DocumentEnd,
	DocumentEndExtend,
	PageUp,
	PageUpExtend,
	PageUpRectExtend,
	PageDown,
	PageDownExtend,
	PageDownRectExtend,
	StutteredPageUp,
	StutteredPageUpExtend,
	StutteredPageDown,
	StutteredPageDownExtend,
	ScrollToStart,
	ScrollToEnd,
	EditToggleOvertype,
	Cancel,
	DeleteBack,
	DeleteBackNotLine,
	Tab,
	LineIndent,
	BackTab,
	LineDedent,
	NewLine,
	FormFeed,
	ZoomIn,
	ZoomOut,
	SetZoom,
	DelWordLeft,
	DelWordRight,
	DelWordRightEnd,
	DelLineLeft,
	DelLineRight,
	LineCopy,
	LineCut,
	LineDelete,
	LineTranspose,
	LineReverse,
	LineDuplicate,
	SelectionDuplicate,
	LowerCase,
	UpperCase,
	Cut,
	Copy,
	Paste,
	Clear,
	CopyAllowLine,
	CutAllowLine,
	SelectAll,
	Undo,
	Redo,
	VerticalCentreCaret,
	MoveSelectedLinesUp,
	MoveSelectedLinesDown,
	RotateSelection,
	SwapMainAnchorCaret,
	MultipleSelectAddNext,
	MultipleSelectAddEach,
	LinesJoin,
	LinesSplit,
	SearchAnchor,
	SearchNext,
	SearchPrev,
};

// Temporary conversion while WndProc still accepts Message for command keys.
// Removed with the generated message layer (phase 5).
EditorCommand CommandFromMessage(Scintilla::Message message) noexcept;

}

#endif
