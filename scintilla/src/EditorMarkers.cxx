// Scintilla source code edit control
/** @file EditorMarkers.cxx
 ** Marker definitions (symbols, colours, images, layers) and per-line marker placement.
 **
 ** There are 32 marker numbers, 0..MarkerMax (31). Any combination may be present on a document line. Markers draw in a symbol margin when that margin's mask includes their bit; otherwise they change the text-line background (or draw as content marks such as Background / Underline).
 **
 ** Numbers 0..20 are free for the application. 21..24 are change-history markers when that option is on, otherwise free. 25..31 are the folding set (FolderEnd .. FolderOpen) selected by MaskFolders on a margin. Applications that do not fold may use all 32 numbers.
 **
 ** Each number has one symbol (default Circle), opaque or translucent fore/back colours, optional fold-selected back colour, stroke width, and a content-area layer/alpha for marks drawn over text. Pixmaps and RGBA images replace the built-in symbol. Symbols draw in number order except Bar, which draws first so it sits under other marks across multi-line ranges. Markers track the start of their line; when a line is deleted its marks are OR-combined into the next line.
 **
 ** MarkerAdd returns a handle for later line lookup or deletion. MarkerGet reports a 32-bit bitset of marks on a line (including change-history bits when that option is enabled). MarkerNext / MarkerPrevious search by mask. Image width, height, and scale for MarkerDefineRGBAImage are the shared RGBAImageSet* state in EditorStyling.cxx.
 **/
// Copyright 1998-2011 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cmath>

#include <stdexcept>
#include <utility>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <set>
#include <forward_list>
#include <optional>
#include <algorithm>
#include <iterator>
#include <memory>
#include <chrono>
#include <atomic>
#include <mutex>
#include <thread>
#include <future>

#include "ScintillaTypes.h"
#include "ScintillaStructures.h"
#include "ILoader.h"
#include "ILexer.h"

#include "Debugging.h"
#include "Geometry.h"
#include "Platform.h"

#include "CharacterType.h"
#include "CharacterCategoryMap.h"
#include "Position.h"
#include "UniqueString.h"
#include "SplitVector.h"
#include "Partitioning.h"
#include "RunStyles.h"
#include "ContractionState.h"
#include "CellBuffer.h"
#include "PerLine.h"
#include "KeyMap.h"
#include "Indicator.h"
#include "LineMarker.h"
#include "Style.h"
#include "ViewStyle.h"
#include "CharClassify.h"
#include "Decoration.h"
#include "CaseFolder.h"
#include "CaseConvert.h"
#include "Document.h"
#include "UniConversion.h"
#include "Selection.h"
#include "PositionCache.h"
#include "EditModel.h"
#include "MarginView.h"
#include "EditView.h"
#include "Editor.h"
#include "ElapsedPeriod.h"

using namespace Scintilla;
using namespace Scintilla::Internal;

namespace {

// Matches the historical uptr_t message check (wParam <= MarkerMax): size_t keeps values above 32 bits from wrapping into a valid index.
bool ValidMarkerNumber(size_t markerNumber) noexcept {
	return markerNumber <= static_cast<size_t>(MarkerMax);
}

}

// Line that currently holds the marker with this handle, or -1 if the handle is unknown.
Sci::Line Editor::MarkerLineFromHandle(int markerHandle) const noexcept {
	return pdoc->LineFromHandle(markerHandle);
}

// Remove the marker identified by handle wherever it sits.
void Editor::MarkerDeleteHandle(int markerHandle) {
	pdoc->DeleteMarkFromHandle(markerHandle);
}

// Nth marker handle on line, or -1 when which is past the markers on that line.
int Editor::MarkerHandleFromLine(Sci::Line line, int which) const noexcept {
	return pdoc->MarkerHandleFromLine(line, which);
}

// Nth marker number on line, or -1 when which is past the markers on that line.
int Editor::MarkerNumberFromLine(Sci::Line line, int which) const noexcept {
	return pdoc->MarkerNumberFromLine(line, which);
}

