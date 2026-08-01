// Milestone-limited emoji sequence segmentation for shaping fallback.

#include "EmojiSequence.h"

namespace Scintilla::Internal {

namespace {

bool IsRegionalIndicator(char32_t c) noexcept {
	return c >= 0x1F1E6 && c <= 0x1F1FF;
}

bool IsEmojiModifier(char32_t c) noexcept {
	return c >= 0x1F3FB && c <= 0x1F3FF;
}

bool IsVariationSelector(char32_t c) noexcept {
	return c == 0xFE0E || c == 0xFE0F;
}

bool IsZWJ(char32_t c) noexcept {
	return c == 0x200D;
}

bool IsKeycapBase(char32_t c) noexcept {
	return (c >= U'0' && c <= U'9') || c == U'#' || c == U'*';
}

bool IsCombiningEnclosingKeycap(char32_t c) noexcept {
	return c == 0x20E3;
}

// Consume optional VS then optional skin-tone after a base at [begin, end).
size_t ConsumePresentationAndModifier(const char32_t *cps, size_t n, size_t j) noexcept {
	if (j < n && IsVariationSelector(cps[j])) {
		j++;
	}
	if (j < n && IsEmojiModifier(cps[j])) {
		j++;
	}
	return j;
}

size_t EndOfUnit(const char32_t *cps, size_t n, size_t i) noexcept {
	if (i >= n) {
		return i;
	}

	// Regional-indicator flag pair.
	if (IsRegionalIndicator(cps[i]) && i + 1 < n && IsRegionalIndicator(cps[i + 1])) {
		return i + 2;
	}

	// Keycap: [0-9#*] FE0F? 20E3
	if (IsKeycapBase(cps[i])) {
		size_t j = i + 1;
		if (j < n && cps[j] == 0xFE0F) {
			j++;
		}
		if (j < n && IsCombiningEnclosingKeycap(cps[j])) {
			return j + 1;
		}
	}

	// Base + optional VS + optional modifier + ZWJ chains.
	size_t j = ConsumePresentationAndModifier(cps, n, i + 1);
	while (j < n && IsZWJ(cps[j]) && j + 1 < n) {
		j++; // ZWJ
		j++; // following base
		j = ConsumePresentationAndModifier(cps, n, j);
	}
	return j;
}

}

bool IsEmojiCoverageIgnorable(char32_t codePoint) noexcept {
	return codePoint == 0x200D || codePoint == 0xFE0E || codePoint == 0xFE0F;
}

std::vector<EmojiUnit> SegmentEmojiUnits(const char32_t *codePoints, size_t count) {
	std::vector<EmojiUnit> units;
	if (!codePoints || count == 0) {
		return units;
	}
	size_t i = 0;
	while (i < count) {
		const size_t end = EndOfUnit(codePoints, count, i);
		units.push_back(EmojiUnit{i, end > i ? end : i + 1});
		i = units.back().charEnd;
	}
	return units;
}

}
