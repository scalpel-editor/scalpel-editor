// Milestone-limited emoji sequence segmentation for shaping fallback.

#ifndef EMOJISEQUENCE_H
#define EMOJISEQUENCE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Scintilla::Internal {

/**
 * One run of input characters that must stay on a single face during shaping.
 *
 * charBegin/charEnd are indices into the parallel code-point / character array
 * (not byte offsets). A unit is either one ordinary character or one supported
 * multi-code-point emoji sequence.
 */
struct EmojiUnit {
	size_t charBegin = 0;
	size_t charEnd = 0;
};

/**
 * Segment code points into ordinary characters and supported emoji sequences.
 *
 * Supported multi-code-point forms for this milestone:
 * - emoji presentation selectors (base + U+FE0E / U+FE0F)
 * - emoji skin-tone modifiers (base + U+1F3FB..U+1F3FF)
 * - ZWJ-linked emoji (base (ZWJ base)+ with optional VS and modifiers)
 * - regional-indicator flag pairs (U+1F1E6..U+1F1FF twice)
 * - keycap sequences ([0-9#*] + optional U+FE0F + U+20E3)
 *
 * This is not a complete UAX #29 grapheme-boundary implementation. Cursor
 * movement, deletion, and SafeSegment still use the broader document rules.
 * Empty input yields no units.
 */
std::vector<EmojiUnit> SegmentEmojiUnits(const char32_t *codePoints, size_t count);

/** True for ZWJ and emoji presentation variation selectors in coverage checks. */
bool IsEmojiCoverageIgnorable(char32_t codePoint) noexcept;

}

#endif
