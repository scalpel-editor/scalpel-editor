#include "EmojiCatalog.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Scalpel {
namespace {

#include "EmojiCatalogData.inc"

[[nodiscard]] char AsciiLower(char c) noexcept {
	if (c >= 'A' && c <= 'Z') {
		return static_cast<char>(c - 'A' + 'a');
	}
	return c;
}

[[nodiscard]] int ComparePrefix(std::string_view alias,
	std::string_view query) noexcept {
	const std::size_t n = query.size();
	for (std::size_t i = 0; i < n; ++i) {
		if (i == alias.size()) {
			return -1;
		}
		const char a = alias[i];
		const char q = AsciiLower(query[i]);
		if (a < q) {
			return -1;
		}
		if (a > q) {
			return 1;
		}
	}
	return 0;
}

[[nodiscard]] bool AliasStartsWith(std::string_view alias,
	std::string_view query) noexcept {
	return ComparePrefix(alias, query) == 0;
}

[[nodiscard]] bool BetterAlias(std::string_view candidate, bool candidateExact,
	std::string_view current, bool currentExact) noexcept {
	if (candidateExact != currentExact) {
		return candidateExact;
	}
	if (candidate.size() != current.size()) {
		return candidate.size() < current.size();
	}
	return candidate < current;
}

}

bool IsEmojiShortcodeChar(char c) noexcept {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') || c == '_' || c == '+' || c == '-';
}

std::optional<EmojiToken> FindEmojiToken(std::string_view textBeforeCaret) {
	std::size_t queryStart = textBeforeCaret.size();
	while (queryStart > 0 &&
		IsEmojiShortcodeChar(textBeforeCaret[queryStart - 1])) {
		--queryStart;
	}
	if (queryStart == 0 || textBeforeCaret[queryStart - 1] != ':') {
		return std::nullopt;
	}
	const std::size_t colonOffset = queryStart - 1;
	if (colonOffset > 0) {
		const unsigned char previous = static_cast<unsigned char>(
			textBeforeCaret[colonOffset - 1]);
		if (IsEmojiShortcodeChar(static_cast<char>(previous)) ||
			previous >= 0x80) {
			return std::nullopt;
		}
	}
	return EmojiToken{colonOffset, textBeforeCaret.substr(queryStart)};
}

std::vector<EmojiMatch> MatchEmojiPrefix(std::string_view query) {
	const std::size_t emojiCount =
		sizeof(kEmojiGlyphs) / sizeof(kEmojiGlyphs[0]);
	if (query.empty()) {
		std::vector<EmojiMatch> matches;
		matches.reserve(emojiCount);
		for (std::size_t i = 0; i < emojiCount; ++i) {
			matches.push_back({kEmojiGlyphs[i], kPreferredAliases[i],
				kPreferredAliases[i]});
		}
		return matches;
	}

	const EmojiAliasRow *const aliasBegin = kAliases;
	const EmojiAliasRow *const aliasEnd =
		kAliases + (sizeof(kAliases) / sizeof(kAliases[0]));
	const EmojiAliasRow *first = std::lower_bound(aliasBegin, aliasEnd, query,
		[](const EmojiAliasRow &row, std::string_view needle) {
			return ComparePrefix(row.alias, needle) < 0;
		});

	struct BestMatch {
		std::string_view alias;
		bool exact = false;
		bool seen = false;
	};
	std::vector<BestMatch> best(emojiCount);
	for (const EmojiAliasRow *row = first; row != aliasEnd; ++row) {
		if (!AliasStartsWith(row->alias, query)) {
			break;
		}
		const bool exact = row->alias.size() == query.size();
		BestMatch &slot = best[row->emojiIndex];
		if (!slot.seen || BetterAlias(row->alias, exact, slot.alias, slot.exact)) {
			slot.alias = row->alias;
			slot.exact = exact;
			slot.seen = true;
		}
	}

	std::vector<EmojiMatch> matches;
	for (std::size_t i = 0; i < emojiCount; ++i) {
		if (!best[i].seen) {
			continue;
		}
		matches.push_back({kEmojiGlyphs[i], best[i].alias, kPreferredAliases[i]});
	}
	std::sort(matches.begin(), matches.end(),
		[query](const EmojiMatch &a, const EmojiMatch &b) {
			const bool aExact = a.alias.size() == query.size();
			const bool bExact = b.alias.size() == query.size();
			if (aExact != bExact) {
				return aExact;
			}
			if (a.alias != b.alias) {
				return a.alias < b.alias;
			}
			return a.emoji < b.emoji;
		});
	return matches;
}

}
