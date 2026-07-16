// Scintilla source code edit control
/** @file EditorDocumentTypes.h
 ** Project-owned document, history, and search types.
 **
 ** White space, end-of-line, find options, undo/change-history options, document
 ** construction flags, idle styling, and lexer property typing. Numeric values
 ** match the former generated constants so retained behaviour stays stable.
 **/
// Copyright 1998-2019 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef EDITOR_DOCUMENT_TYPES_H
#define EDITOR_DOCUMENT_TYPES_H

namespace Scintilla {

enum class WhiteSpace {
	Invisible = 0,
	VisibleAlways = 1,
	VisibleAfterIndent = 2,
	VisibleOnlyInIndent = 3,
};

enum class TabDrawMode {
	LongArrow = 0,
	StrikeOut = 1,
	ControlChar = 2,
};

enum class EndOfLine {
	CrLf = 0,
	Cr = 1,
	Lf = 2,
};

enum class FindOption {
	None = 0x0,
	WholeWord = 0x2,
	MatchCase = 0x4,
	WordStart = 0x00100000,
	RegExp = 0x00200000,
	Posix = 0x00400000,
	Cxx11RegEx = 0x00800000,
};

enum class ChangeHistoryOption {
	Disabled = 0,
	Enabled = 1,
	Markers = 2,
	Indicators = 4,
};

enum class UndoSelectionHistoryOption {
	Disabled = 0,
	Enabled = 1,
	Scroll = 2,
};

enum class UndoFlags {
	None = 0,
	MayCoalesce = 1,
};

enum class LineEndType {
	Default = 0,
	Unicode = 1,
};

enum class LineCharacterIndexType {
	None = 0,
	Utf32 = 1,
	Utf16 = 2,
};

enum class Status {
	Ok = 0,
	Failure = 1,
	BadAlloc = 2,
	OutsideDocument = 3,
	WarnStart = 1000,
	RegEx = 1001,
};

enum class DocumentOption {
	Default = 0,
	StylesNone = 0x1,
	TextLarge = 0x100,
};

enum class IdleStyling {
	None = 0,
	ToVisible = 1,
	AfterVisible = 2,
	All = 3,
};

enum class TypeProperty {
	Boolean = 0,
	Integer = 1,
	String = 2,
};

constexpr FindOption operator|(FindOption a, FindOption b) noexcept {
	return static_cast<FindOption>(static_cast<int>(a) | static_cast<int>(b));
}

inline FindOption &operator|=(FindOption &self, FindOption a) noexcept {
	self = self | a;
	return self;
}

constexpr DocumentOption operator|(DocumentOption a, DocumentOption b) noexcept {
	return static_cast<DocumentOption>(static_cast<int>(a) | static_cast<int>(b));
}

constexpr LineEndType operator&(LineEndType a, LineEndType b) noexcept {
	return static_cast<LineEndType>(static_cast<int>(a) & static_cast<int>(b));
}

constexpr LineCharacterIndexType operator|(LineCharacterIndexType a, LineCharacterIndexType b) noexcept {
	return static_cast<LineCharacterIndexType>(static_cast<int>(a) | static_cast<int>(b));
}

}

#endif
