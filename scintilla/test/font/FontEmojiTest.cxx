#include "FontTest.h"

#include "EmojiSequence.h"

namespace {

// UTF-8 helpers for fixture sequences (byte strings stay explicit in tests).
const std::string grinning = "\xF0\x9F\x98\x80"; // U+1F600
const std::string whiteSmile = "\xE2\x98\xBA"; // U+263A
const std::string vs15 = "\xEF\xB8\x8E"; // U+FE0E
const std::string vs16 = "\xEF\xB8\x8F"; // U+FE0F
const std::string thumbsUp = "\xF0\x9F\x91\x8D"; // U+1F44D
const std::string toneLight = "\xF0\x9F\x8F\xBB"; // U+1F3FB
const std::string woman = "\xF0\x9F\x91\xA9"; // U+1F469
const std::string zwj = "\xE2\x80\x8D"; // U+200D
const std::string rocket = "\xF0\x9F\x9A\x80"; // U+1F680
const std::string regionalU = "\xF0\x9F\x87\xBA"; // U+1F1FA
const std::string regionalS = "\xF0\x9F\x87\xB8"; // U+1F1F8
const std::string keycap = "\xE2\x83\xA3"; // U+20E3

std::shared_ptr<FontFace> LoadEmoji(FontCache &cache, double size = 16.0) {
	return cache.LoadPath(emojiPath, FontParameters("fixture-emoji", size));
}

std::vector<char32_t> CodePointsOf(std::string_view text) {
	std::vector<char32_t> points;
	size_t i = 0;
	while (i < text.size()) {
		const int classified = UTF8Classify(text.data() + i, text.size() - i);
		REQUIRE_FALSE(classified & UTF8MaskInvalid);
		const size_t len = static_cast<size_t>(classified & UTF8MaskWidth);
		points.push_back(static_cast<char32_t>(
			UnicodeFromUTF8(std::string_view(text.data() + i, len))));
		i += len;
	}
	return points;
}

bool AllGlyphsUseFace(const ShapedRun &run, const std::shared_ptr<FontFace> &face) {
	for (const ShapedGlyph &glyph : run.glyphs) {
		if (glyph.face != face) {
			return false;
		}
	}
	return !run.glyphs.empty();
}

}

TEST_CASE("SegmentEmojiUnits keeps supported multi-code-point forms together") {
	const std::string text = std::string("A") + grinning + thumbsUp + toneLight +
		woman + zwj + rocket + regionalU + regionalS + "1" + vs16 + keycap + "B";
	const auto points = CodePointsOf(text);
	const auto units = SegmentEmojiUnits(points.data(), points.size());

	// A | grinning | thumb+tone | woman+zwj+rocket | flag | keycap | B
	REQUIRE(units.size() == 7);
	CHECK(units[0].charEnd - units[0].charBegin == 1);
	CHECK(units[1].charEnd - units[1].charBegin == 1);
	CHECK(units[2].charEnd - units[2].charBegin == 2);
	CHECK(units[3].charEnd - units[3].charBegin == 3);
	CHECK(units[4].charEnd - units[4].charBegin == 2);
	CHECK(units[5].charEnd - units[5].charBegin == 3);
	CHECK(units[6].charEnd - units[6].charBegin == 1);
}

TEST_CASE("SegmentEmojiUnits keeps text and emoji variation selectors with the base") {
	const auto emojiVs = CodePointsOf(whiteSmile + vs16);
	const auto textVs = CodePointsOf(whiteSmile + vs15);
	const auto emojiUnits = SegmentEmojiUnits(emojiVs.data(), emojiVs.size());
	const auto textUnits = SegmentEmojiUnits(textVs.data(), textVs.size());
	REQUIRE(emojiUnits.size() == 1);
	REQUIRE(textUnits.size() == 1);
	CHECK(emojiUnits[0].charEnd == 2);
	CHECK(textUnits[0].charEnd == 2);
}

TEST_CASE("Emoji fixture shapes a plain emoji") {
	FontCache cache;
	const auto emoji = LoadEmoji(cache);
	const ShapedRun run = ShapeText(grinning, emoji);
	REQUIRE_FALSE(run.glyphs.empty());
	CHECK(run.Width() > 0.0);
	CHECK(run.caretStops == std::vector<size_t>{0, grinning.size()});
	CHECK(AllGlyphsUseFace(run, emoji));
}

TEST_CASE("Emoji fixture shapes variation selector sequences as one unit") {
	FontCache cache;
	const auto emoji = LoadEmoji(cache);
	const std::string text = whiteSmile + vs16;
	const ShapedRun run = ShapeText(text, emoji);
	REQUIRE_FALSE(run.glyphs.empty());
	CHECK(run.Width() > 0.0);
	CHECK(run.caretStops == std::vector<size_t>{0, text.size()});
	// All bytes of the sequence share the end position.
	for (size_t i = 0; i < text.size(); i++) {
		CHECK(run.byteEndPositions[i] == run.Width());
	}
}

