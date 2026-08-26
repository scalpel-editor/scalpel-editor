// GitHub-style emoji shortcode catalog: alias prefix lookup over a generated
// table. +1 and thumbsup resolve to the same emoji. Matching is
// case-insensitive ASCII and does not search tags.

#ifndef EMOJICATALOG_H
#define EMOJICATALOG_H

#include <string_view>
#include <vector>

namespace Scalpel {

/** One unique emoji that matched a shortcode prefix. */
struct EmojiMatch {
	std::string_view emoji;
	/** Alias that matched the query (preferred alias when the query is empty). */
	std::string_view alias;
	/** First gemoji alias for this emoji. */
	std::string_view preferredAlias;
};

/**
 * Unique emoji whose aliases start with query, ignoring ASCII case.
 * An empty query returns every catalog emoji in source order, using each
 * preferred alias. Exact alias matches sort first, then matched aliases
 * alphabetically. When several aliases of one emoji match, the exact match
 * wins, then the shortest alias, then the alphabetically earlier alias.
 */
[[nodiscard]] std::vector<EmojiMatch> MatchEmojiPrefix(std::string_view query);

}

#endif
