#include "catch.hpp"

#include "EmojiCatalog.h"

using Scalpel::EmojiMatch;
using Scalpel::FindEmojiToken;
using Scalpel::IsEmojiShortcodeChar;
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

TEST_CASE("shortcode characters are ASCII letters digits plus minus underscore") {
	CHECK(IsEmojiShortcodeChar('a'));
	CHECK(IsEmojiShortcodeChar('Z'));
	CHECK(IsEmojiShortcodeChar('0'));
	CHECK(IsEmojiShortcodeChar('+'));
	CHECK(IsEmojiShortcodeChar('-'));
	CHECK(IsEmojiShortcodeChar('_'));
	CHECK_FALSE(IsEmojiShortcodeChar(':'));
	CHECK_FALSE(IsEmojiShortcodeChar(' '));
	CHECK_FALSE(IsEmojiShortcodeChar('\n'));
}

TEST_CASE("live emoji token accepts colon queries at a word boundary") {
	const auto thumb = FindEmojiToken(":thumb");
	REQUIRE(thumb.has_value());
	CHECK(thumb->colonOffset == 0);
	CHECK(thumb->query == "thumb");

	const auto plus = FindEmojiToken("hello :+1");
	REQUIRE(plus.has_value());
	CHECK(plus->colonOffset == 6);
	CHECK(plus->query == "+1");

	const auto bare = FindEmojiToken(":");
	REQUIRE(bare.has_value());
	CHECK(bare->colonOffset == 0);
	CHECK(bare->query.empty());

	const auto afterNewline = FindEmojiToken("line\n:smile");
	REQUIRE(afterNewline.has_value());
	CHECK(afterNewline->colonOffset == 5);
	CHECK(afterNewline->query == "smile");
}

TEST_CASE("live emoji token rejects colon after a letter digit or UTF-8 byte") {
	CHECK_FALSE(FindEmojiToken("https:").has_value());
	CHECK_FALSE(FindEmojiToken("12:00").has_value());
	CHECK_FALSE(FindEmojiToken("foo:").has_value());
	CHECK_FALSE(FindEmojiToken("12:").has_value());
	CHECK_FALSE(FindEmojiToken("thumb").has_value());
	CHECK_FALSE(FindEmojiToken("").has_value());
	CHECK_FALSE(FindEmojiToken("café:").has_value());
}
