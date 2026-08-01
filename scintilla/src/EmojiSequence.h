// Milestone-limited emoji sequence segmentation for shaping fallback.

#ifndef EMOJISEQUENCE_H
#define EMOJISEQUENCE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Scintilla::Internal {

/**
 * Presentation preference for one shaping unit.
 *
 * Unspecified means no emoji/text preference for face selection (ordinary
 * text and characters whose default presentation is text). Text and Emoji
 * come from explicit selectors or from constructed emoji sequences and the
 * Unicode Emoji_Presentation property.
 */
enum class EmojiPresentation {
	Unspecified = 0,
	Text = 1,
	Emoji = 2,
};

/**
 * One run of input characters that must stay on a single face during shaping.
 *
 * charBegin/charEnd are indices into the parallel code-point / character array
 * (not byte offsets). A unit is either one ordinary character or one supported
 * multi-code-point emoji sequence. presentation records the unit's face
 * preference for fallback selection.
 */
struct EmojiUnit {
	size_t charBegin = 0;
	size_t charEnd = 0;
	EmojiPresentation presentation = EmojiPresentation::Unspecified;
};

/**
 * True when codePoint has Unicode Emoji_Presentation=Yes (default emoji form).
 * Lookup is local and allocation-free against a checked-in sorted range table.
 */
bool HasEmojiPresentation(char32_t codePoint) noexcept;

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
 * Presentation rules:
 * - U+FE0E on a base-plus-selector unit requests Text; U+FE0F requests Emoji.
 * - Modifier, ZWJ, regional-indicator flag, and keycap units request Emoji.
 * - An unqualified single character with Emoji_Presentation requests Emoji;
 *   other single characters stay Unspecified.
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
