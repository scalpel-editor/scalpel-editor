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

TEST_CASE("HasEmojiPresentation matches Unicode 16.0 boundaries") {
	// Default-emoji face (grinning) is in Emoji_Presentation.
	CHECK(HasEmojiPresentation(U'\U0001F600'));
	// Range start/end samples from the checked-in table.
	CHECK(HasEmojiPresentation(U'\u231A'));
	CHECK(HasEmojiPresentation(U'\u231B'));
	CHECK_FALSE(HasEmojiPresentation(U'\u2319'));
	CHECK_FALSE(HasEmojiPresentation(U'\u231C'));
	// White smiling face is Emoji but default text presentation.
	CHECK_FALSE(HasEmojiPresentation(U'\u263A'));
	// Ordinary ASCII and unpaired high surrogates stay out of the table.
	CHECK_FALSE(HasEmojiPresentation(U'A'));
	CHECK_FALSE(HasEmojiPresentation(U'\0'));
	CHECK_FALSE(HasEmojiPresentation(static_cast<char32_t>(0x110000)));
	// Late-range sample still covered.
	CHECK(HasEmojiPresentation(U'\U0001FAF8'));
	CHECK_FALSE(HasEmojiPresentation(U'\U0001FAF9'));
}

TEST_CASE("SegmentEmojiUnits keeps supported multi-code-point forms together") {
	const std::string text = std::string("A") + grinning + thumbsUp + toneLight +
		woman + zwj + rocket + regionalU + regionalS + "1" + vs16 + keycap + "B";
	const auto points = CodePointsOf(text);
	const auto units = SegmentEmojiUnits(points.data(), points.size());

	// A | grinning | thumb+tone | woman+zwj+rocket | flag | keycap | B
	REQUIRE(units.size() == 7);
	CHECK(units[0].charEnd - units[0].charBegin == 1);
	CHECK(units[0].presentation == EmojiPresentation::Unspecified);
	CHECK(units[1].charEnd - units[1].charBegin == 1);
	CHECK(units[1].presentation == EmojiPresentation::Emoji);
	CHECK(units[2].charEnd - units[2].charBegin == 2);
	CHECK(units[2].presentation == EmojiPresentation::Emoji);
	CHECK(units[3].charEnd - units[3].charBegin == 3);
	CHECK(units[3].presentation == EmojiPresentation::Emoji);
	CHECK(units[4].charEnd - units[4].charBegin == 2);
	CHECK(units[4].presentation == EmojiPresentation::Emoji);
	CHECK(units[5].charEnd - units[5].charBegin == 3);
	CHECK(units[5].presentation == EmojiPresentation::Emoji);
	CHECK(units[6].charEnd - units[6].charBegin == 1);
	CHECK(units[6].presentation == EmojiPresentation::Unspecified);
}

TEST_CASE("SegmentEmojiUnits keeps text and emoji variation selectors with the base") {
	const auto emojiVs = CodePointsOf(whiteSmile + vs16);
	const auto textVs = CodePointsOf(whiteSmile + vs15);
	const auto emojiUnits = SegmentEmojiUnits(emojiVs.data(), emojiVs.size());
	const auto textUnits = SegmentEmojiUnits(textVs.data(), textVs.size());
	REQUIRE(emojiUnits.size() == 1);
	REQUIRE(textUnits.size() == 1);
	CHECK(emojiUnits[0].charEnd == 2);
	CHECK(emojiUnits[0].presentation == EmojiPresentation::Emoji);
	CHECK(textUnits[0].charEnd == 2);
	CHECK(textUnits[0].presentation == EmojiPresentation::Text);
}