// Associate markerNumber (0..31) with a built-in symbol or SC_MARK_CHARACTER + code point. Out-of-range numbers skip the assignment but still refresh style data (historical message-path behaviour).
void Editor::MarkerDefine(size_t markerNumber, MarkerSymbol markerSymbol) {
	if (ValidMarkerNumber(markerNumber)) {
		vs.markers[markerNumber].markType = markerSymbol;
		vs.CalcLargestMarkerHeight();
	}
	InvalidateStyleData();
	RedrawSelMargin();
}

// Symbol currently defined for markerNumber, or 0 when the number is out of range.
MarkerSymbol Editor::MarkerSymbolDefined(size_t markerNumber) const noexcept {
	if (ValidMarkerNumber(markerNumber))
		return vs.markers[markerNumber].markType;
	return static_cast<MarkerSymbol>(0);
}

// Opaque foreground colour of the marker symbol (RGB).
void Editor::MarkerSetFore(size_t markerNumber, int rgb) {
	if (ValidMarkerNumber(markerNumber))
		vs.markers[markerNumber].fore = ColourRGBA::FromIpRGB(rgb);
	InvalidateStyleData();
	RedrawSelMargin();
}

// Opaque background colour of the marker symbol (RGB).
void Editor::MarkerSetBack(size_t markerNumber, int rgb) {
	if (ValidMarkerNumber(markerNumber))
		vs.markers[markerNumber].back = ColourRGBA::FromIpRGB(rgb);
	InvalidateStyleData();
	RedrawSelMargin();
}

// Background used when the marker's folding block is selected (default red). Requires MarkerEnableHighlight.
void Editor::MarkerSetBackSelected(size_t markerNumber, int rgb) {
	if (ValidMarkerNumber(markerNumber))
		vs.markers[markerNumber].backSelected = ColourRGBA::FromIpRGB(rgb);
	InvalidateStyleData();
	RedrawSelMargin();
}

// Foreground with alpha (colouralpha packed as int).
void Editor::MarkerSetForeTranslucent(size_t markerNumber, int colourAlpha) {
	if (ValidMarkerNumber(markerNumber))
		vs.markers[markerNumber].fore = ColourRGBA(colourAlpha);
	InvalidateStyleData();
	RedrawSelMargin();
}

// Background with alpha.
void Editor::MarkerSetBackTranslucent(size_t markerNumber, int colourAlpha) {
	if (ValidMarkerNumber(markerNumber))
		vs.markers[markerNumber].back = ColourRGBA(colourAlpha);
	InvalidateStyleData();
	RedrawSelMargin();
}

// Selected-block background with alpha.
void Editor::MarkerSetBackSelectedTranslucent(size_t markerNumber, int colourAlpha) {
	if (ValidMarkerNumber(markerNumber))
		vs.markers[markerNumber].backSelected = ColourRGBA(colourAlpha);
	InvalidateStyleData();
	RedrawSelMargin();
}

// Stroke width for outline-drawn symbols, in hundredths of a pixel (default 100 = one pixel).
void Editor::MarkerSetStrokeWidth(size_t markerNumber, int hundredths) {
	if (ValidMarkerNumber(markerNumber))
		vs.markers[markerNumber].strokeWidth = static_cast<XYPOSITION>(hundredths) / 100.0f;
	InvalidateStyleData();
	RedrawSelMargin();
}

// When true, fold markers in the selected folding block use the selected background colour.
void Editor::MarkerEnableHighlight(bool enabled) {
	marginView.highlightDelimiter.isEnabled = enabled;
	RedrawSelMargin();
}

