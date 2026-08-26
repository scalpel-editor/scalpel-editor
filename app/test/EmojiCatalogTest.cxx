#include "catch.hpp"

#include "EmojiCatalog.h"

using Scalpel::EmojiMatch;
using Scalpel::MatchEmojiPrefix;

namespace {

[[nodiscard]] const EmojiMatch *FindEmoji(const std::vector<EmojiMatch> &matches,
	std::string_view emoji) {
	for (const EmojiMatch &match : matches) {
		if (match.emoji == emoji) {
			return &match;
		}
	}
	return nullptr;
}

}

TEST_CASE("plus one and thumbsup resolve to the same thumbs-up emoji") {
	const std::vector<EmojiMatch> plus = MatchEmojiPrefix("+1");
	const std::vector<EmojiMatch> thumbsup = MatchEmojiPrefix("thumbsup");
	REQUIRE(plus.size() == 1);
	REQUIRE(thumbsup.size() == 1);
	CHECK(plus[0].emoji == "👍");
	CHECK(thumbsup[0].emoji == plus[0].emoji);
	CHECK(plus[0].alias == "+1");
	CHECK(thumbsup[0].alias == "thumbsup");
	CHECK(plus[0].preferredAlias == "+1");
	CHECK(thumbsup[0].preferredAlias == "+1");
}

TEST_CASE("thumb prefix returns thumbsup and thumbsdown as unique emoji") {
	const std::vector<EmojiMatch> matches = MatchEmojiPrefix("thumb");
	REQUIRE(matches.size() >= 2);
	const EmojiMatch *up = FindEmoji(matches, "👍");
	const EmojiMatch *down = FindEmoji(matches, "👎");
	REQUIRE(up != nullptr);
	REQUIRE(down != nullptr);
	CHECK(up->alias == "thumbsup");
	CHECK(down->alias == "thumbsdown");
	CHECK(up->preferredAlias == "+1");
	CHECK(down->preferredAlias == "-1");
	CHECK(matches[0].alias == "thumbsdown");
	CHECK(matches[1].alias == "thumbsup");
}

TEST_CASE("prefix matching ignores ASCII case") {
	const std::vector<EmojiMatch> lower = MatchEmojiPrefix("thumbsup");
	const std::vector<EmojiMatch> upper = MatchEmojiPrefix("THUMBSUP");
	REQUIRE(lower.size() == 1);
	REQUIRE(upper.size() == 1);
	CHECK(upper[0].emoji == lower[0].emoji);
	CHECK(upper[0].alias == "thumbsup");
}

TEST_CASE("unknown query returns no matches") {
	CHECK(MatchEmojiPrefix("not_an_emoji_shortcode").empty());
	CHECK(MatchEmojiPrefix("thumbsupx").empty());
}

TEST_CASE("empty query returns every catalog emoji with its preferred alias") {
	const std::vector<EmojiMatch> matches = MatchEmojiPrefix("");
	REQUIRE(matches.size() == 1870);
	CHECK(matches.front().preferredAlias == matches.front().alias);
	const EmojiMatch *up = FindEmoji(matches, "👍");
	REQUIRE(up != nullptr);
	CHECK(up->preferredAlias == "+1");
	CHECK(up->alias == "+1");
}
