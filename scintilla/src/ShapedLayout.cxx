// scalpel-editor layout from shaped runs: screen-line positions for LTR English.

#include <algorithm>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "FontPlatform.h"
#include "Geometry.h"
#include "Platform.h"
#include "ShapedLayout.h"
#include "ShapedRun.h"
#include "UniConversion.h"

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
	}

	size_t PositionFromX(XYPOSITION xDistance, bool charPosition) override {
		if (xAtCaret.size() <= 1) {
			return 0;
		}
		const size_t last = xAtCaret.size() - 1;
		if (xDistance <= xAtCaret[0]) {
			return 0;
		}
		if (xDistance >= xAtCaret[last]) {
			return last;
		}
		// Mirror LineLayout::FindPositionFromX over the whole line range.
		size_t pos = 0;
		while (pos < last) {
			if (charPosition) {
				if (xDistance < xAtCaret[pos + 1]) {
					return pos;
				}
			} else if (xDistance < (xAtCaret[pos] + xAtCaret[pos + 1]) / 2.0) {
				return pos;
			}
			pos++;
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
};

size_t CharacterByteLength(std::string_view text, size_t offset) noexcept {
	if (offset >= text.size()) {
		return 0;
	}
	const int classified = UTF8Classify(text.data() + offset, text.size() - offset);
	if (classified & UTF8MaskInvalid) {
		return 1;
	}
	return static_cast<size_t>(classified & UTF8MaskWidth);
}

}

std::unique_ptr<IScreenLineLayout> LayoutScreenLine(
	const IScreenLine *screenLine,
	ShapedRunCache *cache,
	const std::vector<std::shared_ptr<FontFace>> &fallbacks) {
	if (!screenLine) {
		return std::make_unique<ScreenLineLayout>(std::vector<XYPOSITION>{0.0});
	}

	const std::string_view text = screenLine->Text();
	const size_t len = text.size();
	std::vector<XYPOSITION> xAtCaret(len + 1, 0.0);
	XYPOSITION x = 0.0;
	size_t i = 0;
	while (i < len) {
		const XYPOSITION reprWidth = screenLine->RepresentationWidth(i);
		if (reprWidth > 0.0) {
			const size_t charLen = CharacterByteLength(text, i);
			x += reprWidth;
			for (size_t b = 1; b <= charLen; b++) {
				xAtCaret[i + b] = x;
			}
			i += charLen;
			continue;
		}
		if (text[i] == '\t') {
			x = screenLine->TabPositionAfter(x);
			xAtCaret[i + 1] = x;
			i++;
			continue;
		}

		// Maximal same-font plain-text span (no tabs, no representations).
		const Font *font = screenLine->FontOfPosition(i);
		const size_t spanStart = i;
		i++;
		while (i < len) {
			if (text[i] == '\t') {
				break;
			}
			if (screenLine->RepresentationWidth(i) > 0.0) {
				break;
			}
			if (screenLine->FontOfPosition(i) != font) {
				break;
			}
			i++;
		}
		const std::string_view span = text.substr(spanStart, i - spanStart);
		std::shared_ptr<FontFace> primary = SharedFaceFromFont(font);
		if (!primary) {
			throw std::runtime_error("LayoutScreenLine requires a platform Font with a face");
		}
		std::shared_ptr<const ShapedRun> run;
		if (cache) {
			run = cache->Get(span, primary, fallbacks);
		} else {
			run = std::make_shared<const ShapedRun>(ShapeText(span, primary, fallbacks));
		}
		for (size_t b = 0; b < span.size(); b++) {
			xAtCaret[spanStart + b + 1] = x + run->byteEndPositions[b];
		}
		x = xAtCaret[i];
	}
	return std::make_unique<ScreenLineLayout>(std::move(xAtCaret));
}

}