TEST_CASE("Emoji fixture shapes a skin-tone modifier sequence") {
	FontCache cache;
	const auto emoji = LoadEmoji(cache);
	const std::string text = thumbsUp + toneLight;
	const ShapedRun run = ShapeText(text, emoji);
	REQUIRE_FALSE(run.glyphs.empty());
	CHECK(emoji->ShapesSequence(CodePointsOf(text).data(), CodePointsOf(text).size()));
	CHECK(run.caretStops == std::vector<size_t>{0, text.size()});
	CHECK(AllGlyphsUseFace(run, emoji));
}

TEST_CASE("Emoji fixture shapes a ZWJ sequence without internal caret stops") {
	FontCache cache;
	const auto emoji = LoadEmoji(cache);
	const std::string text = woman + zwj + rocket;
	const ShapedRun run = ShapeText(text, emoji);
	REQUIRE_FALSE(run.glyphs.empty());
	CHECK(run.Width() > 0.0);
	CHECK(run.caretStops == std::vector<size_t>{0, text.size()});
	for (const ShapedGlyph &glyph : run.glyphs) {
		CHECK(glyph.cluster == 0);
		CHECK(glyph.face == emoji);
	}
	for (size_t i = 0; i < text.size(); i++) {
		CHECK(run.byteEndPositions[i] == run.Width());
	}
}

TEST_CASE("Emoji fixture shapes a regional-indicator flag") {
	FontCache cache;
	const auto emoji = LoadEmoji(cache);
	const std::string text = regionalU + regionalS;
	const ShapedRun run = ShapeText(text, emoji);
	REQUIRE_FALSE(run.glyphs.empty());
	CHECK(run.caretStops == std::vector<size_t>{0, text.size()});
	CHECK(AllGlyphsUseFace(run, emoji));
}

TEST_CASE("Emoji fixture shapes a keycap sequence") {
	FontCache cache;
	const auto emoji = LoadEmoji(cache);
	const std::string text = "1" + vs16 + keycap;
	const ShapedRun run = ShapeText(text, emoji);
	REQUIRE_FALSE(run.glyphs.empty());
	CHECK(run.caretStops == std::vector<size_t>{0, text.size()});
	CHECK(AllGlyphsUseFace(run, emoji));
}

TEST_CASE("ShapeText keeps surrounding ASCII on the primary face") {
	FontCache cache;
	const auto primary = LoadPrimary(cache);
	const auto emoji = LoadEmoji(cache);
	const std::string text = "A" + grinning + "B";
	const ShapedRun run = ShapeText(text, primary, FontFallback::Fixed({emoji}));

	REQUIRE(run.byteEndPositions.size() == text.size());
	CHECK(MonotonicEnds(run.byteEndPositions));
	// Caret: before A, before emoji, before B, end.
	REQUIRE(run.caretStops == std::vector<size_t>{0, 1, 1 + grinning.size(), text.size()});

	bool sawPrimaryA = false;
	bool sawEmoji = false;
	bool sawPrimaryB = false;
	for (const ShapedGlyph &glyph : run.glyphs) {
		if (glyph.cluster == 0) {
			CHECK(glyph.face == primary);
			sawPrimaryA = true;
		} else if (glyph.cluster == 1) {
			CHECK(glyph.face == emoji);
			sawEmoji = true;
		} else if (glyph.cluster == 1 + grinning.size()) {
			CHECK(glyph.face == primary);
			sawPrimaryB = true;
		}
	}
	CHECK(sawPrimaryA);
	CHECK(sawEmoji);
	CHECK(sawPrimaryB);
	CHECK(run.Width() > 0.0);
}

TEST_CASE("ShapeText emoji fallback failure keeps the primary face") {
	FontCache cache;
	const auto primary = LoadPrimary(cache);
	REQUIRE_FALSE(primary->HasGlyph(U'\U0001F600'));
	const ShapedRun run = ShapeText(grinning, primary, FontFallback::Fixed({}));
	REQUIRE_FALSE(run.glyphs.empty());
	CHECK(AllGlyphsUseFace(run, primary));
}

TEST_CASE("ZWJ sequence is not split across per-code-point faces") {
	FontCache cache;
	const auto primary = LoadPrimary(cache);
	const auto emoji = LoadEmoji(cache);
	// Primary has no emoji; fixed emoji must take the whole ZWJ sequence.
	const std::string text = woman + zwj + rocket;
	const ShapedRun run = ShapeText(text, primary, FontFallback::Fixed({emoji}));
	REQUIRE_FALSE(run.glyphs.empty());
	CHECK(AllGlyphsUseFace(run, emoji));
	CHECK(run.caretStops == std::vector<size_t>{0, text.size()});
}
