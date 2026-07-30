// Shared vertical and horizontal scroll ranges for chrome layout and host callbacks.

#ifndef SCROLLMETRICS_H
#define SCROLLMETRICS_H

#include "EditorBasicTypes.h"

namespace Scalpel {

/**
 * One axis of scroll range for chrome layout and interaction.
 * Units are display lines for vertical bars and logical pixels for horizontal.
 */
struct ScrollAxisMetrics {
	Scintilla::Line position = 0;
	Scintilla::Line upperBound = 0;
	Scintilla::Line pageSize = 0;
	Scintilla::Line pageIncrement = 0;
	bool visible = false;
};

/** Vertical and horizontal ranges owned by ApplicationEditor host callbacks. */
struct ScrollMetrics {
	ScrollAxisMetrics vertical;
	ScrollAxisMetrics horizontal;
};

}

#endif