// Content-area translucency for Background / Underline marks. Alpha::NoAlpha forces opaque base-layer drawing; any other alpha sets OverText layer.
void Editor::MarkerSetAlpha(size_t markerNumber, Alpha alpha) {
	if (ValidMarkerNumber(markerNumber)) {
		if (alpha == Alpha::NoAlpha) {
			SetAppearance(vs.markers[markerNumber].alpha, Alpha::Opaque);
			SetAppearance(vs.markers[markerNumber].layer, Layer::Base);
		} else {
			SetAppearance(vs.markers[markerNumber].alpha, alpha);
			SetAppearance(vs.markers[markerNumber].layer, Layer::OverText);
		}
	}
}

// Layer for content-area markers (Base, UnderText, OverText). Margin drawing uses translucent colours instead.
void Editor::MarkerSetLayer(size_t markerNumber, Layer layer) {
	if (ValidMarkerNumber(markerNumber)) {
		SetAppearance(vs.markers[markerNumber].layer, layer);
	}
}

// Content-area layer for markerNumber, or Base when out of range.
Layer Editor::MarkerGetLayer(size_t markerNumber) const noexcept {
	if (ValidMarkerNumber(markerNumber))
		return vs.markers[markerNumber].layer;
	return static_cast<Layer>(0);
}

// Place markerNumber on line. Returns a handle for later lookup/delete, or -1 on bad line / allocation failure. Does not check for duplicates.
int Editor::MarkerAdd(Sci::Line line, int markerNumber) {
	return pdoc->AddMark(line, markerNumber);
}

// Set several markers on line in one call. markerSet uses the same one-bit-per-marker layout as MarkerGet. Zero is a no-op.
void Editor::MarkerAddSet(Sci::Line line, int markerSet) {
	if (markerSet != 0)
		pdoc->AddMarkSet(line, markerSet);
}

// Remove one occurrence of markerNumber on line. markerNumber -1 removes every marker on that line.
void Editor::MarkerDelete(Sci::Line line, int markerNumber) {
	pdoc->DeleteMark(line, markerNumber);
}

// Remove markerNumber from every line. markerNumber -1 clears all markers in the document.
void Editor::MarkerDeleteAll(int markerNumber) {
	pdoc->DeleteAllMarks(markerNumber);
}

// 32-bit bitset of markers on line (bit 0 = marker 0). Includes change-history bits when that option is enabled.
int Editor::MarkerGet(Sci::Line line) const {
	return GetMark(line);
}

// First line at or after lineStart that has any marker in markerMask, or -1 if none.
Sci::Line Editor::MarkerNext(Sci::Line lineStart, int markerMask) const noexcept {
	return pdoc->MarkerNext(lineStart, markerMask);
}

// First line at or before lineStart that has any marker in markerMask (via GetMark, so change-history bits count), or -1 if none.
Sci::Line Editor::MarkerPrevious(Sci::Line lineStart, int markerMask) const {
	for (Sci::Line iLine = lineStart; iLine >= 0; iLine--) {
		if ((GetMark(iLine) & markerMask) != 0)
			return iLine;
	}
	return -1;
}

// Define markerNumber from an XPM pixmap string (text form starting with "/* XPM */", or the internal lines form). Sets symbol Pixmap.
void Editor::MarkerDefinePixmap(size_t markerNumber, const char *pixmap) {
	if (ValidMarkerNumber(markerNumber)) {
		vs.markers[markerNumber].SetXPM(pixmap);
		vs.CalcLargestMarkerHeight();
	}
	InvalidateStyleData();
	RedrawSelMargin();
}

// Define markerNumber from RGBA pixels. Width, height, and scale come from the last RGBAImageSetWidth / Height / Scale calls. Sets symbol RgbaImage.
void Editor::MarkerDefineRGBAImage(size_t markerNumber, const unsigned char *pixels) {
	if (ValidMarkerNumber(markerNumber)) {
		vs.markers[markerNumber].SetRGBAImage(sizeRGBAImage, scaleRGBAImage / 100.0f, pixels);
		vs.CalcLargestMarkerHeight();
	}
	InvalidateStyleData();
	RedrawSelMargin();
}