TEST_CASE("SegmentEmojiUnits classifies default emoji and default-text symbols") {
	const auto grin = CodePointsOf(grinning);
	const auto smile = CodePointsOf(whiteSmile);
	const auto letter = CodePointsOf("A");
	const auto grinUnits = SegmentEmojiUnits(grin.data(), grin.size());
	const auto smileUnits = SegmentEmojiUnits(smile.data(), smile.size());
	const auto letterUnits = SegmentEmojiUnits(letter.data(), letter.size());
	REQUIRE(grinUnits.size() == 1);
	REQUIRE(smileUnits.size() == 1);
	REQUIRE(letterUnits.size() == 1);
	CHECK(grinUnits[0].presentation == EmojiPresentation::Emoji);
	// Default-text emoji-capable symbol stays Unspecified without a selector.
	CHECK(smileUnits[0].presentation == EmojiPresentation::Unspecified);
	CHECK(letterUnits[0].presentation == EmojiPresentation::Unspecified);
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

TEST_CASE("FontFace HasColor distinguishes colour and monochrome fixtures") {
	FontCache cache;
	const auto mono = LoadEmojiMono(cache);
	const auto emoji = LoadEmoji(cache);
	const auto primary = LoadPrimary(cache);
	CHECK_FALSE(mono->HasColor());
	CHECK_FALSE(primary->HasColor());
	CHECK(emoji->HasColor());
	CHECK(mono->HasGlyph(U'\U0001F600'));
	CHECK(mono->HasGlyph(U'\u263A'));
	CHECK(emoji->HasGlyph(U'\U0001F600'));
	CHECK(emoji->HasGlyph(U'\u263A'));
}

TEST_CASE("default emoji presentation bypasses covering monochrome primary") {
	FontCache cache;
	const auto mono = LoadEmojiMono(cache);
	const auto emoji = LoadEmoji(cache);
	REQUIRE(mono->HasGlyph(U'\U0001F600'));
	REQUIRE_FALSE(mono->HasColor());
	const FontFallback fallback = FontFallback::Fixed({emoji});
	const ShapedRun run = ShapeText(grinning, mono, fallback);
	REQUIRE_FALSE(run.glyphs.empty());
	CHECK(AllGlyphsUseFace(run, emoji));
}

TEST_CASE("emoji variation selector prefers the colour face") {
	FontCache cache;
	const auto mono = LoadEmojiMono(cache);
	const auto emoji = LoadEmoji(cache);
	const std::string text = whiteSmile + vs16;
	const ShapedRun run = ShapeText(text, mono, FontFallback::Fixed({emoji}));
	REQUIRE_FALSE(run.glyphs.empty());
	CHECK(AllGlyphsUseFace(run, emoji));
}

TEST_CASE("text variation selector prefers the monochrome face") {
	FontCache cache;
	const auto mono = LoadEmojiMono(cache);
	const auto emoji = LoadEmoji(cache);
	const std::string text = whiteSmile + vs15;
	const ShapedRun run = ShapeText(text, mono, FontFallback::Fixed({emoji}));
	REQUIRE_FALSE(run.glyphs.empty());
	CHECK(AllGlyphsUseFace(run, mono));
}

TEST_CASE("ordinary text retains the monochrome primary with colour fallback present") {
	FontCache cache;
	const auto mono = LoadEmojiMono(cache);
	const auto emoji = LoadEmoji(cache);
	const ShapedRun run = ShapeText("AB", mono, FontFallback::Fixed({emoji}));
	REQUIRE_FALSE(run.glyphs.empty());
	CHECK(AllGlyphsUseFace(run, mono));
}

TEST_CASE("neighbouring ASCII stays on primary when default emoji uses colour") {
	FontCache cache;
	const auto mono = LoadEmojiMono(cache);
	const auto emoji = LoadEmoji(cache);
	const std::string text = "A" + grinning + "B";
	const ShapedRun run = ShapeText(text, mono, FontFallback::Fixed({emoji}));
	REQUIRE(run.caretStops == std::vector<size_t>{0, 1, 1 + grinning.size(), text.size()});
	bool sawPrimaryA = false;
	bool sawEmoji = false;
	bool sawPrimaryB = false;
	for (const ShapedGlyph &glyph : run.glyphs) {
		if (glyph.cluster == 0) {
			CHECK(glyph.face == mono);
			sawPrimaryA = true;
		} else if (glyph.cluster == 1) {
			CHECK(glyph.face == emoji);
			sawEmoji = true;
		} else if (glyph.cluster == 1 + grinning.size()) {
			CHECK(glyph.face == mono);
			sawPrimaryB = true;
		}
	}
	CHECK(sawPrimaryA);
	CHECK(sawEmoji);
	CHECK(sawPrimaryB);
}

TEST_CASE("complete emoji sequences stay on one colour face with monochrome primary") {
	FontCache cache;
	const auto mono = LoadEmojiMono(cache);
	const auto emoji = LoadEmoji(cache);
	const FontFallback fallback = FontFallback::Fixed({emoji});
	const std::string zwjText = woman + zwj + rocket;
	const std::string flagText = regionalU + regionalS;
	const std::string keycapText = "1" + vs16 + keycap;
	for (const std::string &text : {zwjText, flagText, keycapText}) {
		const ShapedRun run = ShapeText(text, mono, fallback);
		REQUIRE_FALSE(run.glyphs.empty());
		CHECK(AllGlyphsUseFace(run, emoji));
		CHECK(run.caretStops == std::vector<size_t>{0, text.size()});
	}
}

TEST_CASE("emoji presentation falls back to monochrome when no colour face covers") {
	FontCache cache;
	const auto mono = LoadEmojiMono(cache);
	// No colour candidate: unrestricted policy keeps the covering monochrome primary.
	const ShapedRun run = ShapeText(grinning, mono, FontFallback::Fixed({}));
	REQUIRE_FALSE(run.glyphs.empty());
	CHECK(AllGlyphsUseFace(run, mono));
}

TEST_CASE("text presentation falls back to colour when no monochrome face covers") {
	FontCache cache;
	const auto primary = LoadPrimary(cache);
	const auto emoji = LoadEmoji(cache);
	REQUIRE_FALSE(primary->HasGlyph(U'\u263A'));
	const std::string text = whiteSmile + vs15;
	// Prefer non-colour, but only the colour fixture covers the base.
	const ShapedRun run = ShapeText(text, primary, FontFallback::Fixed({emoji}));
	REQUIRE_FALSE(run.glyphs.empty());
	CHECK(AllGlyphsUseFace(run, emoji));
}

TEST_CASE("production ResolveFallback cache does not cross presentation preference") {
	FontCache cache;
	const auto mono = LoadEmojiMono(cache);
	const char32_t grin = U'\U0001F600';
	const char32_t smile = U'\u263A';
	// Warm unrestricted and emoji-preferred decisions for the same code points.
	const auto unrestricted = cache.ResolveFallback(*mono, grin, EmojiPresentation::Unspecified);
	const auto emojiPref = cache.ResolveFallback(*mono, grin, EmojiPresentation::Emoji);
	const auto textSmile = cache.ResolveFallback(*mono, smile, EmojiPresentation::Text);
	const auto emojiSmile = cache.ResolveFallback(*mono, smile, EmojiPresentation::Emoji);
	// Monochrome primary already covers grin; unrestricted may return empty (no
	// need for a different face) while emoji preference may resolve a colour face
	// when one is installed. Keys must stay independent either way.
	CHECK(cache.ResolveFallback(*mono, grin, EmojiPresentation::Unspecified) == unrestricted);
	CHECK(cache.ResolveFallback(*mono, grin, EmojiPresentation::Emoji) == emojiPref);
	CHECK(cache.ResolveFallback(*mono, smile, EmojiPresentation::Text) == textSmile);
	CHECK(cache.ResolveFallback(*mono, smile, EmojiPresentation::Emoji) == emojiSmile);
	// Text vs emoji preferences for the same base cannot share one decision.
	// When both resolve, the faces may differ by colour capability.
	if (textSmile && emojiSmile) {
		CHECK(textSmile->HasColor() != emojiSmile->HasColor());
	}
}
