// scalpel-editor layout from shaped runs: screen-line positions for LTR English.

#ifndef SHAPEDLAYOUT_H
#define SHAPEDLAYOUT_H

#include <memory>
#include <vector>

#include "FontPlatform.h"
#include "Platform.h"
#include "ShapedRun.h"

namespace Scintilla::Internal {

/**
 * Lay out one screen line using the same shaped-run results as measurement.
 *
 * Walks the line left-to-right: tabs use TabPositionAfter, non-zero
 * RepresentationWidth entries use that fixed width for the UTF-8 character,
 * and other spans are shaped with ShapeText (or the optional cache). English
 * LTR only; no mixed-direction reordering. Returned layout answers
 * PositionFromX, XFromPosition, and FindRangeIntervals from the stored
 * caret x positions.
 */
std::unique_ptr<IScreenLineLayout> LayoutScreenLine(
	const IScreenLine *screenLine,
	ShapedRunCache *cache = nullptr,
	const std::vector<std::shared_ptr<FontFace>> &fallbacks = {});

}

#endif
