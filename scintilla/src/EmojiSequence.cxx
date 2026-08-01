// Milestone-limited emoji sequence segmentation for shaping fallback.

#include "EmojiSequence.h"

#include <algorithm>
#include <cstddef>

namespace Scintilla::Internal {

namespace {

// Inclusive [lo, hi] ranges: Emoji_Presentation=Yes.
// Generated from Unicode 16.0.0 ucd/emoji/emoji-data.txt
// SHA-256 f1365a5173eee18e1f98b240cdc492e84a25f1ce7e0c9d1094eb29c41a22696a
// 80 ranges, 1212 code points.
// Regenerate: tools/generate-emoji-presentation.py <emoji-data.txt>
constexpr char32_t kEmojiPresentationRanges[][2] = {
	{0x231A, 0x231B},
	{0x23E9, 0x23EC},
	{0x23F0, 0x23F0},
	{0x23F3, 0x23F3},
	{0x25FD, 0x25FE},
	{0x2614, 0x2615},
	{0x2648, 0x2653},
	{0x267F, 0x267F},
	{0x2693, 0x2693},
	{0x26A1, 0x26A1},
	{0x26AA, 0x26AB},
	{0x26BD, 0x26BE},
	{0x26C4, 0x26C5},
	{0x26CE, 0x26CE},
	{0x26D4, 0x26D4},
	{0x26EA, 0x26EA},
	{0x26F2, 0x26F3},
	{0x26F5, 0x26F5},
	{0x26FA, 0x26FA},
	{0x26FD, 0x26FD},
	{0x2705, 0x2705},
	{0x270A, 0x270B},
	{0x2728, 0x2728},
	{0x274C, 0x274C},
	{0x274E, 0x274E},
	{0x2753, 0x2755},
	{0x2757, 0x2757},
	{0x2795, 0x2797},
	{0x27B0, 0x27B0},
	{0x27BF, 0x27BF},
	{0x2B1B, 0x2B1C},
	{0x2B50, 0x2B50},
	{0x2B55, 0x2B55},
	{0x1F004, 0x1F004},
	{0x1F0CF, 0x1F0CF},
	{0x1F18E, 0x1F18E},
	{0x1F191, 0x1F19A},
	{0x1F1E6, 0x1F1FF},
	{0x1F201, 0x1F201},
	{0x1F21A, 0x1F21A},
	{0x1F22F, 0x1F22F},
	{0x1F232, 0x1F236},
	{0x1F238, 0x1F23A},
	{0x1F250, 0x1F251},
	{0x1F300, 0x1F320},
	{0x1F32D, 0x1F335},
	{0x1F337, 0x1F37C},
	{0x1F37E, 0x1F393},
	{0x1F3A0, 0x1F3CA},
	{0x1F3CF, 0x1F3D3},
	{0x1F3E0, 0x1F3F0},
	{0x1F3F4, 0x1F3F4},
	{0x1F3F8, 0x1F43E},
	{0x1F440, 0x1F440},
	{0x1F442, 0x1F4FC},
	{0x1F4FF, 0x1F53D},
	{0x1F54B, 0x1F54E},
	{0x1F550, 0x1F567},
	{0x1F57A, 0x1F57A},
	{0x1F595, 0x1F596},
	{0x1F5A4, 0x1F5A4},
	{0x1F5FB, 0x1F64F},
	{0x1F680, 0x1F6C5},
	{0x1F6CC, 0x1F6CC},
	{0x1F6D0, 0x1F6D2},
	{0x1F6D5, 0x1F6D7},
	{0x1F6DC, 0x1F6DF},
	{0x1F6EB, 0x1F6EC},
	{0x1F6F4, 0x1F6FC},
	{0x1F7E0, 0x1F7EB},
	{0x1F7F0, 0x1F7F0},
	{0x1F90C, 0x1F93A},
	{0x1F93C, 0x1F945},
	{0x1F947, 0x1F9FF},
	{0x1FA70, 0x1FA7C},
	{0x1FA80, 0x1FA89},
	{0x1FA8F, 0x1FAC6},
	{0x1FACE, 0x1FADC},
	{0x1FADF, 0x1FAE9},
	{0x1FAF0, 0x1FAF8},
};

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
// sawModifier is set when a skin-tone code point is consumed.
size_t ConsumePresentationAndModifier(
	const char32_t *cps, size_t n, size_t j, bool *sawModifier) noexcept {
	if (j < n && IsVariationSelector(cps[j])) {
		j++;
	}
	if (j < n && IsEmojiModifier(cps[j])) {
		if (sawModifier) {
			*sawModifier = true;
		}
		j++;
	}
	return j;
}

struct UnitExtent {
	size_t end = 0;
	EmojiPresentation presentation = EmojiPresentation::Unspecified;
};

UnitExtent ExtentOfUnit(const char32_t *cps, size_t n, size_t i) noexcept {
	UnitExtent extent{i + 1, EmojiPresentation::Unspecified};
	if (i >= n) {
		extent.end = i;
		return extent;
	}

	// Regional-indicator flag pair.
	if (IsRegionalIndicator(cps[i]) && i + 1 < n && IsRegionalIndicator(cps[i + 1])) {
		return UnitExtent{i + 2, EmojiPresentation::Emoji};
	}

	// Keycap: [0-9#*] FE0F? 20E3
	if (IsKeycapBase(cps[i])) {
		size_t j = i + 1;
		if (j < n && cps[j] == 0xFE0F) {
			j++;
		}
		if (j < n && IsCombiningEnclosingKeycap(cps[j])) {
			return UnitExtent{j + 1, EmojiPresentation::Emoji};
		}
	}

	// Base + optional VS + optional modifier + ZWJ chains.
	EmojiPresentation fromSelector = EmojiPresentation::Unspecified;
	bool sawModifier = false;
	bool sawZwj = false;
	size_t j = i + 1;
	if (j < n && IsVariationSelector(cps[j])) {
		fromSelector = (cps[j] == 0xFE0E)
			? EmojiPresentation::Text
			: EmojiPresentation::Emoji;
		j++;
	}
	if (j < n && IsEmojiModifier(cps[j])) {
		sawModifier = true;
		j++;
	}
	while (j < n && IsZWJ(cps[j]) && j + 1 < n) {
		sawZwj = true;
		j++; // ZWJ
		j++; // following base
		j = ConsumePresentationAndModifier(cps, n, j, &sawModifier);
	}
	extent.end = j;

	if (sawZwj || sawModifier) {
		// Constructed emoji sequences request emoji presentation.
		extent.presentation = EmojiPresentation::Emoji;
		return extent;
	}
	if (fromSelector != EmojiPresentation::Unspecified) {
		// Explicit VS on a base-plus-selector unit wins.
		extent.presentation = fromSelector;
		return extent;
	}
	// Single unqualified character: Emoji_Presentation or unspecified.
	if (j == i + 1 && HasEmojiPresentation(cps[i])) {
		extent.presentation = EmojiPresentation::Emoji;
	}
	return extent;
}

}

bool HasEmojiPresentation(char32_t codePoint) noexcept {
	const auto *begin = std::begin(kEmojiPresentationRanges);
	const auto *end = std::end(kEmojiPresentationRanges);
	const auto *it = std::lower_bound(begin, end, codePoint,
		[](const char32_t range[2], char32_t value) noexcept {
			return range[1] < value;
		});
	return it != end && (*it)[0] <= codePoint && codePoint <= (*it)[1];
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
		const UnitExtent extent = ExtentOfUnit(codePoints, count, i);
		const size_t end = extent.end > i ? extent.end : i + 1;
		units.push_back(EmojiUnit{i, end, extent.presentation});
		i = units.back().charEnd;
	}
	return units;
}

}
