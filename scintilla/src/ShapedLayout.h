// scalpel-editor layout from shaped runs: screen-line positions for LTR English.

#ifndef SHAPEDLAYOUT_H
#define SHAPEDLAYOUT_H

#include <memory>

#include "Platform.h"

namespace Scintilla::Internal {

/**
 * Lay out one screen line using the same shaped-run results as measurement.
 *
 * Copies the positions already measured for LineLayout so wrapping, hit
 * testing, and selection use the same shaped widths. English LTR only; no
 * mixed-direction reordering. Returned layout answers PositionFromX,
 * XFromPosition, and FindRangeIntervals from those caret x positions.
 */
std::unique_ptr<IScreenLineLayout> LayoutScreenLine(const IScreenLine *screenLine);

}

#endif
