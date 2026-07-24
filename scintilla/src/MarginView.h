// Scintilla source code edit control
/** @file MarginView.h
 ** Defines the appearance of the editor margin.
 **/
// Copyright 1998-2014 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#ifndef MARGINVIEW_H
#define MARGINVIEW_H

#include <memory>

#include "Document.h"
#include "EditModel.h"
#include "Geometry.h"
#include "Platform.h"
#include "ViewStyle.h"

namespace Scintilla::Internal {

void DrawWrapMarker(Surface *surface, PRectangle rcPlace, bool isEndMarker, ColourRGBA wrapColour);

typedef void (*DrawWrapMarkerFn)(Surface *surface, PRectangle rcPlace, bool isEndMarker, ColourRGBA wrapColour);

/**
* MarginView draws the margins.
*/
class MarginView {
public:
	std::unique_ptr<Surface> pixmapSelMargin;
	std::unique_ptr<Surface> pixmapSelPattern;
	std::unique_ptr<Surface> pixmapSelPatternOffset1;
	// Highlight current folding block
	HighlightDelimiter highlightDelimiter;

	int wrapMarkerPaddingRight; // right-most pixel padding of wrap markers
	/** Optional override when the default wrap-marker drawing is not used. */
	DrawWrapMarkerFn customDrawWrapMarker;

	MarginView() noexcept;

	void DropGraphics() noexcept;
	void RefreshPixMaps(Surface *surfaceWindow, const ViewStyle &vsDraw);
	void PaintOneMargin(Surface *surface, PRectangle rc, PRectangle rcOneMargin, const MarginStyle &marginStyle,
		const EditModel &model, const ViewStyle &vs, XYPOSITION clientTop = 0) const;
	void PaintMargin(Surface *surface, Sci::Line topLine, PRectangle rc, PRectangle rcMargin,
		const EditModel &model, const ViewStyle &vs, XYPOSITION clientTop = 0);
};

}

#endif
