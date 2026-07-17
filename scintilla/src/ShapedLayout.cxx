// scalpel-editor layout from shaped runs: screen-line positions for LTR English.

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "Geometry.h"
#include "Platform.h"
#include "ShapedLayout.h"

namespace Scintilla::Internal {

namespace {

/**
 * LTR screen-line layout.
 *
 * xAtCaret[i] is the x of a caret after the first i bytes (same as LineLayout::positions[i]).
 * Multi-byte characters share one end x for every trail byte, matching MeasureWidths.
 */
class ScreenLineLayout final : public IScreenLineLayout {
public:
	explicit ScreenLineLayout(std::vector<XYPOSITION> xAtCaret_) : xAtCaret(std::move(xAtCaret_)) {
		caretStops.push_back(0);
		for (size_t position = 1; position < xAtCaret.size(); position++) {
			if ((position + 1 == xAtCaret.size()) ||
				xAtCaret[position] != xAtCaret[position + 1]) {
				caretStops.push_back(position);
			}
		}
	}

	size_t PositionFromX(XYPOSITION xDistance, bool charPosition) override {
		if (xAtCaret.size() <= 1) {
			return 0;
		}
		const size_t last = caretStops.back();
		if (xDistance <= xAtCaret[0]) {
			return 0;
		}
		if (xDistance >= xAtCaret[last]) {
			return last;
		}
		// Mirror LineLayout::FindPositionFromX over the whole line range.
		for (size_t stop = 0; stop + 1 < caretStops.size(); stop++) {
			const size_t pos = caretStops[stop];
			const size_t next = caretStops[stop + 1];
			if (charPosition) {
				if (xDistance < xAtCaret[next]) {
					return pos;
				}
			} else if (xDistance < (xAtCaret[pos] + xAtCaret[next]) / 2.0) {
				return pos;
			}
		}
		return last;
	}

	XYPOSITION XFromPosition(size_t caretPosition) override {
		if (xAtCaret.empty()) {
			return 0.0;
		}
		if (caretPosition >= xAtCaret.size()) {
			return xAtCaret.back();
		}
		return xAtCaret[caretPosition];
	}

	std::vector<Interval> FindRangeIntervals(size_t start, size_t end) override {
		if (start > end) {
			std::swap(start, end);
		}
		const XYPOSITION left = XFromPosition(start);
		const XYPOSITION right = XFromPosition(end);
		if (right <= left) {
			return {};
		}
		return {Interval{left, right}};
	}

private:
	std::vector<XYPOSITION> xAtCaret;
	std::vector<size_t> caretStops;
};

}

std::unique_ptr<IScreenLineLayout> LayoutScreenLine(const IScreenLine *screenLine) {
	if (!screenLine) {
		return std::make_unique<ScreenLineLayout>(std::vector<XYPOSITION>{0.0});
	}

	const size_t len = screenLine->Length();
	std::vector<XYPOSITION> xAtCaret(len + 1, 0.0);
	for (size_t position = 0; position <= len; position++) {
		xAtCaret[position] = screenLine->XFromPosition(position);
	}
	return std::make_unique<ScreenLineLayout>(std::move(xAtCaret));
}

}
