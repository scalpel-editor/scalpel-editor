// Scintilla source code edit control
/** @file EditorPrinting.cxx
 ** Print settings and FormatRange for drawing a document range onto a surface
 ** (including a printer device context).
 **
 ** Print magnification scales the view styles for output. Colour mode chooses
 ** how colours are rendered for monochrome or colour devices. Print wrap mode
 ** is Word or None for the print layout (independent of the screen wrap mode).
 ** FormatRange measures or draws a document byte range into a rectangle on a
 ** pair of surfaces (draw and measure); the return value is the next document
 ** position after the formatted page. Positions are Sci::Position; the
 ** rectangle is PRectangle; surface handles are Platform SurfaceID values.
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

// Extra points added to the base style size when printing (can be negative).
void Editor::SetPrintMagnification(int magnification) {
	view.printParameters.magnification = magnification;
}

int Editor::GetPrintMagnification() const noexcept {
	return view.printParameters.magnification;
}

// How colours are rendered for print output.
void Editor::SetPrintColourMode(PrintOption mode) {
	view.printParameters.colourMode = mode;
}

PrintOption Editor::GetPrintColourMode() const noexcept {
	return view.printParameters.colourMode;
}

// Print layout wrap: Word or None (other wrap values collapse to None).
void Editor::SetPrintWrapMode(Wrap wrapMode) {
	view.printParameters.wrapState = (wrapMode == Wrap::Word) ? Wrap::Word : Wrap::None;
}

Wrap Editor::GetPrintWrapMode() const noexcept {
	return view.printParameters.wrapState;
}

// This is mostly copied from the Paint path but omits margin markers,
// selection, and caret. An enabled line-number margin is printed.
Sci::Position Editor::FormatRange(bool draw, Sci::Position cpMin, Sci::Position cpMax,
	PRectangle rc, SurfaceID hdc, SurfaceID hdcTarget) {
	AutoSurface surface(hdc, this, Technology::Default);
	AutoSurface surfaceMeasure(hdcTarget, this, Technology::Default);
	if (!surface || !surfaceMeasure) {
		return 0;
	}
	return view.FormatRange(draw, cpMin, cpMax, rc, surface, surfaceMeasure, *this, vs);
}
