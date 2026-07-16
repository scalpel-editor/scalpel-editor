// Scintilla source code edit control
/** @file EditorStyleTypes.h
 ** Project-owned styling, marker, decoration, and view-draw types.
 **
 ** Styles, fonts, elements, indicators, markers, margins, annotations, edges,
 ** draw phases, representation appearance, indent view, print colour mode,
 ** and related masks. Numeric values match the former generated constants.
 **/
// Copyright 1998-2019 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef EDITOR_STYLE_TYPES_H
#define EDITOR_STYLE_TYPES_H

namespace Scintilla {

enum class Alpha {
	Transparent = 0,
	Opaque = 255,
	NoAlpha = 256,
};

enum class MarkerSymbol {
	Circle = 0,
	RoundRect = 1,
	Arrow = 2,
	SmallRect = 3,
	ShortArrow = 4,
	Empty = 5,
	ArrowDown = 6,
	Minus = 7,
	Plus = 8,
	VLine = 9,
	LCorner = 10,
	TCorner = 11,
	BoxPlus = 12,
	BoxPlusConnected = 13,
	BoxMinus = 14,
	BoxMinusConnected = 15,
	LCornerCurve = 16,
	TCornerCurve = 17,
	CirclePlus = 18,
	CirclePlusConnected = 19,
	CircleMinus = 20,
	CircleMinusConnected = 21,
	Background = 22,
	DotDotDot = 23,
	Arrows = 24,
	Pixmap = 25,
	FullRect = 26,
	LeftRect = 27,
	Available = 28,
	Underline = 29,
	RgbaImage = 30,
	Bookmark = 31,
	VerticalBookmark = 32,
	Bar = 33,
	Character = 10000,
};

enum class MarkerOutline {
	HistoryRevertedToOrigin = 21,
	HistorySaved = 22,
	HistoryModified = 23,
	HistoryRevertedToModified = 24,
	FolderEnd = 25,
	FolderOpenMid = 26,
	FolderMidTail = 27,
	FolderTail = 28,
	FolderSub = 29,
	Folder = 30,
	FolderOpen = 31,
};

enum class MarginType {
	Symbol = 0,
	Number = 1,
	Back = 2,
	Fore = 3,
	Text = 4,
	RText = 5,
	Colour = 6,
};

enum class StylesCommon {
	Default = 32,
	LineNumber = 33,
	BraceLight = 34,
	BraceBad = 35,
	ControlChar = 36,
	IndentGuide = 37,
	CallTip = 38,
	FoldDisplayText = 39,
	LastPredefined = 39,
	Max = 255,
};

enum class CharacterSet {
	Ansi = 0,
	Default = 1,
	Baltic = 186,
	ChineseBig5 = 136,
	EastEurope = 238,
	GB2312 = 134,
	Greek = 161,
	Hangul = 129,
	Mac = 77,
	Oem = 255,
	Russian = 204,
	Oem866 = 866,
	Cyrillic = 1251,
	ShiftJis = 128,
	Symbol = 2,
	Turkish = 162,
	Johab = 130,
	Hebrew = 177,
	Arabic = 178,
	Vietnamese = 163,
	Thai = 222,
	Iso8859_15 = 1000,
};

enum class CaseVisible {
	Mixed = 0,
	Upper = 1,
	Lower = 2,
	Camel = 3,
};

enum class FontWeight {
	Normal = 400,
	SemiBold = 600,
	Bold = 700,
};

enum class FontStretch {
	UltraCondensed = 1,
	ExtraCondensed = 2,
	Condensed = 3,
	SemiCondensed = 4,
	Normal = 5,
	SemiExpanded = 6,
	Expanded = 7,
	ExtraExpanded = 8,
	UltraExpanded = 9,
};

enum class Element {
	List = 0,
	ListBack = 1,
	ListSelected = 2,
	ListSelectedBack = 3,
	SelectionText = 10,
	SelectionBack = 11,
	SelectionAdditionalText = 12,
	SelectionAdditionalBack = 13,
	SelectionSecondaryText = 14,
	SelectionSecondaryBack = 15,
	SelectionInactiveText = 16,
	SelectionInactiveBack = 17,
	SelectionInactiveAdditionalText = 18,
	SelectionInactiveAdditionalBack = 19,
	Caret = 40,
	CaretAdditional = 41,
	CaretLineBack = 50,
	WhiteSpace = 60,
	WhiteSpaceBack = 61,
	HotSpotActive = 70,
	HotSpotActiveBack = 71,
	FoldLine = 80,
	HiddenLine = 81,
};

enum class Layer {
	Base = 0,
	UnderText = 1,
	OverText = 2,
};

enum class IndicatorStyle {
	Plain = 0,
	Squiggle = 1,
	TT = 2,
	Diagonal = 3,
	Strike = 4,
	Hidden = 5,
	Box = 6,
	RoundBox = 7,
	StraightBox = 8,
	Dash = 9,
	Dots = 10,
	SquiggleLow = 11,
	DotBox = 12,
	SquigglePixmap = 13,
	CompositionThick = 14,
	CompositionThin = 15,
	FullBox = 16,
	TextFore = 17,
	Point = 18,
	PointCharacter = 19,
	Gradient = 20,
	GradientCentre = 21,
	PointTop = 22,
};

enum class IndicatorNumbers {
	Container = 8,
	Ime = 32,
	ImeMax = 35,
	HistoryRevertedToOriginInsertion = 36,
	HistoryRevertedToOriginDeletion = 37,
	HistorySavedInsertion = 38,
	HistorySavedDeletion = 39,
	HistoryModifiedInsertion = 40,
	HistoryModifiedDeletion = 41,
	HistoryRevertedToModifiedInsertion = 42,
	HistoryRevertedToModifiedDeletion = 43,
	Max = 43,
};

enum class IndicValue {
	Bit = 0x1000000,
	Mask = 0xFFFFFF,
};

enum class IndicFlag {
	None = 0,
	ValueFore = 1,
};

enum class IndentView {
	None = 0,
	Real = 1,
	LookForward = 2,
	LookBoth = 3,
};

enum class PrintOption {
	Normal = 0,
	InvertLight = 1,
	BlackOnWhite = 2,
	ColourOnWhite = 3,
	ColourOnWhiteDefaultBG = 4,
	ScreenColours = 5,
};

enum class FontQuality {
	QualityMask = 0xF,
	QualityDefault = 0,
	QualityNonAntialiased = 1,
	QualityAntialiased = 2,
	QualityLcdOptimized = 3,
};

enum class EdgeVisualStyle {
	None = 0,
	Line = 1,
	Background = 2,
	MultiLine = 3,
};

enum class MarginOption {
	None = 0,
	SubLineSelect = 1,
};

enum class AnnotationVisible {
	Hidden = 0,
	Standard = 1,
	Boxed = 2,
	Indented = 3,
};

enum class RepresentationAppearance {
	Plain = 0,
	Blob = 1,
	Colour = 0x10,
};

enum class EOLAnnotationVisible {
	Hidden = 0x0,
	Standard = 0x1,
	Boxed = 0x2,
	Stadium = 0x100,
	FlatCircle = 0x101,
	AngleCircle = 0x102,
	CircleFlat = 0x110,
	Flats = 0x111,
	AngleFlat = 0x112,
	CircleAngle = 0x120,
	FlatAngle = 0x121,
	Angles = 0x122,
};

enum class Supports {
	LineDrawsFinal = 0,
	PixelDivisions = 1,
	FractionalStrokeWidth = 2,
	TranslucentStroke = 3,
	PixelModification = 4,
	ThreadSafeMeasureWidths = 5,
};

enum class LineCache {
	None = 0,
	Caret = 1,
	Page = 2,
	Document = 3,
};

enum class PhasesDraw {
	One = 0,
	Two = 1,
	Multiple = 2,
};

constexpr int MarkerMax = 31;
constexpr int MaskHistory = 0x01E00000;
constexpr int MaskFolders = 0xFE000000;
constexpr int MaxMargin = 4;
constexpr int FontSizeMultiplier = 100;
constexpr int TimeForever = 10000000;
constexpr int KeywordsetMax = 8;
constexpr int IndicatorMax = static_cast<int>(IndicatorNumbers::Max);

inline int operator<<(int i, MarkerOutline marker) noexcept {
	return i << static_cast<int>(marker);
}

constexpr FontQuality operator&(FontQuality a, FontQuality b) noexcept {
	return static_cast<FontQuality>(static_cast<int>(a) & static_cast<int>(b));
}

constexpr RepresentationAppearance operator|(RepresentationAppearance a, RepresentationAppearance b) noexcept {
	return static_cast<RepresentationAppearance>(static_cast<int>(a) | static_cast<int>(b));
}

}

#endif
